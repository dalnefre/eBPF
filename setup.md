# Machine Setup Instructions

The experiments in this respository require an up-to-date Linux kernel running on a Raspberry Pi 3 or 4.
You will have to compile your own kernel in order to enable XDP functionality.
The kernel source tree also contains tools and header files on which this respository depends.

## Install and Configure Linux

Create a bootable SD card (at least 16GB, class 10 or better)
with the latest version of "Raspberry Pi OS" (previously known as "Raspbian")
from https://www.raspberrypi.org/downloads/raspberry-pi-os/

We do all of our work under the default user "pi", which has "sudo" permission.

You may want to enable
[`ssh`](https://www.raspberrypi.org/documentation/remote-access/ssh/README.md)
or 
[`vnc`](https://www.raspberrypi.org/documentation/remote-access/vnc/README.md)
for remote access (via Wi-Fi).
We expect to use the wired Ethernet port for protocol experiments.

The `ifconfig` command will display lots of information about the available interfaces and their associated addresses.

```
$ ifconfig -a
```

### Choose a Hostname unique to your LAN (optional)

```
$ sudo vi /etc/hostname  # replace "raspberrypi" with your chosen hostname
$ sudo vi /etc/hosts  # replace "raspberrypi" with your chosen hostname
```

Reboot for hostname change to take effect.

```
$ sudo shutdown -r now
```

### Update to latest packages and OS

```
$ sudo apt update
$ sudo apt full-upgrade
$ sudo apt autoremove
```

## Install Development Tools

Edit `/etc/apt/sources.list` and uncomment the `dev-src` line.

```
$ sudo apt install git make clang llvm
$ sudo apt install build-essential bc bison flex
$ sudo apt install libssl-dev libelf-dev libcap-dev libpcap-dev
```

## Build Custom Kernel

We do our development under the `~/dev` directory.

```
$ cd ~
$ mkdir dev
$ cd dev
```

Clone the kernel source repository.

```
$ git clone --depth=1 https://github.com/raspberrypi/linux
```

We use `--depth=1` to prune the extensive history from the repository.

### Configure build

The standard distribution boots `kernel7` on an RPi3, or `kernel7l` for RPi4.

Configure which kernel image to build based on your target device.

#### For Raspberry Pi 3

```
$ cd ~/dev/linux
$ KERNEL=kernel7
$ make bcm2709_defconfig
```

Edit `.config` to label your custom kernel and enable XDP features.

```
CONFIG_LOCALVERSION="-v7-xdp"
CONFIG_XDP_SOCKETS=y
CONFIG_XDP_SOCKETS_DIAG=y
```

#### For Raspberry Pi 4

```
$ cd ~/dev/linux
$ KERNEL=kernel7l
$ make bcm2711_defconfig
```

Edit `.config` to label your custom kernel and enable XDP features.

```
CONFIG_LOCALVERSION="-v7l-xdp"
CONFIG_XDP_SOCKETS=y
CONFIG_XDP_SOCKETS_DIAG=y
```

### Compile and install custom kernel

Launch kernel compile using all 4 cores (prepare to wait a while...)

```
$ make -j4 zImage modules dtbs
$ make headers_install
$ sudo make modules_install
$ sudo cp arch/arm/boot/dts/*.dtb /boot/
$ sudo cp arch/arm/boot/dts/overlays/*.dtb* /boot/overlays/
$ sudo cp arch/arm/boot/dts/overlays/README /boot/overlays/
```

We install the kernel image with a custom name, so it won't be overwritten but the package manager.

#### Install RPi3 kernel image

```
$ sudo cp arch/arm/boot/zImage /boot/kernel7-xdp.img
```

Edit `/boot/config.txt` to specify boot kernel `kernel7-xdp.img`

```
# specify kernel to boot (Pi3 default is kernel7.img)
#kernel=kernel7.img
kernel=kernel7-xdp.img
```

#### Install RPi4 kernel image

```
$ sudo cp arch/arm/boot/zImage /boot/kernel7l-xdp.img
```

Edit `/boot/config.txt` to specify boot kernel `kernel7l-xdp.img`

```
# specify kernel to boot (Pi4 default is kernel7l.img)
#kernel=kernel7l.img
kernel=kernel7l-xdp.img
```

### Reboot to run newly-build kernel

```
$ sudo shutdown -r now
```

Verify that you're running the new custom kernel

```
$ uname -a
Linux raspberrypi 5.4.65-v7l-xdp+ #1 SMP Wed Sep 23 13:24:39 MDT 2020 armv7l GNU/Linux
```

## Build eBPF support tools

```
$ cd ~/dev/linux/tools/lib/bpf
$ make
$ sudo make install
$ sudo make install_headers
```

```
$ cd ~/dev/linux/tools/bpf/bpftool
$ make
$ sudo make install
```

```
$ cd ~/dev/linux/samples/bpf
$ make
```

It feels like the wrong thing to do, but this resolves many dependencies...

```
$ sudo ln -s /usr/include/asm-generic /usr/include/asm
```

## Build eBPF experiments

```
$ cd ~/dev
$ git clone https://github.com/dalnefre/eBPF.git
$ cd eBPF
```

### Build [protocol lab](proto/README.md)

```
$ cd ~/dev/eBPF/proto
$ make clean all test
```


### Build [XDP experiments](XDP/README.md)

```
$ cd ~/dev/eBPF/XDP
$ make clean all test
```
