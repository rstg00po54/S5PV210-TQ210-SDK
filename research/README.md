# S5PV210 / TQ210 硬件研究

本目录独立于原厂 SDK，不修改原厂文件内容。最初用于 USB 协议逆向；后续双阶段 RAM 启动与硬件探测已实机验证，独立启动器开发见 [OpenBoot](../openboot/README.md)。

## 从 SDK 得到的发现

- 直接分析了 `linux-tools/opt/EmbedSky/TQBoardDNW for linux/TQBoardDNW_64`（未去符号的 ELF）。符号地址：QDownLoadThread::run 0x427d80、TQMain::downLoadFile 0x417710、TQMain::download210Boot 0x417910、csum_upload 0x4278a0。
- downLoadFile 中包含地址常量 0xd0020010 和 0x23e00000，选择取决于模式与镜像类型。USB 包头地址不一定就是 CPU 执行入口。
- run 构造 8 字节包头，包含地址及文件长度 + 10。普通路径首个 2048 字节块中读取至多 2040 字节有效数据，此后每块至多 2048 字节；另有独立 2 字节校验和传输。特殊镜像路径当时尚未解析。
- 校验函数包含无符号字节累加；最初尚需验证长文件校验是覆盖全文件还是有限范围。
- 原厂说明明确要求 USB 启动使用 V210_USB.BL2.bin。它是 ARM 镜像（首条 ARM 分支指向 0x20 偏移），不是通用 UART 测试程序。早期尚未确认封包及执行地址。
- 原厂 `bare-metal/no-os-examples/src/12.uart_putchar/Makefile` 链接到 0x20000000，start.S 设置 SP=0x40000000，需要已初始化的 DDR。main.c 等待接收字节，再发送字节+1；不会主动输出问候。
- USB 传输成功不能证明 CPU 已执行，不能因未验证镜像失败而直接认定 UART 损坏。

## 工具

- audit_vendor.sh：提取原厂函数及可选 ARM 固件反汇编到本地输出目录。
- dnw_probe.c：使用 libusb 1.0 的只读 USB 描述符和端点诊断，不写有效数据，用于区分传输与启动镜像问题。

    cd research
    bash audit_vendor.sh
    make
    sudo ./dnw_probe

## 早期实验计划

从反汇编解析特殊镜像封包及校验范围，查阅随附 iROM 应用笔记，然后实现启动阶段上传器。如果 CH340C TX 电压超过核心板 UART 额定值，应隔离该输出。DDR 初始化后使用原厂 UART 测试，115200 8N1 发送 A，应收到 B。

不要烧写 NAND/SD 或发送未经验证的初始化镜像。

## 更新：双阶段 RAM 启动实验

[USB_PROTOCOL.md](USB_PROTOCOL.md) 记录精确逆向证据及测试命令。dnw_ram.py 实现原厂普通路径及两种校验模式。uart_ddr.S 是 320 字节无栈 ARM UART 测试，曾使用 clang/LLD 本地构建，入口 0x23e00000，并提供 GNU ARM 构建脚本。封包编码在本地以 9 种长度和两种校验模式测试。最初提交时实际 USB 传输和 CPU 执行仍待实机测试，后续结果见 [HW_PROBE.md](HW_PROBE.md)。此方案替代了早期仅探测计划。
