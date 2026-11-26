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

## First BusyBox hardware boot (2026-11-26)

Build of 469fb56 succeeded: static ARM ELF32 BusyBox 1.36.1 (670 KiB),
gzip cpio payload 431582 bytes, legacy ramdisk 431646 bytes.
New USB identity U-Boot built at 14:30:26 successfully downloaded all
three images. bootz 26000000 2c000000 2a000000 verified the ramdisk
checksum and relocated it to 0x5eb4a000. Linux unpacked initramfs,
freed its 424 KiB input buffer, ran /init, and printed the BusyBox
ash interactive shell prompt. User executed ls in empty /root.
No kernel panic or mount failure shown. The literal prompt \\w is
because ASH_EXPAND_PRMT was disabled; enable it in the next rootfs
build, along with interactive top and CPU percentages. This is a
userspace-only change and needs no kernel or U-Boot rebuild. Runtime
stress, actual clock accuracy and peripherals remain unverified.

### Basic userspace smoke test

On the first BusyBox image, uname, free, /proc/cpuinfo, /proc/interrupts,
/proc/uptime, ls, ps and mount all returned successfully. /proc/cpuinfo
reported Cortex-A8 part 0xc08 revision 2 and VFPv3/NEON feature bits.
Memory snapshot (KiB): total 1034660, used 7040, free 1026668,
available 1020676. The minimal free applet printed KiB despite -m.
VIC timer IRQ 40 count 489, UART IRQ 58 count 464, Err 0;
uptime 145.10 s (not independently timed). PID 1 /bin/sh /init and
interactive ash were running. rootfs, devtmpfs, proc, sysfs and tmpfs
were mounted. gpiochip0 through gpiochip33 registered; electrical GPIO
operation and pinctrl IRQs are not tested. VFP/NEON execution, sustained
memory/scheduler stress, clock accuracy and external peripherals remain
unvalidated. No storage or network enabled.

## Experimental USB gadget / ADB bring-up

The known-good minimal kernel remains unchanged. Build the independent
USB kernel and its DTB from linux/: 

    sh tq210-linux-6.18/build-adb-kernel.sh
    sh tq210-linux-6.18/build-rootfs.sh

Boot with the existing three-image RAM procedure, substituting
build-tq210-adb/arch/arm/boot/zImage and
build-tq210-adb/arch/arm/boot/dts/samsung/s5pv210-tq210-adb.dtb.
At the UART Linux shell inspect ls /sys/class/udc and dmesg, then run
/sbin/tq210-usb acm. On Ubuntu check lsusb, lsusb -t, and /dev/ttyACM*.
The ACM function is only a USB transport smoke test, not ADB. Stop it
with /sbin/tq210-usb stop before changing function.

For actual ADB a separately built, trusted static ARMv7 Linux adbd with
FunctionFS support is required. Buildroot's android-tools package or a
Linux standalone port can supply it, but ARM32 compatibility and static
linkage are not verified yet. Once available, use
ADBD_BIN=/absolute/path/to/adbd sh tq210-linux-6.18/build-rootfs.sh
and reboot with the USB kernel and new rootfs. Manually run
/sbin/tq210-usb adb, then use adb devices / adb shell from the host.
No daemon starts automatically. Root shell over USB is intended only
for a trusted isolated test cable; no TCP listener should be enabled.
1209:0001 is a non-unique private-test VID/PID, not for distribution.
Both USB kernel and userspace integration are untested on hardware.


## One-command RAM boot

Close the UART terminal first (only one program may own the UART).
On Ubuntu, from linux/:

    sudo apt install python3-serial
    ls /dev/ttyUSB* /dev/ttyACM*
    sudo /usr/bin/python3 tq210-linux-6.18/boot-linux.py /dev/ttyUSB0

Replace ttyUSB0 with the actual UART0 adapter. Power-cycle into iROM USB
download mode when prompted. The script checks image headers and sizes,
loads OpenBoot and the new-identity U-Boot, stages DTB/ramdisk/kernel
through Fastboot, checks CRC32 on the board, runs bootz, and releases UART
after the userspace banner. Reopen your normal serial terminal afterward.
Use --basic for the original non-USB kernel or --uboot-ready if the board
is already at the TQ210 U-Boot prompt (do not use while Linux is running).
Requires the previously built artifacts, corrected TQ210-TEST identity,
one target board, and a separate UART adapter that remains connected
during power cycles. No flash writes. Script has not been tested on
hardware. If a stage fails, inspect the printed logs and reset if needed.


## USB-only assisted boot (manual UART)

For operators who keep their own UART terminal open, use the USB-only
shell script instead of boot-linux.py. No pyserial or UART access.
From linux/:

    sh tq210-linux-6.18/boot-usb.sh all

