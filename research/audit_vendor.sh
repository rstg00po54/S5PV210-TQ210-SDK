#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vendor="$root/Linux平台工具/opt/EmbedSky/TQBoardDNW for linux"
out="${1:-$root/research/out}"
mkdir -p "$out"
exe="$vendor/TQBoardDNW_64"
fw="$vendor/V210_USB.BL2.bin"
file "$exe" "$fw" > "$out/files.txt"
sha256sum "$exe" "$fw" > "$out/sha256.txt"
nm -n -C "$exe" > "$out/symbols.txt"
readelf -a "$exe" > "$out/elf.txt"
objdump -d -M intel -C "$exe" > "$out/vendor-full.dis"
for sym in _ZN15QDownLoadThread3runEv _ZN6TQMain12downLoadFileE7QString _ZN6TQMain15download210BootEv _ZN15QDownLoadThread13startDownLoadE7QStringiii10IMAGE_TYPE _Z11csum_uploadPKc _Z18getEndpointAddressP13libusb_device; do
  objdump -d -M intel --disassemble="$sym" "$exe" > "$out/$sym.dis"
done
od -Ax -tx1 -N128 "$fw" > "$out/bl2-head.txt"
if command -v arm-none-eabi-objdump >/dev/null; then
  arm-none-eabi-objdump -D -b binary -m arm -EL "$fw" > "$out/bl2-offsets.dis"
fi
printf 'Audit saved to %s\n' "$out"
