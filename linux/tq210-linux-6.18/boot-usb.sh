#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# USB transfers only: UART stays under the operator's control.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk=$(CDPATH= cd -- "$here/../.." && pwd)
linux="$sdk/linux"
openboot="$sdk/openboot"
variant=adb
if [ "${1:-}" = --basic ]; then
    variant=basic
    shift
fi
if [ "$variant" = basic ]; then
    boot="$linux/build-tq210-6.18/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210.dtb"
else
    boot="$linux/build-tq210-adb/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-adb.dtb"
fi
ramdisk="$linux/build-rootfs-tq210/rootfs.uimg"
kernel="$boot/zImage"
uboot="$sdk/u-boot/mainline/build-tq210/u-boot.bin"
dnw="$openboot/build-host/tq210-dnw"
loader="$openboot/out/rom_usb_dma_fast.bin"
action=${1:-all}
[ "$#" -le 1 ] || { echo "Usage: $0 [--basic] [all|uboot|dtb|rootfs|kernel]" >&2; exit 2; }

check_file() {
    [ -s "$1" ] || { echo "Missing image: $1" >&2; exit 1; }
    size=$(wc -c < "$1")
    [ "$size" -le "$2" ] || { echo "Image too large: $1" >&2; exit 1; }
}
check_uboot() {
    [ -x "$dnw" ] || { echo "Missing executable: $dnw" >&2; exit 1; }
    check_file "$loader" 16384
    check_file "$uboot" 16777216
    [ $(( $(wc -c < "$uboot") % 4 )) -eq 0 ] || {
        echo "U-Boot must be 4-byte aligned" >&2; exit 1;
    }
}
check_images() {
    check_file "$dtb" 1048576
    check_file "$ramdisk" 16777216
    check_file "$kernel" 67108864
}
root_run() {
    if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
}
pause() {
    printf '%s' "$1"
    IFS= read -r answer
}
load_uboot() {
    check_uboot
    echo "Board must be in Samsung iROM USB download mode (04e8:1234)."
    root_run "$dnw" --dma-boot "$loader" "$uboot"
}
stage() {
    path=$1
    addr=$2
    max_hex=$3
    max_bytes=$4
    check_file "$path" "$max_bytes"
    printf '\nUART (U-Boot): fastboot -l %s -s %s usb 0\n' "$addr" "$max_hex"
    pause "After entering that exact command on UART, press Enter here: "
    # The host Fastboot protocol does not convey a RAM address. The UART
    # command above sets the device's buffer; getvar/version only checks
    # communication, not the selected address.
    root_run timeout 20 fastboot -s TQ210-TEST getvar version
    root_run timeout 120 fastboot -s TQ210-TEST stage "$path"
    printf 'Downloaded %s bytes to requested slot 0x%s (UART address not independently verified).\n' "$(wc -c < "$path")" "$addr"
    echo "Press Ctrl+C on UART to return to U-Boot before the next command."
}
stage_dtb() { stage "$dtb" 2a000000 100000 1048576; }
stage_rootfs() { stage "$ramdisk" 2c000000 1000000 16777216; }
stage_kernel() { stage "$kernel" 26000000 4000000 67108864; }
boot_cmd() {
    cat <<'EOF'

UART, after Ctrl+C:
    fdt addr 2a000000
    fdt header
    md.b 2c000000 4
    setenv bootargs 'console=ttySAC0,115200n8 earlycon ignore_loglevel clk_ignore_unused rdinit=/init'
    bootz 26000000 2c000000 2a000000
EOF
}
case "$action" in
    all)
        check_uboot
        check_images
        pause "Power-cycle into iROM USB mode, then press Enter: "
        load_uboot
        stage_dtb
        stage_rootfs
        stage_kernel
        boot_cmd
        ;;
    uboot) load_uboot ;;
    dtb) stage_dtb ;;
    rootfs) stage_rootfs ;;
    kernel) stage_kernel; boot_cmd ;;
    *) echo "Usage: $0 [--basic] [all|uboot|dtb|rootfs|kernel]" >&2; exit 2 ;;
esac
