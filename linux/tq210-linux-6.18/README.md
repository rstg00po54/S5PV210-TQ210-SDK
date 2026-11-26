# TQ210 / Linux v6.18 RAM bring-up

The full upstream v6.18 source is tracked in linux/kernel-6.18, baseline
commit 08cc4920743481f83c410f4a1685dc2ee6c353c6. The old vendor kernels
remain untouched. The native board files are now in the kernel tree:
arch/arm/configs/tq210_defconfig and
arch/arm/boot/dts/samsung/s5pv210-tq210.dts.
The helper no longer modifies source files.

From the SDK root:

    sudo apt install build-essential gcc-10-arm-linux-gnueabihf binutils-arm-linux-gnueabihf bison flex bc libssl-dev libelf-dev
    sh linux/tq210-linux-6.18/build.sh

Outputs under linux/build-tq210-6.18/arch/arm/boot:
zImage and dts/samsung/s5pv210-tq210.dtb. To preserve a manually changed
.config, use direct Kbuild instead of rerunning the helper (which resets
to tq210_defconfig). No architecture source directories removed yet.
Compile and boot the unpruned kernel first.

## First boot, no root filesystem

At the existing U-Boot shell, optionally read the clock input strap:

    md.l e010e100 1

OM_STAT bit0 selects xxti (0) or xusbxti (1) for fin_pll. Both DT
input rates are initially set to 24 MHz, consistent with the inherited
clock assumption; physical oscillator wiring and absolute rate are
not yet independently verified. CPU frequency scaling and CPU idle are
disabled to reduce early bring-up variables. clk_ignore_unused prevents
the late unused-clock sweep from gating clocks we have not audited.

First send DTB to staging RAM, then exit Fastboot with UART Ctrl-C
and copy DTB to its separate address before downloading zImage:

    # UART
    fastboot usb 0
    # Ubuntu, SDK root (unbind visor if still using old 04e8:6601)
    sudo fastboot stage linux/build-tq210-6.18/arch/arm/boot/dts/samsung/s5pv210-tq210.dtb
    # UART Ctrl-C
    cp.b 26000000 2a000000 ${filesize}
    fdt addr 2a000000
    fdt header
    fastboot usb 0
    # Ubuntu
    sudo fastboot stage linux/build-tq210-6.18/arch/arm/boot/zImage
    # UART Ctrl-C
    setenv bootargs 'console=ttySAC0,115200n8 earlycon ignore_loglevel clk_ignore_unused'
    bootz 26000000 - 2a000000

The new private-test 1209:0001 USB identity may require fastboot -i
0x1209 with older host clients. No Flash commands. DTB at 0x2a000000 is
outside staging RAM [0x26000000,0x2a000000). U-Boot is relocated near
the top of DDR. Linux normally decompresses near 0x20008000.

First objective is early console and successful kernel initialization.
Without initramfs or rootfs, a final VFS panic is expected and does not
by itself imply a platform failure. Do not use the U-Boot control DT
as a Linux DT. The first kernel boot has been validated below.

## First hardware boot (2026-11-26)

Native board port d271f60 built with GCC 10.5.0 and binutils 2.34.
zImage: 2035056 bytes; DTB: 23224 bytes. Fastboot RAM downloads and
bootz succeeded. Linux 6.18.0-tq210 reached root filesystem mounting.
All four PL192 VICs detected; Samsung clocksource selected; UART0
transitioned from earlycon to ttySAC0. The whole 1 GiB DDR range was
recognized (1031916 KiB available), VFP and devtmpfs initialized.
Reported PLL rates: APLL 800 MHz, MPLL 667 MHz. Final VFS panic
unknown-block(0,0) was expected with no root filesystem or initramfs.
Unresolved diagnostics: pinctrl wakeup device link, cache hierarchy
reporting, Cortex-A8 Spectre v2 mitigation warning. No claim of full
peripheral validation or measured oscillator accuracy. Next: RAM-only
initramfs with an ARM userspace and /init.

## BusyBox initramfs (RAM-only)

From linux/ (after the successful kernel build):

    sudo apt install curl bzip2 gzip libc6-dev-armhf-cross
    sh tq210-linux-6.18/build-rootfs.sh

The helper downloads the pinned BusyBox 1.36.1 source and checks its
SHA-256, cross-compiles a static ARM executable using GCC 10, and builds
root-owned newc cpio with /init and /dev/console without root privileges.
Output: linux/build-rootfs-tq210/rootfs.cpio.gz and rootfs.uimg.
The latter is a legacy U-Boot RAMDisk image (gzip-compressed cpio payload),
so raw-initrd support is not required. No kernel rebuild needed if
CONFIG_BLK_DEV_INITRD and CONFIG_RD_GZIP remain enabled. This is a lab-only
root shell, no network, no login/password, no persistence. BusyBox source
and build outputs are ignored. Source build and hardware boot still need
validation on the target host.

Cold boot U-Boot through OpenBoot as before. With the corrected USB ID,
use sudo fastboot on the host, no visor unbind. Each download must finish
with OKAY, then exit Fastboot on UART with Ctrl-C before copying.

    # UART
    fastboot usb 0
    # Ubuntu, from linux/
    sudo fastboot stage build-tq210-6.18/arch/arm/boot/dts/samsung/s5pv210-tq210.dtb
    # UART Ctrl-C
    cp.b 26000000 2a000000 ${filesize}
    fdt addr 2a000000
    fdt header
    fastboot usb 0
    # Ubuntu
    sudo fastboot stage build-rootfs-tq210/rootfs.uimg
    # UART Ctrl-C
    cp.b 26000000 2c000000 ${filesize}
    md.b 2c000000 4
    fastboot usb 0
    # Ubuntu
    sudo fastboot stage build-tq210-6.18/arch/arm/boot/zImage
    # UART Ctrl-C
    setenv bootargs 'console=ttySAC0,115200n8 earlycon ignore_loglevel clk_ignore_unused rdinit=/init'
    bootz 26000000 2c000000 2a000000

The expected ramdisk magic at 0x2c000000 is 27 05 19 56. U-Boot validates
and relocates the images. No storage writes. Test the resulting shell:

    uname -a
    cat /proc/cpuinfo
    free -m
    cat /proc/interrupts
    cat /proc/uptime
    ls /dev
    ps
    mount

If PID1 or mount errors appear, capture the full serial boot log.
