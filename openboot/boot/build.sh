#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p ../out
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  cc=arm-none-eabi-gcc; objcopy=arm-none-eabi-objcopy; target=; linker=; objdump=arm-none-eabi-objdump
else
  cc=clang; objcopy=llvm-objcopy; target=--target=arm-none-eabi; linker=-fuse-ld=lld; objdump=llvm-objdump
fi
for mode in sram ddr; do
  defs=; [ "$mode" != sram ] || defs=-DSRAM_ONLY
  "$cc" $target $linker -mcpu=cortex-a8 -marm -mfloat-abi=soft -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -Wl,-T,iram.ld,-Map,../out/$mode.map $defs start.S main.c -o ../out/$mode.elf
  "$objcopy" -O binary ../out/$mode.elf ../out/$mode.bin
  "$objdump" -d ../out/$mode.elf > ../out/$mode.dis
  wc -c ../out/$mode.bin
done
