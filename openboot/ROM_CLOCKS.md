# OpenBoot v0.3 — 保留冷启动 iROM 时钟

2026-11-25 实机快照，未使用原厂 BL2：

```
ENTRY_CPSR=0x60000153
ENTRY_SP=0xD0037D50
MIDR=0x412FC082
SCTLR=0x00C51078
VBAR=0x00000000
APLL_CON0=0xA0C80601
MPLL_CON=0xA29B0C01
EPLL_CON0=0xA8500303
VPLL_CON=0x006C0303
CLK_SRC0=0x00000111
CLK_SRC1=0x00000000
CLK_SRC4=0x66667777
CLK_SRC6=0x00000000
CLK_DIV0=0x14141251
CLK_DIV1=0x00000400
CLK_DIV4=0x99990000
CLK_DIV6=0x00071000
CLK_DIV_STAT0=0x00000000
CLK_DIV_STAT1=0x00000000
CLK_GATE_0400=0xFFFFFFFF
CLK_GATE_0404=0xFFFFFFFF
```

假设 FIN=24 MHz：APLL=800 MHz，ARMCLK=400 MHz，HCLKM=133.33 MHz，A2M=133.33 MHz，DMC0 由 A2M /1 提供，DMC1 由 HCLKM 提供，PCLKP=66.7 MHz。未实际测量频率。参考 Linux S5PV210 时钟驱动和旧版 mach-s5pv210/clock.c。

保持 ROM 时钟树不变，仅配置 UART 门控/引脚复用及看门狗。DDR 初始化前必须精确匹配关键时钟寄存器。TIMINGAREF=0x400（133.33 MHz 下标称 7.68 us），而非原厂 200 MHz 下的 0x618；其余时序周期保留为实验性保守配置。PHY 强制配置及状态检查来自原厂 BL2；当时尚未验证较低时钟下的有效性。写入前记录 DMC 控制和状态。

在 openboot 目录：

```sh
git pull
sh boot/build.sh
# 完全断电进入 ROM USB 模式，打开 UART0 115200 8N1
sudo ./build-host/tq210-dnw out/rom_sram.bin --pad-vendor-size
# 仅在 SRAM PASS 后再次断电进入 ROM USB 模式
sudo ./build-host/tq210-dnw out/rom_ddr.bin --pad-vendor-size
```

此实验不要使用旧 sram.bin/ddr.bin。两个新镜像曾使用本地 Clang/LLD 编译为 1524 和 3952 字节，ELF 入口为 0xD0020010；检查了 VBAR 和栈。后续 SRAM、DDR 小范围读写及执行测试通过，见 [实机验证日志](HW_VALIDATION_V03.md)。

启动前先打印无栈 '>' 标记，随后 C 入口、精确时钟检查及可选 DDR 初始化。SRAM PASS 表示 C 初始化和继承时钟检查通过；DDR SCRATCH/EXEC PASS 仅表示两块 16 KiB 区域及指令测试代码工作，不代表容量或全面稳定性。基础测试通过后，可以在独立测试环境中分别运行可选地址镜像探测，限制见验证日志。不写 NAND/SD。失败时断电，提供完整串口输出。
