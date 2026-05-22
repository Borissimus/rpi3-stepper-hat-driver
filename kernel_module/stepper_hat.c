/* Copyright (c) Borys Nykytiuk <borysworking@gmail.com> */

#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../include/uapi/stepper_hat_ioctl.h"

#define STEPPER_HAT_NAME "stepper_hat"
#define STEPPER_HAT_MOTOR_COUNT 2
#define STEPPER_HAT_MIN_STEP_DELAY_US 2U
#define STEPPER_HAT_DEFAULT_STEP_DELAY_US 5000U

static bool enable_active_high = true;
module_param(enable_active_high, bool, 0444);
MODULE_PARM_DESC(enable_active_high,
		 "Use active-high motor enable (default true for Waveshare Rev2.1)");

static char *gpiochip_label = "pinctrl-bcm2835";
module_param(gpiochip_label, charp, 0444);
MODULE_PARM_DESC(gpiochip_label,
		 "GPIO chip label used to translate BCM offsets into global GPIO numbers");

struct stepper_hat_gpio_config {
	unsigned int enable_gpio;
	unsigned int dir_gpio;
	unsigned int step_gpio;
	unsigned int mode_gpios[3];
};

struct stepper_hat_gpio_labels {
	const char *enable;
	const char *dir;
	const char *step;
	const char *mode[3];
};

struct stepper_hat_move_state {
	u8 direction;
	u8 flags;
	u32 steps;
	u32 step_delay_us;
};

struct stepper_hat_motor {
	u8 id;
	struct stepper_hat_gpio_config gpio;
	struct mutex lock;
	wait_queue_head_t wq;
	struct task_struct *worker;
	struct stepper_hat_move_state pending_move;
	bool command_pending;
	bool busy;
	bool enabled;
	bool stop_requested;
	u8 control_mode;
	u8 microstep;
	u8 direction;
	bool hold_enabled_after_move;
	u32 default_step_delay_us;
	u32 target_steps;
	u32 completed_steps;
	s64 position_steps;
};

struct stepper_hat_device {
	struct miscdevice miscdev;
	struct stepper_hat_motor motors[STEPPER_HAT_MOTOR_COUNT];
};

static struct stepper_hat_device stepper_hat = {
	.miscdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = STEPPER_HAT_NAME,
	},
};

static const struct stepper_hat_gpio_config stepper_hat_default_gpio_map[] = {
	{
		.enable_gpio = 12,
		.dir_gpio = 13,
		.step_gpio = 19,
		.mode_gpios = { 16, 17, 20 },
	},
	{
		.enable_gpio = 4,
		.dir_gpio = 24,
		.step_gpio = 18,
		.mode_gpios = { 21, 22, 27 },
	},
};

static void stepper_hat_apply_gpio_base(struct stepper_hat_gpio_config *gpio,
					unsigned int gpio_base)
{
	int i;

	gpio->enable_gpio += gpio_base;
	gpio->dir_gpio += gpio_base;
	gpio->step_gpio += gpio_base;
	for (i = 0; i < ARRAY_SIZE(gpio->mode_gpios); i++)
		gpio->mode_gpios[i] += gpio_base;
}

static unsigned int stepper_hat_detect_gpio_base(void)
{
	struct gpio_device *gdev;
	int base;

	gdev = gpio_device_find_by_label(gpiochip_label);
	if (!gdev) {
		pr_warn("%s: gpiochip label '%s' not found, assuming legacy base 0\n",
			STEPPER_HAT_NAME, gpiochip_label);
		return 0;
	}

	base = gpio_device_get_base(gdev);
	gpio_device_put(gdev);
	if (base < 0) {
		pr_warn("%s: gpiochip '%s' has invalid base %d, assuming 0\n",
			STEPPER_HAT_NAME, gpiochip_label, base);
		return 0;
	}

	pr_info("%s: using gpiochip '%s' base %d\n",
		STEPPER_HAT_NAME, gpiochip_label, base);
	return base;
}

