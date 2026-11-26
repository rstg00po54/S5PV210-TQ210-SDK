#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src="$here/../kernel-6.18"
out="$here/../build-tq210-adb"
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
cc=${CC:-"${CROSS_COMPILE}gcc-10"}
make -C "$src" O="$out" CC="$cc" tq210_adb_defconfig
for opt in USB_GADGET USB_DWC2 USB_DWC2_PERIPHERAL PHY_SAMSUNG_USB2 PHY_S5PV210_USB2 CONFIGFS_FS USB_CONFIGFS USB_CONFIGFS_F_FS USB_CONFIGFS_ACM BLK_DEV_INITRD UNIX98_PTYS; do
    grep -qx "CONFIG_${opt}=y" "$out/.config" || {
        echo "Required config missing: $opt" >&2; exit 1;
    }
done
make -C "$src" O="$out" CC="$cc" -j"${JOBS:-$(nproc)}" zImage samsung/s5pv210-tq210-adb.dtb
ls -lh "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-adb.dtb"
