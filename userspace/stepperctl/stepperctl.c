#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "stepper_hat_ioctl.h"

#define STEPPERCTL_DEFAULT_DEVICE "/dev/stepper_hat"

struct cli_context {
	const char *device;
};

static void print_main_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl <command> [options]\n"
		"\n"
		"Commands:\n"
		"  configure  Configure control mode, microstep, hold, and default delay\n"
		"  move       Run a move on one motor\n"
		"  enable     Explicitly energize or de-energize one motor\n"
		"  stop       Stop one motor and disable it\n"
		"  status     Print current motor state\n"
		"  help       Show help for a command\n"
		"\n"
		"Common option:\n"
		"  --device <path>   Device node, default: %s\n",
		STEPPERCTL_DEFAULT_DEVICE);
}

static void print_configure_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl configure --motor <1|2> [options]\n"
		"\n"
		"Options:\n"
		"  --device <path>\n"
		"  --control <hardware|software>\n"
		"  --microstep <full|half|1/4|1/8|1/16|1/32>\n"
		"  --hold <on|off>\n"
		"  --delay-us <microseconds>\n");
}

static void print_move_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl move --motor <1|2> --dir <forward|backward> --steps <count> [options]\n"
		"\n"
		"Options:\n"
		"  --device <path>\n"
		"  --delay-us <microseconds>\n"
		"  --wait\n"
		"  --keep-enabled\n");
}

static void print_enable_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl enable --motor <1|2> <--on|--off> [--device <path>]\n");
}

static void print_stop_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl stop --motor <1|2> [--device <path>]\n");
}

static void print_status_usage(FILE *stream)
{
	fprintf(stream,
		"Usage: stepperctl status [--motor <1|2> | --all] [--device <path>]\n");
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno || !end || *end != '\0' || parsed > UINT32_MAX)
		return -1;

	*value = (uint32_t)parsed;
	return 0;
}

static int parse_motor(const char *text, uint8_t *motor)
{
	uint32_t parsed;

	if (parse_u32(text, &parsed))
		return -1;
	if (parsed != STEPPER_HAT_MOTOR1 && parsed != STEPPER_HAT_MOTOR2)
		return -1;

	*motor = (uint8_t)parsed;
	return 0;
}

static int parse_direction(const char *text, uint8_t *direction)
{
	if (!strcasecmp(text, "forward")) {
		*direction = STEPPER_HAT_DIR_FORWARD;
		return 0;
	}
	if (!strcasecmp(text, "backward")) {
		*direction = STEPPER_HAT_DIR_BACKWARD;
		return 0;
	}
	return -1;
}

static int parse_control_mode(const char *text, uint8_t *control_mode)
{
	if (!strcasecmp(text, "hardware")) {
		*control_mode = STEPPER_HAT_CONTROL_HARDWARE;
		return 0;
	}
	if (!strcasecmp(text, "software")) {
		*control_mode = STEPPER_HAT_CONTROL_SOFTWARE;
		return 0;
	}
	return -1;
}

static int parse_microstep(const char *text, uint8_t *microstep)
{
	if (!strcasecmp(text, "full") || !strcasecmp(text, "fullstep")) {
		*microstep = STEPPER_HAT_MICROSTEP_FULL;
		return 0;
	}
	if (!strcasecmp(text, "half") || !strcasecmp(text, "halfstep")) {
		*microstep = STEPPER_HAT_MICROSTEP_HALF;
		return 0;
	}
	if (!strcasecmp(text, "1/4") || !strcasecmp(text, "quarter") ||
	    !strcasecmp(text, "1/4step")) {
		*microstep = STEPPER_HAT_MICROSTEP_QUARTER;
		return 0;
	}
	if (!strcasecmp(text, "1/8") || !strcasecmp(text, "eighth") ||
	    !strcasecmp(text, "1/8step")) {
		*microstep = STEPPER_HAT_MICROSTEP_EIGHTH;
		return 0;
	}
	if (!strcasecmp(text, "1/16") || !strcasecmp(text, "sixteenth") ||
	    !strcasecmp(text, "1/16step")) {
		*microstep = STEPPER_HAT_MICROSTEP_SIXTEENTH;
		return 0;
	}
	if (!strcasecmp(text, "1/32") || !strcasecmp(text, "thirtysecond") ||
	    !strcasecmp(text, "1/32step")) {
		*microstep = STEPPER_HAT_MICROSTEP_THIRTYSECOND;
		return 0;
	}
	return -1;
}

