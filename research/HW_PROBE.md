# CoreB hardware probe
Uses the physically verified two-stage boot, UART0 at 115200 8N1.
Build: bash build_hw_probe.sh
Power-cycle, then sudo python3 dnw_ram.py stage1 --timeout 10000
Then sudo python3 dnw_ram.py stage2 out/hw_probe/hw_probe.bin --timeout 10000 --join-tail
Open serial first. Send r to repeat.
Reports MIDR/SCTLR, raw PLL/mux/divider registers (frequency is derived on host from the report, not guessed), DMC0/1 configs and mapping window sizes (not confirmed populated RAM), 16 KiB scratch test and PMU cycles (not a calibrated DDR benchmark), NAND CE0 raw 8-byte ID. No flash erase/program or page reads. If NAND identification fails, do not guess capacity. No writes outside own DDR image/stack/scratch; peripheral configuration writes required for UART/NAND and PMU. No full-capacity alias probing yet, pending observed DMC mapping. Source: vendor SDK src/14.nand/nand.c, src/6.sdram/BL1/memory.S, known-working uart_rx.S. DMC mapping reference: https://www.eepw.com.cn/article/201611/322804.htm . NAND clock gate: Linux drivers/clk/samsung/clk-s5pv210.c. Code pending target validation.
