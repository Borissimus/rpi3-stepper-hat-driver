FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    bash \
    build-essential \
    make \
    gcc-14-aarch64-linux-gnu \
    g++-14-aarch64-linux-gnu \
    bc \
    bison \
    flex \
    libssl-dev \
    libelf-dev \
    kmod \
    file \
    && rm -rf /var/lib/apt/lists/*

COPY rpi3-kernel-headers/src/linux-headers-6.12.47+rpt-rpi-v8 /usr/src/linux-headers-6.12.47+rpt-rpi-v8
COPY rpi3-kernel-headers/src/linux-headers-6.12.47+rpt-common-rpi /usr/src/linux-headers-6.12.47+rpt-common-rpi
COPY rpi3-kernel-headers/lib/linux-kbuild-6.12.47+rpt /usr/lib/linux-kbuild-6.12.47+rpt


WORKDIR /workspace
CMD ["bash"]
