#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p out
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    cc=arm-none-eabi-gcc
    objcopy=arm-none-eabi-objcopy
    objdump=arm-none-eabi-objdump
    target=
else
    cc=clang
    objcopy=llvm-objcopy
    objdump=llvm-objdump
    target="--target=arm-none-eabi -fuse-ld=lld"
fi
sources="arch/arm/start.S arch/arm/cpu.c arch/arm/benchmark.c drivers/uart.c drivers/timer.c mm/mmu.c mm/pages.c kernel/main.c kernel/selftest.c shell/shell.c"
for variant in runtime runtime-safe; do
    defs=
    if [ "$variant" = runtime-safe ]; then
        defs=-DRUNTIME_SAFE
    fi
    "$cc" $target -mcpu=cortex-a8 -marm -mfloat-abi=soft -Os -g \
        -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -ffunction-sections -fdata-sections -nostdlib -Iinclude $defs \
        -Wl,--gc-sections,-T,arch/arm/runtime.ld,-Map,out/$variant.map \
        $sources -o "out/$variant.elf"
    "$objcopy" -O binary "out/$variant.elf" "out/$variant.bin"
    "$objdump" -d "out/$variant.elf" > "out/$variant.dis"
    bytes=$(wc -c < "out/$variant.bin")
    if [ "$bytes" -lt 4 ] || [ "$bytes" -gt 16777216 ] || [ "$((bytes % 4))" -ne 0 ]; then
        echo "Invalid OBL2 image size: $bytes" >&2
        exit 1
    fi
    echo "$variant.bin: $bytes bytes"
done
