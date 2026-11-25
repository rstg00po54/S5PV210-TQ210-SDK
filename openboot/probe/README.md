# 冷启动 USB 入口探测程序

当 OpenBoot v0.1 传输成功但没有 UART 输出时使用。两个镜像均不使用栈、CP15 初始化、DDR、PLL 修改或 Flash。保持 ROM 时钟树，使能看门狗和 UART 寄存器时钟，关闭看门狗并屏蔽 IRQ/FIQ。入口为 0xD0020010，初始跳转布局与原厂 BL2 的 0x20 偏移一致。

在 openboot 目录：

    sh probe/build.sh
    sudo ./build-host/tq210-dnw out/entry_uart.bin --pad-vendor-size

先打开 UART0（115200 8N1）。每次下载前完全断电进入 ROM USB 模式。UART 探测程序使用继承的 PCLK 循环尝试候选分频值，输出 OPENBOOT ENTRY UART0 DIV=0xNN。某些分频下乱码是预期现象。记录任何可读消息或活动；没有可读信息不能证明未执行。

若无输出，再次断电后执行：

    sudo ./build-host/tq210-dnw out/entry_gpio.bin --pad-vendor-size

用示波器观察 UART0 TX（GPA0[1]，板卡 TX 输出，共地）。预期作为 GPIO 周期翻转，而非发送 UART 数据。禁止将主动驱动的外部输出接到 TX。方波可在不依赖 UART 波特率的情况下证明代码执行。没有波形并不能下结论，仍需分别排查 ROM 入口、早期外设访问和物理信号路径。

两个镜像曾使用 clang/LLD 本地汇编和链接（分别为 148 和 388 字节）。初次提交时无板卡验证，现有 SRAM/DDR 镜像不变。失败时提供完整下载日志、串口记录和波形。每次实验之间冷启动；不要作为 stage2 使用或写 NAND。

## 实机结果与下一步

2026-11-25，用户在 115200 波特率下观察到反复出现可读的 `OPENBOOT ENTRY UART0 DIV=0x23`，中间夹杂分频循环导致的预期乱码。结合冷 ROM 实验流程，这证明不使用原厂 BL2 即可执行独立 RAM 程序。当时尚未测量精确 PCLK 和启动时 PLL。

构建固定分频、无栈的状态探测程序，断电后上传：

    sh probe/build.sh
    sudo ./build-host/tq210-dnw out/rom_state.bin --pad-vendor-size

保存从 ENTRY_CPSR、ENTRY_SP 到 CP15 和 CLK 寄存器的完整日志。发送任意字节重复 CP15/时钟快照（初始 CPSR/SP 仅输出一次）。UART 分频 35，slot 0。不写 PLL/分频/时钟选择寄存器，不访问 DDR 或 Flash。UART 引脚复用、门控、看门狗与中断屏蔽同已成功的入口探测。如果读取寄存器失败，最后打印的标签可帮助定位。新探测程序当时使用 clang/LLD 本地编译（980 字节，入口 0xd0020010，初始跳转至 +0x20），初次提交时待板卡验证。审查 ROM 状态前不要使用原始 DDR 初始化。

后续 ROM 状态实机结果见 [ROM_CLOCKS.md](../ROM_CLOCKS.md)。