static int parse_on_off(const char *text, uint8_t *value)
{
	if (!strcasecmp(text, "on") || !strcasecmp(text, "true") ||
	    !strcasecmp(text, "1") || !strcasecmp(text, "yes")) {
		*value = 1;
		return 0;
	}
	if (!strcasecmp(text, "off") || !strcasecmp(text, "false") ||
	    !strcasecmp(text, "0") || !strcasecmp(text, "no")) {
		*value = 0;
		return 0;
	}
	return -1;
}

static const char *direction_name(uint8_t direction)
{
	return direction == STEPPER_HAT_DIR_BACKWARD ? "backward" : "forward";
}

static const char *control_mode_name(uint8_t control_mode)
{
	return control_mode == STEPPER_HAT_CONTROL_SOFTWARE ?
		"software" : "hardware";
}

static const char *microstep_name(uint8_t microstep)
{
	switch (microstep) {
	case STEPPER_HAT_MICROSTEP_FULL:
		return "full";
	case STEPPER_HAT_MICROSTEP_HALF:
		return "half";
	case STEPPER_HAT_MICROSTEP_QUARTER:
		return "1/4";
	case STEPPER_HAT_MICROSTEP_EIGHTH:
		return "1/8";
	case STEPPER_HAT_MICROSTEP_SIXTEENTH:
		return "1/16";
	case STEPPER_HAT_MICROSTEP_THIRTYSECOND:
		return "1/32";
	default:
		return "unknown";
	}
}

static int open_stepper_device(const char *device)
{
	int fd = open(device, O_RDWR);

	if (fd < 0)
		fprintf(stderr, "Failed to open %s: %s\n", device, strerror(errno));
	return fd;
}

static int get_status(int fd, uint8_t motor, struct stepper_hat_status *status)
{
	memset(status, 0, sizeof(*status));
	status->motor = motor;

	if (ioctl(fd, STEPPER_HAT_IOC_GET_STATUS, status) < 0) {
		fprintf(stderr, "GET_STATUS for motor %u failed: %s\n",
			motor, strerror(errno));
		return -1;
	}

	return 0;
}

static void print_status(const struct stepper_hat_status *status)
{
	printf("motor=%u\n", status->motor);
	printf("busy=%u\n", status->busy);
	printf("control=%s\n", control_mode_name(status->control_mode));
	printf("microstep=%s\n", microstep_name(status->microstep));
	printf("enabled=%u\n", status->enabled);
	printf("direction=%s\n", direction_name(status->direction));
	printf("stop_requested=%u\n", status->stop_requested);
	printf("hold_enabled=%u\n", status->hold_enabled);
	printf("default_delay_us=%u\n", status->step_delay_us);
	printf("target_steps=%u\n", status->target_steps);
	printf("completed_steps=%u\n", status->completed_steps);
	printf("position_steps=%" PRId64 "\n", (int64_t)status->position_steps);
}

