#ifndef STEPPER_HAT_IOCTL_H
#define STEPPER_HAT_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define STEPPER_HAT_MOTOR1 1
#define STEPPER_HAT_MOTOR2 2

#define STEPPER_HAT_DIR_FORWARD 0
#define STEPPER_HAT_DIR_BACKWARD 1

#define STEPPER_HAT_CONTROL_HARDWARE 0
#define STEPPER_HAT_CONTROL_SOFTWARE 1

#define STEPPER_HAT_MICROSTEP_FULL 0
#define STEPPER_HAT_MICROSTEP_HALF 1
#define STEPPER_HAT_MICROSTEP_QUARTER 2
#define STEPPER_HAT_MICROSTEP_EIGHTH 3
#define STEPPER_HAT_MICROSTEP_SIXTEENTH 4
#define STEPPER_HAT_MICROSTEP_THIRTYSECOND 5

#define STEPPER_HAT_MOVE_F_WAIT 0x01
#define STEPPER_HAT_MOVE_F_KEEP_ENABLED 0x02

struct stepper_hat_config {
	__u8 motor;
	__u8 control_mode;
	__u8 microstep;
	__u8 hold_enabled;
	__u32 default_step_delay_us;
};

struct stepper_hat_move {
	__u8 motor;
	__u8 direction;
	__u8 flags;
	__u8 reserved;
	__u32 steps;
	__u32 step_delay_us;
};

struct stepper_hat_enable {
	__u8 motor;
	__u8 enabled;
	__u8 reserved[2];
};

struct stepper_hat_stop {
	__u8 motor;
	__u8 reserved[3];
};

struct stepper_hat_status {
	__u8 motor;
	__u8 busy;
	__u8 control_mode;
	__u8 microstep;
	__u8 enabled;
	__u8 direction;
	__u8 stop_requested;
	__u8 hold_enabled;
	__u32 step_delay_us;
	__u32 target_steps;
	__u32 completed_steps;
	__s64 position_steps;
};

#define STEPPER_HAT_IOC_MAGIC 's'

#define STEPPER_HAT_IOC_CONFIGURE \
	_IOW(STEPPER_HAT_IOC_MAGIC, 0x01, struct stepper_hat_config)
#define STEPPER_HAT_IOC_MOVE \
	_IOW(STEPPER_HAT_IOC_MAGIC, 0x02, struct stepper_hat_move)
#define STEPPER_HAT_IOC_ENABLE \
	_IOW(STEPPER_HAT_IOC_MAGIC, 0x03, struct stepper_hat_enable)
#define STEPPER_HAT_IOC_STOP \
	_IOW(STEPPER_HAT_IOC_MAGIC, 0x04, struct stepper_hat_stop)
#define STEPPER_HAT_IOC_GET_STATUS \
	_IOWR(STEPPER_HAT_IOC_MAGIC, 0x05, struct stepper_hat_status)

#endif
