#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Single FIT USB RAM boot; never opens UART; --rwtest explicitly starts a kernel scratch NAND write test.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk=$(CDPATH= cd -- "$here/../.." && pwd)
linux="$sdk/linux"
openboot="$sdk/openboot"
variant=adb
ready=no
pack_only=no
for arg in "$@"; do
    case "$arg" in
        --basic) variant=basic ;;
        --nand) variant=nand ;;
        --ecc) variant=ecc ;;
        --rwtest) variant=rwtest ;;
        --bch-rw) variant=bch-rw ;;
        --ubi) variant=ubi ;;
        --nand-rootfs) variant=nand-rootfs ;;
        --ready) ready=yes ;;
        --pack-only) pack_only=yes ;;
        *) echo "Usage: $0 [--basic|--nand|--ecc|--rwtest|--bch-rw|--ubi|--nand-rootfs] [--ready|--pack-only]" >&2; exit 2 ;;
    esac
done
if [ "$variant" = basic ]; then
    boot="$linux/build-tq210-6.18/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210.dtb"
elif [ "$variant" = ubi ] || [ "$variant" = nand-rootfs ]; then
    boot="$linux/build-tq210-nand/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-nand-ubi.dtb"
elif [ "$variant" = bch-rw ]; then
    boot="$linux/build-tq210-nand/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-nand-bch.dtb"
elif [ "$variant" = rwtest ]; then
    boot="$linux/build-tq210-nand/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-nand-rwtest.dtb"
elif [ "$variant" = ecc ]; then
    boot="$linux/build-tq210-nand/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-nand-ecc.dtb"
elif [ "$variant" = nand ]; then
    boot="$linux/build-tq210-nand/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-nand.dtb"
else
    boot="$linux/build-tq210-adb/arch/arm/boot"
    dtb="$boot/dts/samsung/s5pv210-tq210-adb.dtb"
fi
kernel="$boot/zImage"
ramdisk="$linux/build-rootfs-tq210/rootfs.cpio.gz"
if [ "$variant" = nand-rootfs ]; then
    ramdisk="$linux/build-rootfs-tq210/rootfs-nand.cpio.gz"
fi
uboot="$sdk/u-boot/mainline/build-tq210/u-boot.bin"
dnw="$openboot/build-host/tq210-dnw"
loader="$openboot/out/rom_usb_dma_fast.bin"
mkimage="$sdk/u-boot/mainline/build-tq210/tools/mkimage"
[ -x "$mkimage" ] || mkimage=$(command -v mkimage)
tools=dtc
if [ "$pack_only" = no ]; then tools="$tools fastboot lsusb timeout"; fi
for tool in $tools; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
check_file() {
    [ -s "$1" ] || { echo "Missing image: $1" >&2; exit 1; }
    [ "$(wc -c < "$1")" -le "$2" ] || { echo "Image too large: $1" >&2; exit 1; }
}
check_file "$kernel" 67108864
check_file "$dtb" 1048576
check_file "$ramdisk" 251658240
if [ "$pack_only" = no ] && [ "$ready" = no ]; then
    [ -x "$dnw" ] || { echo "Missing $dnw" >&2; exit 1; }
    check_file "$loader" 16384
    check_file "$uboot" 16777216
    [ $(( $(wc -c < "$uboot") % 4 )) -eq 0 ] || { echo "U-Boot not 4-byte aligned" >&2; exit 1; }
fi
out="$linux/build-fit-tq210/$variant"
mkdir -p "$out"
ln -sf "$kernel" "$out/zImage"
ln -sf "$dtb" "$out/board.dtb"
ln -sf "$ramdisk" "$out/rootfs.cpio.gz"
cat > "$out/tq210.its" <<'EOF'
/dts-v1/;
/ {
    description = "TQ210 Linux RAM boot";
    #address-cells = <1>;
    images {
        kernel {
            description = "Linux ARM zImage";
            data = /incbin/("zImage");
            type = "kernel";
            arch = "arm";
            os = "linux";
            compression = "none";
            load = <0x26000000>;
            entry = <0x26000000>;
            hash-1 { algo = "sha256"; };
        };
        fdt {
            description = "TQ210 device tree";
            data = /incbin/("board.dtb");
            type = "flat_dt";
            arch = "arm";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
        ramdisk {
            description = "gzip initramfs";
            data = /incbin/("rootfs.cpio.gz");
            type = "ramdisk";
            arch = "arm";
            os = "linux";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };
    configurations {
        default = "conf";
        conf {
            description = "TQ210 Linux";
            kernel = "kernel";
            fdt = "fdt";
            ramdisk = "ramdisk";
        };
    };
};
EOF
(cd "$out"; "$mkimage" -f tq210.its tq210.itb)
fit="$out/tq210.itb"
check_file "$fit" 268435456
if [ "$pack_only" = yes ]; then
    echo "FIT packaged: $fit"
    sha256sum "$fit"
    exit 0
fi
root_run() {
    if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
}
if [ "$(id -u)" -ne 0 ]; then sudo -v; fi
if [ "$ready" = no ]; then
    echo "Power-cycle into Samsung iROM USB mode."
    echo "For NAND-first U-Boot, interrupt autoboot on UART and enter tqboot for USB rescue."
    i=0
    while :; do
        n=$(lsusb -d 04e8:1234 | wc -l)
        [ "$n" -le 1 ] || { echo "Multiple ROM devices found" >&2; exit 1; }
        [ "$n" -eq 0 ] || break
        i=$((i+1))
        [ "$i" -lt 120 ] || { echo "ROM USB timeout" >&2; exit 1; }
        sleep 1
    done
    (cd "$openboot"; root_run timeout 120 "$dnw" --dma-boot "$loader" "$uboot")
fi
echo "Waiting for TQ210 Fastboot (if NAND-first autoboot is enabled, interrupt and enter tqboot)..."
i=0
while :; do
    n=$(root_run timeout 5 fastboot devices 2>/dev/null | awk '$1=="TQ210-TEST" && $2=="fastboot" {n++} END {print n+0}')
    [ "$n" -le 1 ] || { echo "Multiple TQ210 boards found" >&2; exit 1; }
    [ "$n" -eq 0 ] || break
    i=$((i+1))
    [ "$i" -lt 60 ] || { echo "Fastboot timeout; inspect UART" >&2; exit 1; }
    sleep 1
done
# Ensure this is the new tqboot session, not the old 64 MiB manual session.
info=$(root_run timeout 15 fastboot -s TQ210-TEST getvar max-download-size 2>&1)
printf '%s\n' "$info"
printf '%s\n' "$info" | grep -qi 'max-download-size: *0x10000000' || {
    echo "Expected 256 MiB tqboot session; do not stage into old session" >&2; exit 1;
}
root_run timeout 300 fastboot -s TQ210-TEST stage "$fit"
root_run timeout 30 fastboot -s TQ210-TEST continue
echo "FIT sent; boot requested. Inspect UART for kernel/userspace confirmation."