static const struct stepper_hat_gpio_labels stepper_hat_default_labels[] = {
	{
		.enable = "stepper_hat_m1_enable",
		.dir = "stepper_hat_m1_dir",
		.step = "stepper_hat_m1_step",
		.mode = {
			"stepper_hat_m1_m0",
			"stepper_hat_m1_m1",
			"stepper_hat_m1_m2",
		},
	},
	{
		.enable = "stepper_hat_m2_enable",
		.dir = "stepper_hat_m2_dir",
		.step = "stepper_hat_m2_step",
		.mode = {
			"stepper_hat_m2_m0",
			"stepper_hat_m2_m1",
			"stepper_hat_m2_m2",
		},
	},
};

static const u8 stepper_hat_microstep_table[][3] = {
	{ 0, 0, 0 },
	{ 1, 0, 0 },
	{ 0, 1, 0 },
	{ 1, 1, 0 },
	{ 0, 0, 1 },
	{ 1, 0, 1 },
};

static const char *stepper_hat_motor_label(u8 id)
{
	return id == STEPPER_HAT_MOTOR1 ? "m1" : "m2";
}

static bool stepper_hat_motor_valid(u8 motor)
{
	return motor == STEPPER_HAT_MOTOR1 || motor == STEPPER_HAT_MOTOR2;
}

static struct stepper_hat_motor *stepper_hat_get_motor(u8 motor)
{
	if (!stepper_hat_motor_valid(motor))
		return NULL;

	return &stepper_hat.motors[motor - 1];
}

static bool stepper_hat_motor_idle(struct stepper_hat_motor *motor)
{
	return !READ_ONCE(motor->busy) && !READ_ONCE(motor->command_pending);
}

static u32 stepper_hat_delay_pad(u32 delay_us)
{
	return max_t(u32, 10U, delay_us / 8U);
}

static void stepper_hat_sleep_step_delay(u32 delay_us)
{
	usleep_range(delay_us, delay_us + stepper_hat_delay_pad(delay_us));
}

static int stepper_hat_validate_delay(u32 delay_us)
{
	if (delay_us < STEPPER_HAT_MIN_STEP_DELAY_US)
		return -EINVAL;

	return 0;
}

static void stepper_hat_set_enabled_locked(struct stepper_hat_motor *motor,
					       bool enabled)
{
	int value = enable_active_high ? enabled : !enabled;

	gpio_set_value(motor->gpio.enable_gpio, value);
	motor->enabled = enabled;
}

static void stepper_hat_disable_locked(struct stepper_hat_motor *motor)
{
	stepper_hat_set_enabled_locked(motor, false);
}

static void stepper_hat_apply_microstep_locked(struct stepper_hat_motor *motor)
{
	int i;

	if (motor->control_mode == STEPPER_HAT_CONTROL_HARDWARE) {
		for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++)
			gpio_set_value(motor->gpio.mode_gpios[i], 0);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++)
		gpio_set_value(motor->gpio.mode_gpios[i],
			       stepper_hat_microstep_table[motor->microstep][i]);
}

static void stepper_hat_fill_status_locked(struct stepper_hat_motor *motor,
					       struct stepper_hat_status *status)
{
	status->motor = motor->id;
	status->busy = motor->busy;
	status->control_mode = motor->control_mode;
	status->microstep = motor->microstep;
	status->enabled = motor->enabled;
	status->direction = motor->direction;
	status->stop_requested = motor->stop_requested;
	status->hold_enabled = motor->hold_enabled_after_move;
	status->step_delay_us = motor->default_step_delay_us;
	status->target_steps = motor->target_steps;
	status->completed_steps = motor->completed_steps;
	status->position_steps = motor->position_steps;
}

