#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Update inactive NAND rootfs from RAM rescue, never replace a running PID 1.
set -eu
if [ -e /etc/tq210-rootfs-ready ] || grep -q '^ubi0:rootfs ' /proc/mounts; then
    echo "Refused: boot RAM rescue and leave NAND rootfs unmounted first"
    exit 1
fi
for name in tq210-nand-init tq210-nand-sync; do
    [ -s "/tmp/$name" ]
    sh -n "/tmp/$name"
done
/sbin/tq210-ubi attach
[ "$(cat /sys/class/ubi/ubi0_1/name)" = rootfs ]
target=/mnt/rootfs
mkdir -p "$target"
if grep -q ' /mnt/rootfs ' /proc/mounts || [ -n "$(ls -A "$target")" ]; then
    echo "Refused: mountpoint is occupied"
    exit 1
fi
mount -t ubifs -o ro ubi0:rootfs "$target"
trap 'sync; umount "$target" || echo "Rootfs unmount failed; do not power-cycle"' EXIT
[ "$(cat "$target/etc/tq210-rootfs-ready")" = TQ210-NAND-ROOTFS-V1 ]
for name in tq210-nand-init tq210-nand-sync; do
    [ -f "$target/sbin/$name" ]
    [ ! -L "$target/sbin/$name" ]
    [ ! -e "$target/sbin/$name.new" ]
done
mount -o remount,rw "$target"
for name in tq210-nand-init tq210-nand-sync; do
    dst="$target/sbin/$name"
    [ -e "$dst.old" ] || cp -p "$dst" "$dst.old"
    cp "/tmp/$name" "$dst.new"
    chmod 755 "$dst.new"
    sync
    mv "$dst.new" "$dst"
done
sync
umount "$target"
trap - EXIT
/sbin/tq210-ubi detach
echo "NAND boot integration scripts updated offline; UBI detached"