Power-cycle into iROM when prompted. The script downloads U-Boot and
prints each exact UART Fastboot command. Enter it in the existing UART
terminal, then press Enter in the Ubuntu script to transfer the image.
After each transfer press Ctrl-C on UART. The final bootz command is
printed, not executed. For an already-running U-Boot, run the individual
dtb, rootfs and kernel actions; uboot is a separate cold-boot action.
Use --basic before the action for the original kernel, default is ADB
experimental kernel.

    sh tq210-linux-6.18/boot-usb.sh uboot
    sh tq210-linux-6.18/boot-usb.sh dtb
    sh tq210-linux-6.18/boot-usb.sh rootfs
    sh tq210-linux-6.18/boot-usb.sh kernel

U-Boot cmd/fastboot.c and drivers/fastboot/fb_common.c implement -l and
-s as hexadecimal buffer address and size. The host stage command does
not carry an address. DTB: 2a000000 / 100000; ramdisk: 2c000000 /
1000000; zImage: 26000000 / 4000000. The ranges do not overlap.
The script requires the updated TQ210-TEST serial and one test board.
It checks file sizes and Fastboot transfer success, but cannot verify the
UART-selected address. No cp.b, no serial ownership, no NAND writes.
Direct-address transfers and this host script need hardware validation.

### USB userspace smoke test and BusyBox fix