static int stepper_hat_worker(void *data)
{
	struct stepper_hat_motor *motor = data;

	while (!kthread_should_stop()) {
		struct stepper_hat_move_state move;
		bool keep_enabled;
		u32 i;

		wait_event_interruptible(motor->wq,
					 kthread_should_stop() ||
					 READ_ONCE(motor->command_pending));
		if (kthread_should_stop())
			break;

		mutex_lock(&motor->lock);
		if (!motor->command_pending) {
			mutex_unlock(&motor->lock);
			continue;
		}

		move = motor->pending_move;
		motor->command_pending = false;
		motor->busy = true;
		motor->stop_requested = false;
		motor->completed_steps = 0;
		motor->target_steps = move.steps;
		motor->direction = move.direction;
		keep_enabled = !!(move.flags & STEPPER_HAT_MOVE_F_KEEP_ENABLED) ||
			       motor->hold_enabled_after_move;

		stepper_hat_set_enabled_locked(motor, true);
		gpio_set_value(motor->gpio.dir_gpio,
			       move.direction == STEPPER_HAT_DIR_BACKWARD);
		mutex_unlock(&motor->lock);

		for (i = 0; i < move.steps && !kthread_should_stop(); i++) {
			bool stop_requested;

			mutex_lock(&motor->lock);
			stop_requested = motor->stop_requested;
			mutex_unlock(&motor->lock);
			if (stop_requested)
				break;

			gpio_set_value(motor->gpio.step_gpio, 1);
			stepper_hat_sleep_step_delay(move.step_delay_us);
			gpio_set_value(motor->gpio.step_gpio, 0);
			stepper_hat_sleep_step_delay(move.step_delay_us);

			mutex_lock(&motor->lock);
			motor->completed_steps = i + 1;
			if (move.direction == STEPPER_HAT_DIR_FORWARD)
				motor->position_steps++;
			else
				motor->position_steps--;
			mutex_unlock(&motor->lock);
		}

		mutex_lock(&motor->lock);
		motor->busy = false;
		if (!keep_enabled || motor->stop_requested)
			stepper_hat_disable_locked(motor);
		motor->stop_requested = false;
		mutex_unlock(&motor->lock);

		wake_up_all(&motor->wq);
	}

	mutex_lock(&motor->lock);
	motor->busy = false;
	motor->command_pending = false;
	motor->stop_requested = false;
	stepper_hat_disable_locked(motor);
	mutex_unlock(&motor->lock);
	wake_up_all(&motor->wq);

	return 0;
}

static int stepper_hat_request_gpio(unsigned int gpio, const char *label)
{
	if (!gpio_is_valid(gpio))
		return -EINVAL;

	return gpio_request(gpio, label);
}

static int stepper_hat_request_motor_gpios(struct stepper_hat_motor *motor)
{
	const struct stepper_hat_gpio_labels *labels;
	int ret;
	int i;

	labels = &stepper_hat_default_labels[motor->id - 1];

	ret = stepper_hat_request_gpio(motor->gpio.enable_gpio, labels->enable);
	if (ret)
		return ret;

	ret = stepper_hat_request_gpio(motor->gpio.dir_gpio, labels->dir);
	if (ret)
		goto err_free_enable;

	ret = stepper_hat_request_gpio(motor->gpio.step_gpio, labels->step);
	if (ret)
		goto err_free_dir;

	for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++) {
		ret = stepper_hat_request_gpio(motor->gpio.mode_gpios[i],
					       labels->mode[i]);
		if (ret)
			goto err_free_modes;
	}

	ret = gpio_direction_output(motor->gpio.enable_gpio,
				    enable_active_high ? 0 : 1);
	if (ret)
		goto err_free_modes;

	ret = gpio_direction_output(motor->gpio.dir_gpio, 0);
	if (ret)
		goto err_free_modes;

	ret = gpio_direction_output(motor->gpio.step_gpio, 0);
	if (ret)
		goto err_free_modes;

	for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++) {
		ret = gpio_direction_output(motor->gpio.mode_gpios[i], 0);
		if (ret)
			goto err_free_all_modes;
	}

	return 0;

err_free_modes:
	while (--i >= 0)
		gpio_free(motor->gpio.mode_gpios[i]);
	goto err_free_step;
err_free_all_modes:
	for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++)
		gpio_free(motor->gpio.mode_gpios[i]);
err_free_step:
	gpio_free(motor->gpio.step_gpio);
err_free_dir:
	gpio_free(motor->gpio.dir_gpio);
err_free_enable:
	gpio_free(motor->gpio.enable_gpio);
	return ret;
}

static void stepper_hat_free_motor_gpios(struct stepper_hat_motor *motor)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(motor->gpio.mode_gpios); i++)
		gpio_free(motor->gpio.mode_gpios[i]);
	gpio_free(motor->gpio.step_gpio);
	gpio_free(motor->gpio.dir_gpio);
	gpio_free(motor->gpio.enable_gpio);
}

