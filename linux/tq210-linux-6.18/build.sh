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
    echo "Expected unmodified Linux v6.18 (6.18.0)" >&2
    exit 1
}
dt="$src/arch/arm/boot/dts/samsung"
[ -f "$dt/s5pv210.dtsi" ] || { echo "Missing upstream S5PV210 DT" >&2; exit 1; }
cp "$here/s5pv210-tq210.dts" "$dt/s5pv210-tq210.dts"
if ! grep -q 's5pv210-tq210\.dtb' "$dt/Makefile"; then
    printf '\n# EmbedSky TQ210 RAM bring-up\ndtb-$(CONFIG_ARCH_S5PV210) += s5pv210-tq210.dtb\n' >> "$dt/Makefile"
fi

make -C "$src" O="$out" CC="$CC" s5pv210_defconfig
cfg="$src/scripts/config"
"$cfg" --file "$out/.config" \
    --enable ARCH_S5PV210 --enable VMSPLIT_2G \
    --enable DEBUG_KERNEL --enable DEBUG_LL --enable DEBUG_S3C_UART0 \
    --enable SERIAL_SAMSUNG --enable SERIAL_SAMSUNG_CONSOLE \
    --enable BLK_DEV_INITRD --enable DEVTMPFS --enable DEVTMPFS_MOUNT \
    --enable BINFMT_ELF --enable BINFMT_SCRIPT --enable TMPFS \
    --enable PROC_FS --enable SYSFS --enable VFP --enable NEON \
    --enable PRINTK --enable DEBUG_INFO_NONE \
    --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT --disable DEBUG_INFO_BTF \
    --disable ARM_APPENDED_DTB --disable ARM_LPAE --disable SMP \
    --disable CPU_FREQ --disable CPU_IDLE --disable MODULES \
    --disable USB --disable MMC --disable MTD --disable SCSI \
    --disable DRM --disable MEDIA_SUPPORT --disable SOUND \
    --disable BT --disable WLAN --disable I2C --disable SPI \
    --disable DMADEVICES --disable NET --disable PM \
    --disable DEBUG_SPINLOCK --disable DEBUG_MUTEXES \
    --disable DEBUG_RT_MUTEXES --disable DEBUG_ATOMIC_SLEEP \
    --set-str CMDLINE "" --disable CMDLINE_FORCE --disable CMDLINE_EXTEND \
    --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS "" \
    --set-str LOCALVERSION "-tq210" --disable LOCALVERSION_AUTO
make -C "$src" O="$out" CC="$CC" olddefconfig
for opt in ARCH_S5PV210 CPU_V7 ARM_VIC CLKSRC_SAMSUNG_PWM COMMON_CLK_SAMSUNG SERIAL_SAMSUNG SERIAL_SAMSUNG_CONSOLE SERIAL_EARLYCON DEBUG_LL DEBUG_S3C_UART0 BLK_DEV_INITRD; do
    grep -qx "CONFIG_${opt}=y" "$out/.config" || {
        echo "Required config missing: $opt" >&2; exit 1;
    }
done
make -C "$src" O="$out" CC="$CC" -j"${JOBS:-$(nproc)}" zImage dtbs
ls -lh "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/samsung/s5pv210-tq210.dtb"