The ADB kernel booted and registered ec000000.usb in /sys/class/udc.
DWC2 reported 16 endpoints and 7808 SPRAM entries; dummy USB regulators,
unsupported besl and g_np_tx_fifo_size warnings remain to review.
The first gadget setup attempt failed because the minimal BusyBox image
had neither the shell test builtin nor the [ applet. Enable ASH_TEST,
TEST, TEST1, the [ symlink, and shell arithmetic for the ADB setup loop.
Also enable fancy prompt expansion. Only rebuild rootfs, not the kernel.
Manual configfs ACM commands can bypass test on the running image.
Actual USB enumeration remains pending.

## USB ACM verified; experimental USB-only adbd

Linux 6.18.0-tq210-adb registered ec000000.usb. Manual ConfigFS ACM
enumerated at high speed 480M, cdc_acm bound on Ubuntu and ttyACM0
appeared. Operator confirmed text transfer both directions between
ttyGS0 and ttyACM0. Direct-address Fastboot RAM boot also succeeded.

Build adbd and a new rootfs (no kernel rebuild):

    sh tq210-linux-6.18/build-adbd.sh

The source is a pinned snapshot of ajiang17/android-adbd-for-linux
ba0f0fd06acaa28d6d264818a1703e462b8bfbbc, fetched via HTTPS; the helper
does not independently verify an archive SHA256. It patches the
uninitialized property buffers, removes the TCP/host listener from
device startup and corrects bulk-in FD initialization. It uses GCC 10
and static glibc. This is legacy ADB, no RSA authentication and no
modern shell v2 or compression negotiation. Use only a trusted
private USB cable; any connected host can obtain root access. Do not
enable it automatically or expose to a network. No Android framework.

Boot the USB kernel and regenerated rootfs using boot-usb.sh.
Then on the board UART:

    /sbin/tq210-usb adb
    cat /tmp/adbd.log

On Ubuntu:

    sudo apt install adb
    sudo adb devices
    sudo adb -s TQ210-TEST shell uname -a
    sudo adb -s TQ210-TEST shell

For a basic file transfer test use a small file, push/pull and compare
on the host. No claim of modern-client compatibility until tested.
If endpoints do not appear, inspect adbd.log and dmesg. This source
port has not yet been cross-compiled or run on hardware.

### First ADB hardware validation (2026-11-26)

After the constant little-endian initializer fix in 7c79ad2, the operator
reported a successful ARM adbd/rootfs build and boot. Ubuntu unprivileged
adb devices listed TQ210-TEST as device. adb shell entered interactive
BusyBox ash with root@tq210:~#; pwd returned /root. FunctionFS ep0, ep1,
ep2 were present; ps listed /usr/bin/adbd (PID 62); adbd.log was empty.
This validates USB ADB enumeration, connection and interactive shell.
File sync, binary integrity, reconnect and sustained throughput are still
pending. The daemon remains a legacy unauthenticated USB-only lab tool.

### ADB file sync smoke test

Ubuntu unprivileged adb push ../CD-README.txt / transferred 715 bytes;
adb shell confirmed /CD-README.txt existed and was readable, and
adb pull /CD-README.txt transferred 715 bytes back. Relative paths
failed as expected when the file was outside the current directory.
Chinese text displayed garbled, likely source encoding mismatch;
byte-for-byte comparison has not yet been reported. Small-file push
and pull work; large transfers, integrity and reconnect remain pending.

### Cross-compiled application execution verified

Operator built a standalone C hello program with
arm-linux-gnueabihf-gcc-10 -static -O2 hello.c -o hello.
ADB pushed the 416268-byte executable to /tmp/hello, chmod +x succeeded,
and adb shell /tmp/hello printed Hello TQ210 Linux 6.18!.
This validates the basic cross-compile, transfer and execution workflow.
The reported 571.4 MB/s for the small transfer exceeds USB 2.0 physical
capacity and is not a valid throughput measurement. Sustained throughput,
large-file integrity and reconnect behavior remain untested.

## Unified USB RAM boot (experimental)

Rebuild U-Boot with tq210_defconfig; the default bootcmd is now tqboot,
after a 2-second UART interruption window. Ctrl-C in Fastboot also
returns to the shell without booting. Old manual commands and
boot-usb.sh remain available (interrupt autoboot first).
From linux/:

    sh tq210-linux-6.18/boot-fit.sh

It packages the existing zImage, DTB and raw gzip initramfs into a FIT
with SHA256 hashes, waits for iROM, downloads OpenBoot/U-Boot, waits
for the automatic Fastboot session, stages the FIT, and sends continue.
No UART access, no flash writes. --basic selects the original kernel;
--ready skips iROM/U-Boot if already in the automatic tqboot session.
The new session uses 0x40000000-0x4fffffff (256 MiB); zImage is loaded
at 0x26000000. The old manual Fastboot buffer stays at 0x26000000,
64 MiB. Explicit continue is required after a complete FIT download;
the board validates the FDT header and exact transfer length before
bootm after USB teardown. SHA256 hashes detect corruption, not
authenticate the image. adbd remains manually activated, as before.
This path has not yet been compiled or hardware-tested.

### Unified FIT boot hardware validation

Operator confirmed the new boot-fit.sh automatic USB RAM boot works on
TQ210 CoreB. No UART download commands are required. The original manual
boot path remains available. Maximum 256 MiB transfer, sustained transfer
speed, interrupted download recovery and corrupted-image rejection have
not yet been tested. NAND remains untouched by this boot procedure.

## Experimental read-only NAND kernel

Separate NAND + ADB config, built with:

    sh tq210-linux-6.18/build-nand-kernel.sh
    sh tq210-linux-6.18/boot-fit.sh --nand

The controller is at 0xb0e00000, clock CLK_NFCON (IP1 bit 28),
pinctrl MP03/MP06 function 2 and MP01-2 function 3, matching the
tested runtime/drivers/nand.c register sequence. Only CE0, 8-bit,
1 GiB / 2048+64 / 128 KiB erase geometry accepted. Modern NAND
exec_op API, standard ID/ONFI discovery and RAM-only bad block
scan; read-only MTD, NAND_ROM and operation whitelist rejecting
program/erase/data-out cycles. No ECC correction yet: raw data
may contain bit errors and must not be trusted as filesystem data.
No flash BBT, no UBI, no automatic mounts, no write testing.

Check on UART:
    dmesg | grep -i nand
    cat /proc/mtd
    ls /dev/mtd*
    dd if=/dev/mtd0ro of=/tmp/nand-page0.bin bs=2048 count=1
    hexdump -C /tmp/nand-page0.bin | head

The regular ADB kernel and old boot commands are unaffected.
This new driver has not been compiled or tested on the board yet.

### First Linux NAND probe validation (2026-11-26)

Operator booted Linux 6.18.0-tq210-nand, built with GCC 10.5.0.
NFCONF=01807772 NFCONT=00800007 NFSTAT=f0800b0d; clock=133400000 Hz.
NAND core identified manufacturer 01, device d3, AMD/Spansion S34ML08G1,
1024 MiB SLC, 128 KiB erase blocks, 2048-byte pages and 64-byte OOB.
MTD registered mtd0: 40000000 00020000 "tq210-nand-raw-ro", with
/dev/mtd0ro and /dev/mtd0 present. ECC NONE warnings are expected for
this diagnostic configuration. Probe and geometry passed; repeated
page reads, end-of-device samples, data integrity and ECC correction
remain to be validated. No write or erase testing authorized.

### NAND raw read samples

Operator read page 0 successfully via /dev/mtd0ro. The hexdump includes
little-endian ARM instructions (ea000013, e59ff014) and 12345678 at
offset 0x4c, consistent with existing bootloader contents (not yet
compared against a factory image). Four-page reads at pages 0,
262144 and 524284 all completed, exercising the start, second LUN
boundary and end of the address range. This is sampled access, not
a full-device integrity or anti-alias test. No ECC correction yet.
The throughput command could not run because BusyBox has no time
applet; use /proc/uptime before and after dd instead. No NAND writes.

### NAND bad-block and sequential read baseline

MTD sysfs: bad_blocks=2, bbt_blocks=0, flags=0x0, ecc_strength=0.
Matches the previous bare-metal bad-block candidate count, but block
indices have not been compared. A 16 MiB sequential raw read
(dd bs=131072 count=128 to /dev/null) completed with 128+0 records
in and out. /proc/uptime before/after: 306.80/318.09 seconds;
11.29 s elapsed, approximately 1.42 MiB/s including operator and
command overhead, not an isolated throughput measurement. No ECC
correction or full-device data integrity validation yet. No NAND writes.
