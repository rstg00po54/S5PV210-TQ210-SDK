# TQ210 / Linux v6.18 RAM bring-up

The SDK contains the old vendor kernels separately. This directory is the
tracked board overlay and build helper, not the full Linux source.
Upstream v6.18 has ARCH_S5PV210, Samsung clocks, PL192 VIC, Samsung
PWM clocksource and UART, and s5pv210_defconfig. Do not copy the old
vendor platform implementation into this kernel.

From the SDK root, obtain the official v6.18 tarball (or use an existing
clean v6.18 checkout with the build script's source-path argument):

    cd linux
    curl -fL --retry 3 -o /tmp/linux-6.18.tar.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.tar.xz
    tar -xf /tmp/linux-6.18.tar.xz
    mv linux-6.18 kernel-6.18
    cd ..

If importing into the SDK's single main branch, stage and commit the
clean source before building, excluding build products and any nested
.git directory. This large import is not performed by the overlay.
Install dependencies on Ubuntu 20.04:

    sudo apt install build-essential gcc-10-arm-linux-gnueabihf binutils-arm-linux-gnueabihf bison flex bc libssl-dev libelf-dev xz-utils curl
    sh linux/tq210-linux-6.18/build.sh

Outputs under linux/build-tq210-6.18/arch/arm/boot:
zImage and dts/samsung/s5pv210-tq210.dtb. The upstream minimum GCC is
8.1 and binutils 2.30; installed GCC 10 / binutils 2.34 meet it.

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
as a Linux DT. This overlay has not been cross-compiled or booted.
