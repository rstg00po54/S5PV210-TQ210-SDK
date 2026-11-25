# TQ210 Runtime v0.1 — 自研 DDR 系统运行环境

这是独立于 OpenBoot 的实验性运行环境，不是 Linux。由已验证的 OBL2 USB DMA 加载器放到 0x24000000 执行。保留 ROM 400 MHz 时钟，不重新初始化 DDR、PLL、USB，不读写 NAND/SD。入口依赖 ARM SVC、MMU 和 D-Cache 关闭、UART0 已配置。无需原厂 BL2。

## 构建

在 SDK 根目录：

    sh runtime/build.sh

支持 GNU arm-none-eabi 或 clang/LLD + LLVM 工具。生成 runtime/out/runtime.bin（自动开启 MMU 和 D-Cache）及 runtime/out/runtime-safe.bin（安全排查版，不自动开启）。两者均有 ELF、map、反汇编，链接入口必须为 0x24000000，文件长度须为 4 的倍数且不超过 16 MiB。

## 首次上板：先安全版，再正常版

    cd openboot
    sh boot/build.sh
    cmake -S tools -B build-host
    cmake --build build-host

打开 UART0 115200 8N1，完全断电进入 iROM USB 下载模式，确保仅连接一块板。先加载安全版：

    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin ../runtime/out/runtime-safe.bin

预期 [0] DDR ENTRY、[1] Page allocator ready、[2] Initial memory test PASS、Shell 提示符。依次输入：

    help
    cpuinfo
    meminfo
    alloc 16
    meminfo
    free
    memtest
    mmu
    mmu on
    memtest
    cache on
    memtest

初始保留 18 MiB：DMC0/1 起始各 1 MiB（包含旧测试区），以及 0x24000000..0x24ffffff 的 16 MiB 运行环境区域。初始应有 257536 个空闲页（1006 MiB）。alloc 16 应减少 16 页，free 应恢复。MMU 开启后应显示 TTBR0、TTBCR=0、DACR=1；D-Cache 开启后 memtest 应继续通过。遇挂起或异常，保存完整串口记录并冷复位；不要直接尝试其他修改时钟或 DDR 的固件。

安全版全部通过后，另一次冷启动加载自动版：

    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin ../runtime/out/runtime.bin

预期进一步打印 [3] MMU ON、[4] D-Cache ON、[5] Cached memory test PASS，再进入 Shell。以上均为预期验收条件，尚未实机确认。

## 架构

- arch/arm/start.S：独立异常向量、SVC/IRQ/FIQ/ABT/UND/SYS 栈、BSS 清零；异常报告后停止，不返回。
- arch/arm/runtime.ld：独立 DDR 链接布局、向量对齐及边界断言。
- arch/arm/cpu.c：CP15、时钟及异常诊断。
- mm/mmu.c：ARMv7 short descriptor 一级页表（16 KiB 对齐、1 MiB section），DDR 恒等映射，Normal WBWA、特权访问；首个镜像 section 可执行，其他 DDR XN；仅映射必要 MMIO，Strongly-ordered/XN，未映射地址触发异常。域 0 为 client。启用前按 CLIDR/CCSIDR 遍历并失效数据缓存，TLB/I-cache/分支预测器失效；页表暂为静态。
- mm/pages.c：4 KiB 物理页位图、连续分配与释放、小范围双模式测试；测试在 Cache 开启时 clean+invalidate 后读回。
- drivers/uart.c：沿用 OpenBoot 已验证 UART 设置，轮询输入输出。
- kernel/main.c、shell/shell.c：分阶段初始化及交互命令。

## 限制

这不是完整虚拟内存管理或多任务 OS：无用户态、进程隔离、二级页表、IRQ 调度或定时器。页分配器仅供可信单线程内核调用，释放须使用原始分配边界，尚无所有权表。Shell 仅允许一个显式分配；memtest 使用自身临时页，不覆盖系统或整片 DDR。MMU/Cache 不支持动态关闭，避免未经 clean 的脏数据丢失。没有 PMU 性能测试、重启驱动或任意地址读写命令；这些留待基础运行环境实机通过之后实现。固件异常时只能冷启动。原先 DMA 接管的限制仍适用，不允许其他 USB 查询。

参考：ARMv7-A/R Architecture Reference Manual (DDI0406)，Linux arch/arm/mm/cache-v7.S，SDK 的 OpenBoot 已验证硬件参数。独立实现，并未复制原厂源码。

当前版本为实验代码，必须逐项编译并验证实机。不要将预期日志当作已通过的测试。
