#!/bin/sh
set -eu
cd "$(dirname "$0")"
arm-none-eabi-as -mcpu=cortex-a8 -o uart_ddr.o uart_ddr.S
arm-none-eabi-ld -Ttext=0x23e00000 -e _start -o uart_ddr.elf uart_ddr.o
arm-none-eabi-objcopy -O binary uart_ddr.elf uart_ddr.bin
arm-none-eabi-objdump -d uart_ddr.elf > uart_ddr.dis
arm-none-eabi-size uart_ddr.elf
