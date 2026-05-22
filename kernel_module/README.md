# Stepper Motor HAT kernel module

Copyright (c) Borys Nykytiuk <borysworking@gmail.com>

This module replaces the hello-world test with a misc-device driver for the
two DRV8825 channels on the Waveshare Stepper Motor HAT.

The driver registers `/dev/stepper_hat` and owns these BCM GPIOs by default:

- `M1`: `enable=12`, `dir=13`, `step=19`, `mode0=16`, `mode1=17`, `mode2=20`
- `M2`: `enable=4`, `dir=24`, `step=18`, `mode0=21`, `mode1=22`, `mode2=27`

The default enable polarity is active-high, which matches the vendor's Rev2.1
board and demo code. If you need the older polarity, load the module with:

```sh
sudo insmod stepper_hat.ko enable_active_high=0
```

## Build

```sh
make
```

Artifacts are written to `build/kernel_module/` relative to the repository root.

For the Docker-based flow used in this repository, see the top-level
[`README.md`](../README.md).

## IOCTL interface

Shared request/response structures live in
[`include/uapi/stepper_hat_ioctl.h`](../include/uapi/stepper_hat_ioctl.h).

Supported operations:

- `STEPPER_HAT_IOC_CONFIGURE`: set control mode, microstep mode, hold-enable
  behavior, and a default step delay
- `STEPPER_HAT_IOC_MOVE`: queue a move for one motor, optionally waiting until
  it finishes
- `STEPPER_HAT_IOC_ENABLE`: explicitly energize or de-energize one motor
- `STEPPER_HAT_IOC_STOP`: stop a queued or running move and disable the motor
- `STEPPER_HAT_IOC_GET_STATUS`: read the current state and accumulated step
  position

`STEPPER_HAT_IOC_MOVE` accepts two flags:

- `STEPPER_HAT_MOVE_F_WAIT`: block until the move finishes
- `STEPPER_HAT_MOVE_F_KEEP_ENABLED`: leave the bridge enabled after the move

The module defaults to hardware microstep control, which matches the HAT's DIP
switches. Software microstep control only makes sense if your board is wired
for it, as described in the vendor manual.

## Raspberry Pi notes

- On the tested Raspberry Pi 3 kernel, the BCM GPIOs are exposed through a
  dynamic `gpiochip` base. The driver detects the `pinctrl-bcm2835` base at
  load time instead of assuming a fixed global GPIO numbering.
- The module was validated on `6.12.47+rpt-rpi-v8`.
- If you want non-root access to `/dev/stepper_hat`, add a `udev` rule that
  assigns the device to the `gpio` group.

## Install and autoload on Raspberry Pi

To make the module available after reboot, install it into the running kernel's
module tree and load it through `modprobe` instead of `insmod`:

```sh
sudo install -D -m 0644 stepper_hat.ko /lib/modules/$(uname -r)/extra/stepper_hat.ko
sudo depmod -a
sudo modprobe stepper_hat
```

To autoload it on boot:

```sh
echo stepper_hat | sudo tee /etc/modules-load.d/stepper_hat.conf >/dev/null
```

After that, verify:

```sh
lsmod | grep stepper_hat
ls -l /dev/stepper_hat
```

## Notes

- The driver owns both motors independently and can run one worker thread per
  channel.
- The pulse timing is Linux-scheduled GPIO timing, so it is fine for moderate
  speeds but not a substitute for a hard real-time step generator.
- Motors are disabled on `STOP`, module unload, and by default after each move
  to avoid leaving the coils powered unnecessarily.
