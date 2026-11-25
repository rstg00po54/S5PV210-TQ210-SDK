# TQ210 OpenBoot v0.1 — 实验性独立 SRAM / DDR2 启动

适用目标：天嵌 TQ210 CoreB / S5PV210，按原厂 USB BL2 逆向得到的双 DMC 参数编写。**未实机验证，不是已完成的替代 BL2。** 不包含原厂 BL2 二进制，不访问 NAND/SD，不写非易失存储。断电后 RAM 中的程序消失。

## 构建及第一次实机验证（Ubuntu / VMware Linux）

在仓库根目录执行：

```sh
cd openboot
# 如缺少工具链：
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi g++ cmake libusb-1.0-0-dev
sh boot/build.sh
cmake -S tools -B build-host
cmake --build build-host
./build-host/tq210-dnw --selftest
./build-host/tq210-dnw out/sram.bin --pad-vendor-size --dry-run
```

也可使用 clang/LLD + llvm-objcopy + llvm-objdump 构建固件。本目录只跟踪源码，out/ 和 build-host/ 为本地构建产物。

建议在已验证原厂 stage1 USB 传输的同一 Ubuntu 环境使用。先打开 UART0（例如 Windows COM5），115200 8N1，无流控，确认板卡 UART 电平匹配。每次上传前完全断电再进入 USB iROM 下载模式；只连接一块 Samsung 04e8:1234 设备。**不要先上传原厂 BL2。**

```sh
sudo ./build-host/tq210-dnw out/sram.bin --pad-vendor-size
```

默认使用已验证的原厂 stage1 分离校验和传输、SET_CONFIGURATION(1)，并填充至原厂 11168 字节（仅添加零，不包含原厂代码），保持下载尺寸及传输形状。初始地址 0xD0020010。USB 传输成功不代表 CPU 已执行。

预期日志：

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

- `boot/start.S`: ARM entry, aligned VBAR, SVC stack, BSS, exception handling, position-independent DDR stub.
- `boot/iram.ld`: link at 0xD0020010, reserve within 16 KiB experimental window; assertions for entry, alignment and stack limit.
- `boot/main.c`: UART, bounded PLL/DMC polling, original board-specific DDR register values, independent cold RAM test.
- `tools/dnw.cpp`: native Linux C++17/libusb downloader, stage1 only at this milestone. No Python, no vendor executable or firmware dependencies.

## Test semantics and assumptions

SRAM-only image initializes clocks but does not touch DDR. Assumes ROM runs in privileged ARM mode, MMU and D-cache disabled; otherwise startup halts without changing live memory mappings. Assumes 24 MHz crystal and known CoreB DDR setup. APLL/MPLL/EPLL are reconfigured with bounded lock polling, VPLL left untouched (not selected in the observed CLK_SRC0). Clock transition and pre-DDR UART divisor have not been measured on the hardware. UART diagnostics do not prove hardware initialization until actual results are seen. DMC PHY force and status predicate are from the vendor binary, not a generic DDR2 algorithm; DMC command gaps are a conservative implementation choice.

Both banks receive distinct 16 KiB patterns and their complements at 0x20010000 and 0x40010000; both are checked after both writes. The executable stub is then copied to 0x20010000 and returns a constant to the SRAM caller. This verifies instruction execution, not full OS relocation. The program's stack and diagnostics stay in SRAM. A successful test does not establish full memory capacity. Optional interactive a and b run sparse address-alias samples (16–256 MiB offsets), may cause bus hangs on unpopulated addresses; defer until basic DDR passes and use only during standalone RAM testing. Restore sampled words in reverse order when accesses complete. Do not assume the configured 512 MiB apertures are fully populated.

## Evidence / references

- Original V210_USB.BL2.bin Git blob b271436e5e693a1de43b872a4c8b1d3d2937a83d, 11168 bytes; startup offsets 0x20–0xA8, DMC init offsets 0x23EC–0x27BC, DMC0/1 data tables at 0x2890 / 0x298C, command tables 0x292C / 0x2A28. Register values were independently transcribed into this source; original machine code is not included.
- Existing SDK [USB_PROTOCOL.md](../research/USB_PROTOCOL.md), [HW_PROBE.md](../research/HW_PROBE.md), uart_rx.S, and SDK src/10.clock_s/clock.S.
- TQ210 DDR address windows: https://www.eepw.com.cn/article/201611/322804.htm
- Linux Samsung S5PV210 clock driver: https://cos.googlesource.com/third_party/kernel/+/cos-5.15/drivers/clk/samsung/clk-s5pv210.c

## Local validation

Cross-compiled using clang/LLD, ARM ELF entry and vector table alignment verified in disassembly; C++ host compiled with GNU C++ and linked libusb; packet selftest and dry runs passed. No physical USB transfer or DDR/UART execution tested here. Full stage2 USB device driver and downloader automation are future work, not included in v0.1.
