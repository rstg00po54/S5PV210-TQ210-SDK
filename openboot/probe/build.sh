#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p ../out
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  cc=arm-none-eabi-gcc; target=; linker=; objcopy=arm-none-eabi-objcopy
else
  cc=clang; target=--target=arm-none-eabi; linker=-fuse-ld=lld; objcopy=llvm-objcopy
fi
for mode in gpio uart; do
  defs=; [ "$mode" != gpio ] || defs=-DGPIO_ONLY
  "$cc" $target $linker -mcpu=cortex-a8 -marm -nostdlib -Wl,-Ttext=0xd0020010,-e,_start,--build-id=none $defs entry.S -o ../out/entry_$mode.elf
  "$objcopy" -O binary -j .text ../out/entry_$mode.elf ../out/entry_$mode.bin
  wc -c ../out/entry_$mode.bin
done

"$cc" $target $linker -mcpu=cortex-a8 -marm -nostdlib -Wl,-Ttext=0xd0020010,-e,_start,--build-id=none rom_state.S -o ../out/rom_state.elf
"$objcopy" -O binary -j .text ../out/rom_state.elf ../out/rom_state.bin
wc -c ../out/rom_state.bin