static long stepper_hat_ioctl_configure(struct stepper_hat_config __user *argp)
{
	struct stepper_hat_config config;
	struct stepper_hat_motor *motor;

	if (copy_from_user(&config, argp, sizeof(config)))
		return -EFAULT;

	motor = stepper_hat_get_motor(config.motor);
	if (!motor)
		return -EINVAL;

	if (config.control_mode > STEPPER_HAT_CONTROL_SOFTWARE)
		return -EINVAL;
	if (config.microstep > STEPPER_HAT_MICROSTEP_THIRTYSECOND)
		return -EINVAL;
	if (config.default_step_delay_us &&
	    stepper_hat_validate_delay(config.default_step_delay_us))
		return -EINVAL;

	mutex_lock(&motor->lock);
	if (!stepper_hat_motor_idle(motor)) {
		mutex_unlock(&motor->lock);
		return -EBUSY;
	}

	motor->control_mode = config.control_mode;
	motor->microstep = config.microstep;
	motor->hold_enabled_after_move = !!config.hold_enabled;
	if (config.default_step_delay_us)
		motor->default_step_delay_us = config.default_step_delay_us;
	stepper_hat_apply_microstep_locked(motor);
	mutex_unlock(&motor->lock);

	return 0;
}

static long stepper_hat_ioctl_move(struct stepper_hat_move __user *argp)
{
	struct stepper_hat_move move;
	struct stepper_hat_motor *motor;
	int ret;

	if (copy_from_user(&move, argp, sizeof(move)))
		return -EFAULT;

	motor = stepper_hat_get_motor(move.motor);
	if (!motor)
		return -EINVAL;
	if (move.direction > STEPPER_HAT_DIR_BACKWARD || move.steps == 0)
		return -EINVAL;

	mutex_lock(&motor->lock);
	if (!stepper_hat_motor_idle(motor)) {
		mutex_unlock(&motor->lock);
		return -EBUSY;
	}

	if (!move.step_delay_us)
		move.step_delay_us = motor->default_step_delay_us;

	ret = stepper_hat_validate_delay(move.step_delay_us);
	if (ret) {
		mutex_unlock(&motor->lock);
		return ret;
	}

	motor->pending_move.direction = move.direction;
	motor->pending_move.flags = move.flags;
	motor->pending_move.steps = move.steps;
	motor->pending_move.step_delay_us = move.step_delay_us;
	motor->command_pending = true;
	mutex_unlock(&motor->lock);

	wake_up_all(&motor->wq);

	if (!(move.flags & STEPPER_HAT_MOVE_F_WAIT))
		return 0;

	ret = wait_event_interruptible(motor->wq, stepper_hat_motor_idle(motor));
	if (ret)
		return ret;

	return 0;
}

static long stepper_hat_ioctl_enable(struct stepper_hat_enable __user *argp)
{
	struct stepper_hat_enable enable;
	struct stepper_hat_motor *motor;

	if (copy_from_user(&enable, argp, sizeof(enable)))
		return -EFAULT;

	motor = stepper_hat_get_motor(enable.motor);
	if (!motor)
		return -EINVAL;
	if (enable.enabled > 1)
		return -EINVAL;

	mutex_lock(&motor->lock);
	if (!stepper_hat_motor_idle(motor)) {
		mutex_unlock(&motor->lock);
		return -EBUSY;
	}

	stepper_hat_set_enabled_locked(motor, enable.enabled);
	mutex_unlock(&motor->lock);

	return 0;
}

static long stepper_hat_ioctl_stop(struct stepper_hat_stop __user *argp)
{
	struct stepper_hat_stop stop;
	struct stepper_hat_motor *motor;
	int ret;

	if (copy_from_user(&stop, argp, sizeof(stop)))
		return -EFAULT;

	motor = stepper_hat_get_motor(stop.motor);
	if (!motor)
		return -EINVAL;

	mutex_lock(&motor->lock);
	motor->command_pending = false;
	motor->stop_requested = true;
	if (!motor->busy) {
		stepper_hat_disable_locked(motor);
		motor->stop_requested = false;
	}
	mutex_unlock(&motor->lock);

	wake_up_all(&motor->wq);

	ret = wait_event_interruptible(motor->wq, stepper_hat_motor_idle(motor));
	if (ret)
		return ret;

	return 0;
}

