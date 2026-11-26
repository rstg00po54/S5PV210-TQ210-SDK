# TQ210 RAM-only bring-up

Initial experimental U-Boot proper port for the tested 1 GiB CoreB.
Requires OpenBoot with the verified ROM clock and DDR configuration.
No SPL, no NAND/SD/USB storage drivers, no persistent environment.
Experimental USB device Fastboot for RAM downloads only.
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
does not prove execution. Do not write storage. No clock-tree or DDR
reprogramming is intended. MMU and D-cache now enable after relocation;
this default-on boot path needs a fresh hardware test.

## Hardware validation (2026-11-26)

On commit db99431 (manual cache enable):
- U-Boot 2027.01-rc3 reached the UART0 shell, reported 1 GiB.
- tqinfo reported PWM clock 66700000 Hz, timer4 1042187 Hz; the
  counter advanced and sleep returned. Absolute timing accuracy was
  not measured against an independent clock.
- Before dcache on: SCTLR=00c5187a ACTLR=00000002 TTBR0=00000000.
- After dcache on: SCTLR=00c5187f ACTLR=00000002 TTBR0=5fff0019.
- 1 MiB scratch at 0x26000000 filled with 0x12345678 gave CRC32
  a0564f88 both with D-cache on and after flush and disable.

For the next build verify tqinfo immediately after boot, repeat the
scratch CRC test, and time sleep against an external reference. These
results do not validate all DDR, DMA cache coherency, or storage.

## USB device bring-up

The iROM/OpenBoot PHY and clock gate are preserved; the Goni PMIC/PHY
power sequence is not used. DWC2 DMA is enabled by the existing UDC.
The lab USB identifier matches the upstream Goni example (04e8:6601);
not a VID/PID assignment for a product. Fastboot flash/erase, storage,
and OEM shell execution are disabled.

First test, at the U-Boot UART shell:

    tqusb
    dcache off
    fastboot usb 0

On Linux host: lsusb, sudo fastboot devices, sudo fastboot getvar version.
If supported by the installed fastboot client, use stage FILE (not flash)
to download a test file into RAM. Ctrl-C on the UART to leave fastboot.
Compare CRC32 with the host, using filesize. Then cold boot and repeat
with cache enabled to test DMA cache coherency. Default RAM buffer:
0x26000000, 64 MiB. No flash commands. If core reset/quiesce fails,
cold power-cycle; do not continue using a potentially active DMA engine.

This USB implementation is not yet built or hardware validated.
