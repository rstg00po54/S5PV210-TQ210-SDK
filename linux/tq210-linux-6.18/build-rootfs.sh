#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
linux=$(CDPATH= cd -- "$here/.." && pwd)
sdk=$(CDPATH= cd -- "$linux/.." && pwd)
kernel="$linux/kernel-6.18"
kbuild="$linux/build-tq210-6.18"
out="$linux/build-rootfs-tq210"
version=1.36.1
tarball="$out/busybox-$version.tar.bz2"
bb="$out/busybox-$version"
cross=${CROSS_COMPILE:-arm-linux-gnueabihf-}
cc=${CC:-"${cross}gcc-10"}
mkdir -p "$out"
for tool in "$cc" "${cross}readelf" curl sha256sum tar gzip make; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
gen="$kbuild/usr/gen_init_cpio"
[ -x "$gen" ] || { echo "Build the Linux kernel first (missing $gen)" >&2; exit 1; }
grep -qx CONFIG_BLK_DEV_INITRD=y "$kbuild/.config"
grep -qx CONFIG_RD_GZIP=y "$kbuild/.config"
mkimage="$sdk/u-boot/mainline/build-tq210/tools/mkimage"
if [ ! -x "$mkimage" ]; then
    mkimage=$(command -v mkimage) || { echo "Install u-boot-tools or build U-Boot" >&2; exit 1; }
fi

if [ ! -f "$tarball" ]; then
    curl -fL --retry 3 -o "$tarball.part" "https://busybox.net/downloads/busybox-$version.tar.bz2"
    mv "$tarball.part" "$tarball"
fi
printf '%s  %s\n' b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314 "$tarball" | sha256sum -c -
[ -d "$bb" ] || tar -xjf "$tarball" -C "$out"

# Start with a minimal config, not a networking/desktop distribution.
make -C "$bb" ARCH=arm CROSS_COMPILE="$cross" CC="$cc" allnoconfig
cfg="$kernel/scripts/config"
applets="sh cat ls cp mv rm mkdir rmdir ln chmod chown touch echo printf pwd basename dirname head tail wc sort uniq grep sed cut tr find xargs tee dd sync df du free ps top uname uptime dmesg mount umount hostname sleep setsid cttyhack stty clear id date hexdump od true false"
# sh is a shell choice, the other applet names match BusyBox Kconfig.
for opt in STATIC BUSYBOX LFS SHOW_USAGE FEATURE_VERBOSE_USAGE ASH SH_IS_ASH ASH_JOB_CONTROL FEATURE_EDITING FEATURE_EDITING_HISTORY; do
    "$cfg" --file "$bb/.config" --enable "$opt"
done
"$cfg" --file "$bb/.config" --disable SH_IS_NONE --disable SH_IS_HUSH
for applet in $applets; do
    [ "$applet" = sh ] && continue
    opt=$(printf '%s' "$applet" | tr '[:lower:]' '[:upper:]')
    "$cfg" --file "$bb/.config" --enable "$opt"
done
# Resolve Kconfig dependencies. Oldconfig accepts defaults for any new choices.
yes '' | make -C "$bb" ARCH=arm CROSS_COMPILE="$cross" CC="$cc" oldconfig
for opt in STATIC ASH SH_IS_ASH MOUNT UMOUNT SETSID CTTYHACK HOSTNAME UNAME SLEEP LS FREE PS TOP; do
    grep -qx "CONFIG_${opt}=y" "$bb/.config" || {
        echo "BusyBox required config missing: $opt" >&2; exit 1;
    }
done
make -C "$bb" ARCH=arm CROSS_COMPILE="$cross" CC="$cc" -j"${JOBS:-$(nproc)}"
LC_ALL=C "${cross}readelf" -h "$bb/busybox" | grep -E 'Class:|Machine:'
if LC_ALL=C "${cross}readelf" -l "$bb/busybox" | grep -q INTERP; then
    echo "BusyBox is dynamically linked, refusing to package" >&2
    exit 1
fi
LC_ALL=C "${cross}readelf" -h "$bb/busybox" | grep -q 'Machine:.*ARM'

# Use the kernel host tool to create root-owned device nodes without sudo.
# Paths in gen_init_cpio manifests must not contain whitespace.
case "$out:$here" in *[[:space:]]*) echo "SDK path must not contain whitespace" >&2; exit 1;; esac
manifest="$out/initramfs.list"
cat > "$manifest" <<EOF
dir /bin 0755 0 0
dir /sbin 0755 0 0
dir /usr 0755 0 0
dir /usr/bin 0755 0 0
dir /usr/sbin 0755 0 0
dir /dev 0755 0 0
dir /proc 0555 0 0
dir /sys 0555 0 0
dir /tmp 1777 0 0
dir /etc 0755 0 0
dir /root 0700 0 0
dir /run 0755 0 0
dir /mnt 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/null 0666 0 0 c 1 3
nod /dev/tty 0666 0 0 c 5 0
file /bin/busybox $bb/busybox 0755 0 0
file /init $here/init 0755 0 0
EOF
for applet in $applets; do
    printf 'slink /bin/%s busybox 0777 0 0\n' "$applet" >> "$manifest"
done
printf 'root:x:0:0:root:/root:/bin/sh\n' > "$out/passwd"
printf 'root:x:0:\n' > "$out/group"
printf 'file /etc/passwd %s/passwd 0644 0 0\nfile /etc/group %s/group 0644 0 0\n' "$out" "$out" >> "$manifest"
"$gen" "$manifest" > "$out/rootfs.cpio"
gzip -n -9 -c "$out/rootfs.cpio" > "$out/rootfs.cpio.gz"
"$mkimage" -A arm -O linux -T ramdisk -C gzip -n "TQ210 BusyBox $version" -d "$out/rootfs.cpio.gz" "$out/rootfs.uimg"
# Keep the image below its 16 MiB planned RAM slot.
[ "$(wc -c < "$out/rootfs.uimg")" -le 16777216 ] || { echo "rootfs too large" >&2; exit 1; }
ls -lh "$bb/busybox" "$out/rootfs.cpio.gz" "$out/rootfs.uimg"
sha256sum "$out/rootfs.uimg"
