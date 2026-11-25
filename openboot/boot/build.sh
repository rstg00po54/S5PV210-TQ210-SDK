#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p ../out
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  cc=arm-none-eabi-gcc; objcopy=arm-none-eabi-objcopy; target=; linker=; objdump=arm-none-eabi-objdump
else
  cc=clang; objcopy=llvm-objcopy; target=--target=arm-none-eabi; linker=-fuse-ld=lld; objdump=llvm-objdump
fi
# The CMake build consumes the same source and variant manifests.
sources=$(sed 's|^|../|' sources.txt | tr '\n' ' ')
while IFS='|' read -r mode defs; do
  "$cc" $target $linker -mcpu=cortex-a8 -marm -mfloat-abi=soft -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -Wl,-T,iram.ld,-Map,../out/$mode.map -I../include $defs start.S $sources -o ../out/$mode.elf
  "$objcopy" -O binary ../out/$mode.elf ../out/$mode.bin
  "$objdump" -d ../out/$mode.elf > ../out/$mode.dis
  wc -c ../out/$mode.bin
done < variants.txt

"$cc" $target $linker -mcpu=cortex-a8 -marm -mfloat-abi=soft -nostdlib -Wl,-T,ddr_app.ld,-Map,../out/ddr_app.map ddr_app.S -o ../out/ddr_app.elf
"$objcopy" -O binary ../out/ddr_app.elf ../out/ddr_app.bin
"$objdump" -d ../out/ddr_app.elf > ../out/ddr_app.dis
wc -c ../out/ddr_app.bin
