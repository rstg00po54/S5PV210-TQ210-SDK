#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p ../out
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  cc=arm-none-eabi-gcc; objcopy=arm-none-eabi-objcopy; target=; linker=; objdump=arm-none-eabi-objdump
else
  cc=clang; objcopy=llvm-objcopy; target=--target=arm-none-eabi; linker=-fuse-ld=lld; objdump=llvm-objdump
fi
for mode in sram ddr rom_sram rom_ddr rom_ddr_row14 rom_ddr_full rom_usb_probe rom_usb_enum rom_usb_dnw_enum rom_usb_bulk rom_usb_exec rom_usb_auto rom_usb_fast rom_usb_handoff rom_usb_dma_boot; do
  case "$mode" in
    sram) defs=-DSRAM_ONLY ;;
    ddr) defs= ;;
    rom_sram) defs="-DROM_CLOCKS -DSRAM_ONLY" ;;
    rom_ddr) defs=-DROM_CLOCKS ;;
    rom_ddr_row14) defs="-DROM_CLOCKS -DDMC0_ROW14" ;;
    rom_ddr_full) defs="-DROM_CLOCKS -DDMC0_ROW14 -DDDR_FULL_TEST" ;;
    rom_usb_probe) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_STATE_PROBE" ;;
    rom_usb_enum) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST" ;;
    rom_usb_dnw_enum) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST -DUSB_DNW_ID" ;;
    rom_usb_bulk) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST -DUSB_DNW_ID -DUSB_BULK_TEST" ;;
    rom_usb_exec) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST -DUSB_DNW_ID -DUSB_BULK_TEST -DUSB_EXEC_TEST" ;;
    rom_usb_auto) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST -DUSB_DNW_ID -DUSB_BULK_TEST -DUSB_EXEC_TEST -DUSB_AUTO_BOOT" ;;
    rom_usb_fast) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_ENUM_TEST -DUSB_DNW_ID -DUSB_BULK_TEST -DUSB_EXEC_TEST -DUSB_AUTO_BOOT -DUSB_FAST_BOOT" ;;
    rom_usb_handoff) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_HANDOFF_TEST" ;;
    rom_usb_dma_boot) defs="-DROM_CLOCKS -DDMC0_ROW14 -DUSB_HANDOFF_TEST -DUSB_HANDOFF_BOOT" ;;
  esac
  "$cc" $target $linker -mcpu=cortex-a8 -marm -mfloat-abi=soft -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -Wl,-T,iram.ld,-Map,../out/$mode.map $defs start.S main.c -o ../out/$mode.elf
  "$objcopy" -O binary ../out/$mode.elf ../out/$mode.bin
  "$objdump" -d ../out/$mode.elf > ../out/$mode.dis
  wc -c ../out/$mode.bin
done

"$cc" $target $linker -mcpu=cortex-a8 -marm -mfloat-abi=soft -nostdlib -Wl,-T,ddr_app.ld,-Map,../out/ddr_app.map ddr_app.S -o ../out/ddr_app.elf
"$objcopy" -O binary ../out/ddr_app.elf ../out/ddr_app.bin
"$objdump" -d ../out/ddr_app.elf > ../out/ddr_app.dis
wc -c ../out/ddr_app.bin
