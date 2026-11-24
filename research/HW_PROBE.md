# CoreB hardware probe
Uses the physically verified two-stage boot, UART0 at 115200 8N1.
Build: bash build_hw_probe.sh
Power-cycle, then sudo python3 dnw_ram.py stage1 --timeout 10000
Then sudo python3 dnw_ram.py stage2 out/hw_probe/hw_probe.bin --timeout 10000 --join-tail
Open serial first. Send r to repeat.
Reports MIDR/SCTLR, raw PLL/mux/divider registers (frequency is derived on host from the report, not guessed), DMC0/1 configs and mapping window sizes (not confirmed populated RAM), 16 KiB scratch test and PMU cycles (not a calibrated DDR benchmark), NAND CE0 raw 8-byte ID. No flash erase/program or page reads. If NAND identification fails, do not guess capacity. No writes outside own DDR image/stack/scratch; peripheral configuration writes required for UART/NAND and PMU. No full-capacity alias probing yet, pending observed DMC mapping. Source: vendor SDK src/14.nand/nand.c, src/6.sdram/BL1/memory.S, known-working uart_rx.S. DMC mapping reference: https://www.eepw.com.cn/article/201611/322804.htm . NAND clock gate: Linux drivers/clk/samsung/clk-s5pv210.c. Code pending target validation.

## Observed CoreB hardware
- MIDR 0x412FC082: Cortex-A8 r2p2. SCTLR 0x00C51078: MMU and data cache off, instruction cache on.
- APLL_CON0 A0FA0601, MPLL_CON A29B0C01, CLK_SRC0 00000111, CLK_DIV0 14131440. With 24 MHz input, nominal CPU 1000 MHz, MPLL 667 MHz.
- DMC0 MEMCONTROL 002024D3, MEMCONFIG0 20E01343, MEMCONFIG1 1EE01343. DMC1 MEMCONTROL 002024D3, MEMCONFIG0 40E01323, MEMCONFIG1 FFE01323. CS0 windows: 512 MiB each at 0x20000000 and 0x40000000. Physical total capacity and aliasing still untested.
- 16 KiB scratch at 23E00880: zero errors. Write cycles 0006BC66, read+compare 00080007. Not a peak bandwidth benchmark.
- NAND CE0 ID 01 D3 D1 95 58 repeats. Matches Spansion S34ML08G1 single-CE 8 Gibit configuration, two 4 Gibit dies, 2048+64 bytes/page, 64 pages/block, 8192 blocks total. Datasheet Table 7.1 and Appendix A: https://www.mouser.com/datasheet/2/980/spansion_S34ML08G1-1592281.pdf
- Validated boot: stage1 default vendor checksum with split tail, stage2 --join-tail, SET_CONFIGURATION(1) enabled. UART0 TX and RX verified.
