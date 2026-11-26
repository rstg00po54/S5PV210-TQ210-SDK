#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Host-only build; no USB operations or NAND writes.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$here/../build-fit-tq210/nand-rootfs"
cross=${CROSS_COMPILE:-arm-linux-gnueabihf-}
cc=${CC:-"${cross}gcc-10"}
sh "$here/boot-fit.sh" --nand-rootfs --pack-only
[ "$(wc -c < "$out/tq210.itb")" -le 33554432 ] || {
    echo "FIT exceeds 32 MiB boot-file limit" >&2; exit 1;
}
"$cc" -static -O2 -Wall "$here/tq210-ubi.c" -o "$out/tq210-ubi"
cp "$here/tq210-install-boot" "$out/tq210-install-boot"
echo "Boot install files ready in $out"
