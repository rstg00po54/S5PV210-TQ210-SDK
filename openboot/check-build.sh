#!/bin/sh
# Build-only regression. No USB connection or flash operation is required.
set -eu
cd "$(dirname "$0")"

sh boot/build.sh
cmake -S tools -B build-host
cmake --build build-host
./build-host/tq210-dnw --selftest

while IFS='|' read -r variant defs; do
    size=$(wc -c < "out/$variant.bin")
    if [ "$size" -eq 0 ] || [ "$size" -gt 11168 ]; then
        echo "FAIL: $variant.bin is $size bytes; ROM frame limit is 11168" >&2
        exit 1
    fi
    printf 'ROM frame PASS: %-24s %s bytes\n' "$variant" "$size"
done < boot/variants.txt

test -s out/ddr_app.bin
echo "BUILD CHECK PASS (hardware regression still required)"
