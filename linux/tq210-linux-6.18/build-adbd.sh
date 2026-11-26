#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$here/../build-rootfs-tq210/adbd"
rev=ba0f0fd06acaa28d6d264818a1703e462b8bfbbc
cross=${CROSS_COMPILE:-arm-linux-gnueabihf-}
cc=${CC:-"${cross}gcc-10"}
mkdir -p "$out"
tarball="$out/source-$rev.tar.gz"
if [ ! -s "$tarball" ]; then
    curl -fL --retry 3 -o "$tarball.part" "https://codeload.github.com/ajiang17/android-adbd-for-linux/tar.gz/$rev"
    mv "$tarball.part" "$tarball"
fi
src="$out/android-adbd-for-linux-$rev"
# Always re-extract clean pinned source before applying local changes.
tar -xzf "$tarball" -C "$out"
python3 "$here/patch-adbd.py" "$src"
make -C "$src" clean
make -C "$src" CC="$cc -fcommon" -j"${JOBS:-$(nproc)}"
"${cross}strip" "$src/adbd"
LC_ALL=C "${cross}readelf" -h "$src/adbd" | grep -q 'Machine:.*ARM'
if LC_ALL=C "${cross}readelf" -l "$src/adbd" | grep -q INTERP; then
    echo "adbd must be static" >&2; exit 1
fi
cp "$src/adbd" "$out/adbd"
ls -lh "$out/adbd"
ADBD_BIN="$out/adbd" sh "$here/build-rootfs.sh"
