# `stepperctl`

Copyright (c) Borys Nykytiuk <borysworking@gmail.com>

`stepperctl` is a user-space CLI for `/dev/stepper_hat`.

This is the recommended split:

- kernel module: owns GPIOs and implements the device ABI
- user-space CLI: parses commands and issues ioctls to the device
- shared UAPI header: keeps both sides on one contract

## Build

Native:

```sh
make
```

Cross-build for the Raspberry Pi image used in this repo:

```sh
make CROSS_COMPILE=aarch64-linux-gnu-
```

The binary is written to `build/stepperctl/stepperctl` relative to the
repository root.

For the full build, deploy, permissions, and hardware test flow, see the
top-level [`README.md`](../../README.md).

## Install on Raspberry Pi

Install the binary into the system path:

```sh
sudo install -m 0755 stepperctl /usr/local/bin/stepperctl
```

Verify that the driver is already loaded and the device exists:

```sh
ls -l /dev/stepper_hat
stepperctl status --all
```

If `/dev/stepper_hat` is missing after reboot, the kernel module is not loaded
yet. Follow the module install and autoload steps in
[`kernel_module/README.md`](../../kernel_module/README.md).

If you want to run `stepperctl` without `sudo`, configure the `udev` rule and
device group ownership described in the top-level [`README.md`](../../README.md).

## Commands

```sh
stepperctl configure --motor 1 --control hardware --microstep full --hold off --delay-us 5000
stepperctl move --motor 1 --dir forward --steps 200 --wait
stepperctl enable --motor 1 --on
stepperctl stop --motor 1
stepperctl status --all
```

Typical first hardware test:

```sh
stepperctl configure --motor 1 --control hardware --microstep full --delay-us 50000
stepperctl enable --motor 1 --on
stepperctl move --motor 1 --dir forward --steps 50 --wait
```