static int run_configure(int argc, char **argv, const struct cli_context *ctx)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "motor", required_argument, NULL, 'm' },
		{ "control", required_argument, NULL, 'c' },
		{ "microstep", required_argument, NULL, 's' },
		{ "hold", required_argument, NULL, 'h' },
		{ "delay-us", required_argument, NULL, 'u' },
		{ "help", no_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	struct stepper_hat_status status;
	struct stepper_hat_config config;
	const char *device = ctx->device;
	bool motor_set = false;
	bool control_set = false;
	bool microstep_set = false;
	bool hold_set = false;
	bool delay_set = false;
	uint8_t motor = 0;
	int fd;
	int opt;

	fd = -1;
	memset(&config, 0, sizeof(config));

	optind = 1;
	while ((opt = getopt_long(argc, argv, "d:m:c:s:h:u:", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			if (parse_motor(optarg, &motor)) {
				fprintf(stderr, "Invalid motor: %s\n", optarg);
				return 1;
			}
			motor_set = true;
			break;
		case 'c':
			if (parse_control_mode(optarg, &config.control_mode)) {
				fprintf(stderr, "Invalid control mode: %s\n", optarg);
				return 1;
			}
			control_set = true;
			break;
		case 's':
			if (parse_microstep(optarg, &config.microstep)) {
				fprintf(stderr, "Invalid microstep: %s\n", optarg);
				return 1;
			}
			microstep_set = true;
			break;
		case 'h':
			if (parse_on_off(optarg, &config.hold_enabled)) {
				fprintf(stderr, "Invalid hold value: %s\n", optarg);
				return 1;
			}
			hold_set = true;
			break;
		case 'u':
			if (parse_u32(optarg, &config.default_step_delay_us)) {
				fprintf(stderr, "Invalid delay-us: %s\n", optarg);
				return 1;
			}
			delay_set = true;
			break;
		case 'H':
			print_configure_usage(stdout);
			return 0;
		default:
			print_configure_usage(stderr);
			return 1;
		}
	}

	if (!motor_set) {
		fprintf(stderr, "--motor is required\n");
		print_configure_usage(stderr);
		return 1;
	}

	fd = open_stepper_device(device);
	if (fd < 0)
		return 1;

	if (get_status(fd, motor, &status)) {
		close(fd);
		return 1;
	}

	config.motor = motor;
	if (!control_set)
		config.control_mode = status.control_mode;
	if (!microstep_set)
		config.microstep = status.microstep;
	if (!hold_set)
		config.hold_enabled = status.hold_enabled;
	if (!delay_set)
		config.default_step_delay_us = status.step_delay_us;

	if (ioctl(fd, STEPPER_HAT_IOC_CONFIGURE, &config) < 0) {
		fprintf(stderr, "CONFIGURE failed for motor %u: %s\n",
			config.motor, strerror(errno));
		close(fd);
		return 1;
	}

	printf("Configured motor %u: control=%s microstep=%s hold=%u delay_us=%u\n",
	       config.motor, control_mode_name(config.control_mode),
	       microstep_name(config.microstep), config.hold_enabled,
	       config.default_step_delay_us);
	close(fd);
	return 0;
}

static int run_move(int argc, char **argv, const struct cli_context *ctx)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "motor", required_argument, NULL, 'm' },
		{ "dir", required_argument, NULL, 'r' },
		{ "steps", required_argument, NULL, 's' },
		{ "delay-us", required_argument, NULL, 'u' },
		{ "wait", no_argument, NULL, 'w' },
		{ "keep-enabled", no_argument, NULL, 'k' },
		{ "help", no_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	struct stepper_hat_move move;
	const char *device = ctx->device;
	bool motor_set = false;
	bool dir_set = false;
	bool steps_set = false;
	int fd;
	int opt;

	memset(&move, 0, sizeof(move));
	optind = 1;
	while ((opt = getopt_long(argc, argv, "d:m:r:s:u:wk", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			if (parse_motor(optarg, &move.motor)) {
				fprintf(stderr, "Invalid motor: %s\n", optarg);
				return 1;
			}
			motor_set = true;
			break;
		case 'r':
			if (parse_direction(optarg, &move.direction)) {
				fprintf(stderr, "Invalid direction: %s\n", optarg);
				return 1;
			}
			dir_set = true;
			break;
		case 's':
			if (parse_u32(optarg, &move.steps)) {
				fprintf(stderr, "Invalid steps: %s\n", optarg);
				return 1;
			}
			steps_set = true;
			break;
		case 'u':
			if (parse_u32(optarg, &move.step_delay_us)) {
				fprintf(stderr, "Invalid delay-us: %s\n", optarg);
				return 1;
			}
			break;
		case 'w':
			move.flags |= STEPPER_HAT_MOVE_F_WAIT;
			break;
		case 'k':
			move.flags |= STEPPER_HAT_MOVE_F_KEEP_ENABLED;
			break;
		case 'H':
			print_move_usage(stdout);
			return 0;
		default:
			print_move_usage(stderr);
			return 1;
		}
	}

	if (!motor_set || !dir_set || !steps_set) {
		fprintf(stderr, "--motor, --dir, and --steps are required\n");
		print_move_usage(stderr);
		return 1;
	}

	fd = open_stepper_device(device);
	if (fd < 0)
		return 1;

	if (ioctl(fd, STEPPER_HAT_IOC_MOVE, &move) < 0) {
		fprintf(stderr, "MOVE failed for motor %u: %s\n",
			move.motor, strerror(errno));
		close(fd);
		return 1;
	}

	printf("Queued move: motor=%u dir=%s steps=%u delay_us=%u wait=%u keep_enabled=%u\n",
	       move.motor, direction_name(move.direction), move.steps,
	       move.step_delay_us, !!(move.flags & STEPPER_HAT_MOVE_F_WAIT),
	       !!(move.flags & STEPPER_HAT_MOVE_F_KEEP_ENABLED));
	close(fd);
	return 0;
}

static int run_enable(int argc, char **argv, const struct cli_context *ctx)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "motor", required_argument, NULL, 'm' },
		{ "on", no_argument, NULL, '1' },
		{ "off", no_argument, NULL, '0' },
		{ "help", no_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	struct stepper_hat_enable enable;
	const char *device = ctx->device;
	bool motor_set = false;
	bool state_set = false;
	int fd;
	int opt;

	memset(&enable, 0, sizeof(enable));
	optind = 1;
	while ((opt = getopt_long(argc, argv, "d:m:", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			if (parse_motor(optarg, &enable.motor)) {
				fprintf(stderr, "Invalid motor: %s\n", optarg);
				return 1;
			}
			motor_set = true;
			break;
		case '1':
			enable.enabled = 1;
			state_set = true;
			break;
		case '0':
			enable.enabled = 0;
			state_set = true;
			break;
		case 'H':
			print_enable_usage(stdout);
			return 0;
		default:
			print_enable_usage(stderr);
			return 1;
		}
	}

	if (!motor_set || !state_set) {
		fprintf(stderr, "--motor and one of --on/--off are required\n");
		print_enable_usage(stderr);
		return 1;
	}

	fd = open_stepper_device(device);
	if (fd < 0)
		return 1;

	if (ioctl(fd, STEPPER_HAT_IOC_ENABLE, &enable) < 0) {
		fprintf(stderr, "ENABLE failed for motor %u: %s\n",
			enable.motor, strerror(errno));
		close(fd);
		return 1;
	}

	printf("Motor %u %s\n", enable.motor, enable.enabled ? "enabled" : "disabled");
	close(fd);
	return 0;
}

static int run_stop(int argc, char **argv, const struct cli_context *ctx)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "motor", required_argument, NULL, 'm' },
		{ "help", no_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	struct stepper_hat_stop stop;
	const char *device = ctx->device;
	bool motor_set = false;
	int fd;
	int opt;

	memset(&stop, 0, sizeof(stop));
	optind = 1;
	while ((opt = getopt_long(argc, argv, "d:m:", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			if (parse_motor(optarg, &stop.motor)) {
				fprintf(stderr, "Invalid motor: %s\n", optarg);
				return 1;
			}
			motor_set = true;
			break;
		case 'H':
			print_stop_usage(stdout);
			return 0;
		default:
			print_stop_usage(stderr);
			return 1;
		}
	}

	if (!motor_set) {
		fprintf(stderr, "--motor is required\n");
		print_stop_usage(stderr);
		return 1;
	}

	fd = open_stepper_device(device);
	if (fd < 0)
		return 1;

	if (ioctl(fd, STEPPER_HAT_IOC_STOP, &stop) < 0) {
		fprintf(stderr, "STOP failed for motor %u: %s\n",
			stop.motor, strerror(errno));
		close(fd);
		return 1;
	}

	printf("Motor %u stopped\n", stop.motor);
	close(fd);
	return 0;
}

static int print_one_status(int fd, uint8_t motor)
{
	struct stepper_hat_status status;

	if (get_status(fd, motor, &status))
		return 1;

	print_status(&status);
	return 0;
}

static int run_status(int argc, char **argv, const struct cli_context *ctx)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{ "motor", required_argument, NULL, 'm' },
		{ "all", no_argument, NULL, 'a' },
		{ "help", no_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	const char *device = ctx->device;
	bool all = false;
	bool motor_set = false;
	uint8_t motor = 0;
	int fd;
	int opt;
	int rc = 0;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "d:m:a", options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			if (parse_motor(optarg, &motor)) {
				fprintf(stderr, "Invalid motor: %s\n", optarg);
				return 1;
			}
			motor_set = true;
			break;
		case 'a':
			all = true;
			break;
		case 'H':
			print_status_usage(stdout);
			return 0;
		default:
			print_status_usage(stderr);
			return 1;
		}
	}

	if (all && motor_set) {
		fprintf(stderr, "Use either --motor or --all\n");
		return 1;
	}
	if (!all && !motor_set)
		motor = STEPPER_HAT_MOTOR1;

	fd = open_stepper_device(device);
	if (fd < 0)
		return 1;

	if (all) {
		rc |= print_one_status(fd, STEPPER_HAT_MOTOR1);
		if (!rc)
			printf("\n");
		rc |= print_one_status(fd, STEPPER_HAT_MOTOR2);
	} else {
		rc = print_one_status(fd, motor);
	}

	close(fd);
	return rc;
}

