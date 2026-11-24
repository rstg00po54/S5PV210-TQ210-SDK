#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p out/hw_probe
out=out/hw_probe
arm-none-eabi-gcc -mcpu=cortex-a8 -marm -c hw_probe_start.S -o "$out/start.o"
arm-none-eabi-gcc -mcpu=cortex-a8 -marm -mfloat-abi=soft -Os -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -c hw_probe.c -o "$out/probe.o"
arm-none-eabi-ld -T hw_probe.ld -o "$out/hw_probe.elf" "$out/start.o" "$out/probe.o"
arm-none-eabi-objcopy -O binary "$out/hw_probe.elf" "$out/hw_probe.bin"
arm-none-eabi-size "$out/hw_probe.elf"
arm-none-eabi-objdump -d "$out/hw_probe.elf" > "$out/hw_probe.dis"
