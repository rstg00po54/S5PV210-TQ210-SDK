# TQ210 RAM-only bring-up

Initial experimental U-Boot proper port for the tested 1 GiB CoreB.
Requires OpenBoot with the verified ROM clock and DDR configuration.
No SPL, no NAND/SD/USB storage drivers, no persistent environment.
The minimal control DT is for U-Boot, not a complete Linux DT.

From u-boot/mainline:

    make O=build-tq210 CROSS_COMPILE=arm-linux-gnueabihf- CC=arm-linux-gnueabihf-gcc-10 tq210_defconfig
    make O=build-tq210 CROSS_COMPILE=arm-linux-gnueabihf- CC=arm-linux-gnueabihf-gcc-10 -j$(nproc)
    arm-linux-gnueabihf-readelf -h build-tq210/u-boot

Expected entry: 0x24000000 (ARM mode). Use u-boot.bin (appended DTB).
If the binary is not a multiple of four bytes, pad a copy with zeros.
From openboot, after cold reset into USB ROM mode:

    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin ../u-boot/mainline/build-tq210/u-boot.bin

UART0: 115200 8N1. Capture complete serial output; USB GO ACK alone
does not prove execution. Do not write storage. This port is unbuilt
and untested at initial commit. The inherited S5P PWM timer's rate
and CPU identification must be confirmed on hardware. No clock-tree
or DDR reprogramming is intended. Data cache is disabled for bring-up.