static long stepper_hat_ioctl_get_status(struct stepper_hat_status __user *argp)
{
	struct stepper_hat_status status;
	struct stepper_hat_motor *motor;

	if (copy_from_user(&status, argp, sizeof(status)))
		return -EFAULT;

	motor = stepper_hat_get_motor(status.motor);
	if (!motor)
		return -EINVAL;

	mutex_lock(&motor->lock);
	stepper_hat_fill_status_locked(motor, &status);
	mutex_unlock(&motor->lock);

	if (copy_to_user(argp, &status, sizeof(status)))
		return -EFAULT;

	return 0;
}

static long stepper_hat_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	switch (cmd) {
	case STEPPER_HAT_IOC_CONFIGURE:
		return stepper_hat_ioctl_configure((void __user *)arg);
	case STEPPER_HAT_IOC_MOVE:
		return stepper_hat_ioctl_move((void __user *)arg);
	case STEPPER_HAT_IOC_ENABLE:
		return stepper_hat_ioctl_enable((void __user *)arg);
	case STEPPER_HAT_IOC_STOP:
		return stepper_hat_ioctl_stop((void __user *)arg);
	case STEPPER_HAT_IOC_GET_STATUS:
		return stepper_hat_ioctl_get_status((void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations stepper_hat_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = stepper_hat_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.llseek = noop_llseek,
};

static int __init stepper_hat_init(void)
{
	unsigned int gpio_base;
	int ret;
	int i;

	stepper_hat.miscdev.fops = &stepper_hat_fops;
	gpio_base = stepper_hat_detect_gpio_base();

	for (i = 0; i < STEPPER_HAT_MOTOR_COUNT; i++) {
		struct stepper_hat_motor *motor = &stepper_hat.motors[i];

		motor->id = i + 1;
		motor->gpio = stepper_hat_default_gpio_map[i];
		stepper_hat_apply_gpio_base(&motor->gpio, gpio_base);
		mutex_init(&motor->lock);
		init_waitqueue_head(&motor->wq);
		motor->control_mode = STEPPER_HAT_CONTROL_HARDWARE;
		motor->microstep = STEPPER_HAT_MICROSTEP_FULL;
		motor->default_step_delay_us = STEPPER_HAT_DEFAULT_STEP_DELAY_US;
		ret = stepper_hat_request_motor_gpios(motor);
		if (ret)
			goto err_cleanup_motors;

		mutex_lock(&motor->lock);
		stepper_hat_apply_microstep_locked(motor);
		stepper_hat_disable_locked(motor);
		mutex_unlock(&motor->lock);

		motor->worker = kthread_run(stepper_hat_worker, motor,
					    "%s_%s", STEPPER_HAT_NAME,
					    stepper_hat_motor_label(motor->id));
		if (IS_ERR(motor->worker)) {
			ret = PTR_ERR(motor->worker);
			motor->worker = NULL;
			stepper_hat_free_motor_gpios(motor);
			goto err_cleanup_motors;
		}
	}

	ret = misc_register(&stepper_hat.miscdev);
	if (ret)
		goto err_cleanup_motors;

	pr_info("%s: registered /dev/%s for two DRV8825 channels\n",
		STEPPER_HAT_NAME, STEPPER_HAT_NAME);
	return 0;

err_cleanup_motors:
	while (--i >= 0) {
		struct stepper_hat_motor *motor = &stepper_hat.motors[i];

		if (motor->worker) {
			kthread_stop(motor->worker);
			motor->worker = NULL;
		}
		stepper_hat_free_motor_gpios(motor);
	}

	return ret;
}

static void __exit stepper_hat_exit(void)
{
	int i;

	misc_deregister(&stepper_hat.miscdev);

	for (i = 0; i < STEPPER_HAT_MOTOR_COUNT; i++) {
		struct stepper_hat_motor *motor = &stepper_hat.motors[i];

		if (motor->worker) {
			kthread_stop(motor->worker);
			motor->worker = NULL;
		}

		mutex_lock(&motor->lock);
		gpio_set_value(motor->gpio.step_gpio, 0);
		gpio_set_value(motor->gpio.dir_gpio, 0);
		stepper_hat_disable_locked(motor);
		mutex_unlock(&motor->lock);
		stepper_hat_free_motor_gpios(motor);
	}

	pr_info("%s: unloaded\n", STEPPER_HAT_NAME);
}

module_init(stepper_hat_init);
module_exit(stepper_hat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borys");
MODULE_DESCRIPTION("Waveshare Stepper Motor HAT driver for Raspberry Pi");
