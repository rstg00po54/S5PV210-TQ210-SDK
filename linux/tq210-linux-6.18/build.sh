#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-"$here/../kernel-6.18"}
src=$(CDPATH= cd -- "$src" && pwd)
out=${OUT:-"$here/../build-tq210-6.18"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
export CC=${CC:-"${CROSS_COMPILE}gcc-10"}

[ -f "$src/Makefile" ] || { echo "Missing Linux source: $src" >&2; exit 1; }
[ "$(make -s -C "$src" kernelversion)" = "6.18.0" ] || {
    echo "Expected Linux v6.18 (6.18.0)" >&2; exit 1;
}
[ -f "$src/arch/arm/configs/tq210_defconfig" ] || {
    echo "Missing TQ210 board configuration; use the SDK kernel tree" >&2; exit 1;
}
make -C "$src" O="$out" CC="$CC" tq210_defconfig
for opt in ARCH_S5PV210 CPU_V7 ARM_VIC CLKSRC_SAMSUNG_PWM COMMON_CLK_SAMSUNG SERIAL_SAMSUNG SERIAL_SAMSUNG_CONSOLE SERIAL_EARLYCON DEBUG_LL DEBUG_S3C_UART0 BLK_DEV_INITRD; do
    grep -qx "CONFIG_${opt}=y" "$out/.config" || {
        echo "Required config missing: $opt" >&2; exit 1;
    }
done
make -C "$src" O="$out" CC="$CC" -j"${JOBS:-$(nproc)}" zImage samsung/s5pv210-tq210.dtb
ls -lh "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210.dtb"
