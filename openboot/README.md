# TQ210 OpenBoot — 独立启动加载器

适用目标：天嵌 TQ210 CoreB / S5PV210。本项目根据原厂 USB BL2 的逆向结果独立实现 SRAM 启动、双 DMC DDR2 初始化及 USB 下载。固件不包含原厂 BL2 机器码，不访问 NAND/SD，不写入非易失存储；断电后 RAM 中的程序消失。

## 当前进度

- 已实机验证 ROM 时钟继承、独立 SRAM C 入口、双 DMC 初始化及完整 1 GiB DDR 地址/数据模式测试。
- 已验证同一 USB 连接上的双向 DMA、直接向 DDR 下载并执行独立 ARM 镜像。
- 推荐使用 `rom_usb_dma_fast.bin`；历史实验固件仍保留。完整测试条件与限制见 [DDR_GEOMETRY.md](DDR_GEOMETRY.md)、[USB_OTG.md](USB_OTG.md)。
- 目前尚未实现 Linux 启动 ABI、持久化存储启动及生产级 USB 协议栈。

## 构建及使用（Ubuntu / VMware Linux）

在仓库根目录执行：

```sh
cd openboot
# 如缺少工具链：
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi g++ cmake libusb-1.0-0-dev
sh boot/build.sh
cmake -S tools -B build-host
cmake --build build-host
./build-host/tq210-dnw --selftest
```

也可使用 clang/LLD + llvm-objcopy + llvm-objdump 构建固件。本目录只跟踪源码，out/ 和 build-host/ 为本地构建产物。每次上传前完全断电再进入 USB iROM 下载模式；只连接一块 Samsung 04e8:1234 设备。先打开 UART0（例如 Windows COM5），115200 8N1，无流控，确认板卡 UART 电平匹配。不要先上传原厂 BL2。

```sh
sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin out/ddr_app.bin
# 另一次冷启动后进行数据校验测试：
sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_fast.bin
```

预期串口输出 `DDR APP EXEC PASS: independent ARM image at 0x24000000`。数据测试预期长度 1048589，CRC32 为 0x6eefefc7。主机 GO ACK 不等于实际执行；必须检查串口。DMA 继承方案仅用于受控本地实验，接管期间不要使用其他 USB 查询工具。

## 历史 v0.1 实验（未经原始实机验证的独立 PLL 初始化）

最早版本用于分离 SRAM 入口与 DDR 问题。以下是历史测试方法，不是当前推荐启动方式。

```sh
./build-host/tq210-dnw out/sram.bin --pad-vendor-size --dry-run
sudo ./build-host/tq210-dnw out/sram.bin --pad-vendor-size
```

默认使用已验证的原厂 stage1 分离校验和传输、SET_CONFIGURATION(1)，并填充至原厂 11168 字节（仅添加零，不包含原厂代码），保持下载尺寸及传输形状。初始地址 0xD0020010。USB 传输成功不代表 CPU 已执行。

历史预期日志：

```text
TQ210 OpenBoot v0.1 EXPERIMENTAL
[0] SRAM ENTRY OK
[1] PLL setup
APLL=0xA0FA0601
MPLL=0xA29B0C01
CLK_SRC0=0x00000111
CLK_DIV0=0x14131440
[1] CLOCK OK
SRAM TEST PASS; DDR not touched
```

SRAM 版完成后，串口输入任意字符可重打 PASS 信息。如果收到完整 SRAM PASS，再断电、进入 ROM USB 下载模式，执行：

```sh
sudo ./build-host/tq210-dnw out/ddr.bin --pad-vendor-size
```

预期 DMC0/1 PHY、DDR2 命令、寄存器日志，以及：

```text
[4] DDR SCRATCH PASS (not capacity verification)
[5] DDR execution result=0x5A5A2100
[5] DDR EXEC PASS; back in SRAM
```

