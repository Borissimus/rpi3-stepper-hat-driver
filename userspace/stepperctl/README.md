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

The binary is written to `/Users/bnykytiuk/projects/rp3-docker/build/stepperctl/stepperctl`.

For the full build, deploy, permissions, and hardware test flow, see the
top-level [`README.md`](/Users/bnykytiuk/projects/rp3-docker/README.md).

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
