#!/bin/sh
set -eu
cd "$(dirname "$0")"
arm-none-eabi-as -mcpu=cortex-a8 -o uart_rx.o uart_rx.S
arm-none-eabi-ld -Ttext=0x23e00000 -e _start -o uart_rx.elf uart_rx.o
arm-none-eabi-objcopy -O binary uart_rx.elf uart_rx.bin
arm-none-eabi-objdump -d uart_rx.elf > uart_rx.dis
arm-none-eabi-size uart_rx.elf