static int run_help(int argc, char **argv)
{
	if (argc < 2) {
		print_main_usage(stdout);
		return 0;
	}

	if (!strcmp(argv[1], "configure"))
		print_configure_usage(stdout);
	else if (!strcmp(argv[1], "move"))
		print_move_usage(stdout);
	else if (!strcmp(argv[1], "enable"))
		print_enable_usage(stdout);
	else if (!strcmp(argv[1], "stop"))
		print_stop_usage(stdout);
	else if (!strcmp(argv[1], "status"))
		print_status_usage(stdout);
	else
		print_main_usage(stdout);

	return 0;
}

int main(int argc, char **argv)
{
	struct cli_context ctx = {
		.device = STEPPERCTL_DEFAULT_DEVICE,
	};

	if (argc < 2) {
		print_main_usage(stderr);
		return 1;
	}

	if (!strcmp(argv[1], "help"))
		return run_help(argc - 1, argv + 1);
	if (!strcmp(argv[1], "configure"))
		return run_configure(argc - 1, argv + 1, &ctx);
	if (!strcmp(argv[1], "move"))
		return run_move(argc - 1, argv + 1, &ctx);
	if (!strcmp(argv[1], "enable"))
		return run_enable(argc - 1, argv + 1, &ctx);
	if (!strcmp(argv[1], "stop"))
		return run_stop(argc - 1, argv + 1, &ctx);
	if (!strcmp(argv[1], "status"))
		return run_status(argc - 1, argv + 1, &ctx);

	fprintf(stderr, "Unknown command: %s\n", argv[1]);
	print_main_usage(stderr);
	return 1;
}