若失败，保存完整下载器输出和串口日志，不自动重试。若传输成功但没有串口输出，检查 USB 是否仍在 iROM 模式、UART 电平、串口设置，并按一次只改变一个选项的方式 A/B 测试：断电重进 ROM 后可尝试取消 `--pad-vendor-size`。`--full-checksum`、`--join-tail` 属于额外实验选项；原厂 stage1 实测使用分离尾部、vendor checksum，不建议在首次尝试混用。任何一次失败后先断电恢复。没有 UART 输出无法区分 ROM 未执行、异常入口、早期时钟问题，不应直接归因于 DDR。

## 源码结构

- `boot/start.S`：ARM 入口、对齐的 VBAR、SVC 栈、BSS、异常处理及位置无关 DDR 测试代码。
- `boot/iram.ld`：链接地址 0xD0020010，在实验用 16 KiB 窗口内保留空间，检查入口、对齐和栈边界。
- `boot/main.c`：启动流程编排。
- `soc/s5pv210/`、`drivers/`、`diagnostics/`、`protocol/`：SoC 初始化、外设驱动、硬件测试和下载协议。
- `tools/`：原生 Linux C++17/libusb 下载器，无 Python 运行时、原厂可执行程序或固件依赖。

详见 [REFACTOR.md](REFACTOR.md)。早期验证文档描述的是历史源码布局。

## 测试含义与假设

历史 SRAM-only 版本初始化时钟但不访问 DDR。假设 ROM 在特权 ARM 模式运行，MMU 和 D-cache 关闭；否则启动代码停止，不改变现有内存映射。假设 24 MHz 晶振以及已知 CoreB DDR 配置。历史版本重新配置 APLL/MPLL/EPLL 并限时等待锁定，不修改未被观测到的 CLK_SRC0 选中的 VPLL。原始时钟切换及 DDR 前 UART 分频未实测；串口诊断需实际结果才能证明初始化。DMC PHY 强制配置及状态判断来自原厂二进制，不是通用 DDR2 算法；命令间隔采用保守实现。当前推荐版本继承 ROM 时钟，详见 [ROM_CLOCKS.md](ROM_CLOCKS.md)。

两个内存窗口分别在 0x20010000 和 0x40010000 写入不同的 16 KiB 模式及其反码，全部写完后再校验。随后将可执行测试代码复制到 0x20010000，执行并返回常量到 SRAM 调用者。这证明指令执行，不代表完整操作系统重定位。栈及诊断始终位于 SRAM。单次小范围测试不能确定完整容量。可选 a/b 命令测试稀疏地址镜像（16–256 MiB 偏移）；未装内存的地址可能导致总线挂起。仅在独立 RAM 测试时使用；访问完成时按逆序恢复原值。不能仅凭控制器配置的 512 MiB 窗口推断实际容量。后续完整测试已另行记录。

## 证据与参考

- 原厂 V210_USB.BL2.bin Git blob b271436e5e693a1de43b872a4c8b1d3d2937a83d，11168 字节；启动偏移 0x20–0xA8，DMC 初始化偏移 0x23EC–0x27BC，DMC0/1 数据表 0x2890 / 0x298C，命令表 0x292C / 0x2A28。寄存器值独立转录，未包含原厂机器码。
- [USB_PROTOCOL.md](../research/USB_PROTOCOL.md)、[HW_PROBE.md](../research/HW_PROBE.md)、`research/uart_rx.S` 与 `bare-metal/no-os-examples/src/10.clock_s/clock.S`。
- TQ210 DDR 地址窗口：https://www.eepw.com.cn/article/201611/322804.htm
- Linux Samsung S5PV210 时钟驱动：https://cos.googlesource.com/third_party/kernel/+/cos-5.15/drivers/clk/samsung/clk-s5pv210.c

## 历史本地验证

v0.1 使用 clang/LLD 交叉编译，反汇编确认 ARM ELF 入口和向量表对齐；主机端使用 GNU C++ 编译、链接 libusb，封包自测和 dry-run 通过。当时未进行实际 USB 传输、DDR/UART 执行，完整 stage2 USB 驱动及自动化尚未完成。后续版本的实机记录见相关专题文档。
