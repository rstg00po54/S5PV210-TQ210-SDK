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
as a Linux DT. This native board port has not been cross-compiled or booted.
