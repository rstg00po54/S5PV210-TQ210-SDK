#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src="$here/../kernel-6.18"
out="$here/../build-tq210-nand"
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
cc=${CC:-"${CROSS_COMPILE}gcc-10"}
make -C "$src" O="$out" CC="$cc" tq210_nand_defconfig
for opt in USB_GADGET USB_DWC2 USB_DWC2_PERIPHERAL PHY_SAMSUNG_USB2 PHY_S5PV210_USB2 CONFIGFS_FS USB_CONFIGFS USB_CONFIGFS_F_FS USB_CONFIGFS_ACM BLK_DEV_INITRD UNIX98_PTYS MTD MTD_RAW_NAND MTD_NAND_TQ210_RO MTD_UBI MTD_UBI_FASTMAP UBIFS_FS MTD_OF_PARTS; do
    grep -qx "CONFIG_${opt}=y" "$out/.config" || {
        echo "Required config missing: $opt" >&2; exit 1;
    }
done
make -C "$src" O="$out" CC="$cc" -j"${JOBS:-$(nproc)}" zImage samsung/s5pv210-tq210-nand.dtb samsung/s5pv210-tq210-nand-ecc.dtb samsung/s5pv210-tq210-nand-rwtest.dtb samsung/s5pv210-tq210-nand-bch.dtb samsung/s5pv210-tq210-nand-ubi.dtb samsung/s5pv210-tq210-nand-bl1.dtb samsung/s5pv210-tq210-nand-bl2.dtb
ls -lh "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-ecc.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-rwtest.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-bch.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-ubi.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-bl1.dtb" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210-nand-bl2.dtb"
