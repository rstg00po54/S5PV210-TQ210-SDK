#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run on live NAND Linux after pushing the two updated scripts into /tmp.
set -eu
[ "$(cat /etc/tq210-rootfs-ready)" = TQ210-NAND-ROOTFS-V1 ]
grep -q '^ubi0:rootfs / ubifs rw,' /proc/mounts
for name in tq210-nand-init tq210-nand-sync; do
    [ -s "/tmp/$name" ]
    sh -n "/tmp/$name"
    [ -f "/sbin/$name" ]
done
for name in tq210-nand-init tq210-nand-sync; do
    [ -e "/sbin/$name.old" ] || cp -p "/sbin/$name" "/sbin/$name.old"
    cp "/tmp/$name" "/sbin/$name.new"
    chmod 755 "/sbin/$name.new"
    sync
    mv "/sbin/$name.new" "/sbin/$name"
done
sync
echo "NAND boot integration scripts updated; previous copies retained"
