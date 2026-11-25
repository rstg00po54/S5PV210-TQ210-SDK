# CoreB 硬件探测

使用已实机验证的双阶段启动，UART0 为 115200 8N1。

构建：

    bash build_hw_probe.sh

先打开串口。断电重启后：

    sudo python3 dnw_ram.py stage1 --timeout 10000
    sudo python3 dnw_ram.py stage2 out/hw_probe/hw_probe.bin --timeout 10000 --join-tail

发送 r 重复报告。

报告 MIDR/SCTLR、原始 PLL/时钟选择/分频寄存器（在主机上根据报告推算频率，而非猜测）、DMC0/1 配置和映射窗口大小（不代表实际装入的 RAM）、16 KiB 测试及 PMU 周期（并非校准的 DDR 性能基准），以及 NAND CE0 原始 8 字节 ID。不进行 Flash 擦除、编程或页读取。若 NAND 识别失败，不应猜测容量。不写入自身 DDR 镜像、栈和测试区之外的内存；UART/NAND 与 PMU 需要配置寄存器。当时尚未全容量镜像测试，需先确认 DMC 映射。

参考：原厂 `bare-metal/no-os-examples/src/14.nand/nand.c`、`bare-metal/no-os-examples/src/6.sdram/BL1/memory.S` 和已验证的 uart_rx.S。DMC 映射：https://www.eepw.com.cn/article/201611/322804.htm 。NAND 时钟门控：Linux drivers/clk/samsung/clk-s5pv210.c。初次提交时待目标板验证。

## CoreB 实机观察

- MIDR 0x412FC082：Cortex-A8 r2p2。SCTLR 0x00C51078：MMU 和数据缓存关闭，指令缓存打开。
- APLL_CON0 A0FA0601、MPLL_CON A29B0C01、CLK_SRC0 00000111、CLK_DIV0 14131440。以 24 MHz 输入推算 CPU 1000 MHz，MPLL 667 MHz。
- DMC0 MEMCONTROL 002024D3、MEMCONFIG0 20E01343、MEMCONFIG1 1EE01343。DMC1 MEMCONTROL 002024D3、MEMCONFIG0 40E01323、MEMCONFIG1 FFE01323。CS0 窗口各为 512 MiB，位于 0x20000000 和 0x40000000。当时物理容量与地址镜像未验证。
- 23E00880 处 16 KiB 测试零错误。写入周期 0006BC66，读取+比较周期 00080007。并非峰值带宽测试。
- NAND CE0 ID 01 D3 D1 95 58 重复出现。与 Spansion S34ML08G1 单 CE、8 Gibit 配置匹配，两个 4 Gibit die，2048+64 字节/页，64 页/块，共 8192 块。参考数据手册表 7.1 和附录 A：https://www.mouser.com/datasheet/2/980/spansion_S34ML08G1-1592281.pdf 。仅 ID 匹配，尚需进一步验证实际芯片。
- 已验证启动方式：stage1 默认原厂校验、分离尾部，stage2 --join-tail，启用 SET_CONFIGURATION(1)。UART0 TX 和 RX 均验证。

后续独立 OpenBoot 的 DDR 几何参数及完整内存测试见 [DDR_GEOMETRY.md](../openboot/DDR_GEOMETRY.md)。
