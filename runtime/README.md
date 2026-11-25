# TQ210 Runtime v0.2 — 自研 DDR 系统运行环境

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

- arch/arm/start.S：独立异常向量、SVC/IRQ/FIQ/ABT/UND/SYS 栈、BSS 清零；非 IRQ 异常报告后停止；IRQ 通过独立汇编入口保存易失寄存器并返回。
- arch/arm/runtime.ld：独立 DDR 链接布局、向量对齐及边界断言。
- arch/arm/cpu.c：CP15、时钟及异常诊断。
- mm/mmu.c：ARMv7 short descriptor 一级页表（16 KiB 对齐、1 MiB section），DDR 恒等映射，Normal WBWA、特权访问；首个镜像 section 可执行，其他 DDR XN；仅映射必要 MMIO，Strongly-ordered/XN，未映射地址触发异常。域 0 为 client。启用前按 CLIDR/CCSIDR 遍历并失效数据缓存，TLB/I-cache/分支预测器失效；页表暂为静态。
- mm/pages.c：4 KiB 物理页位图、连续分配与释放、小范围双模式测试；测试在 Cache 开启时 clean+invalidate 后读回。
- drivers/uart.c：沿用 OpenBoot 已验证 UART 设置，轮询输入输出。
- kernel/main.c、shell/shell.c：分阶段初始化及交互命令。

## 限制

这不是完整虚拟内存管理或多任务 OS：无用户态、进程隔离、二级页表、IRQ 调度；v0.2 新增实验性定时器 IRQ，仍无调度。页分配器仅供可信单线程内核调用，释放须使用原始分配边界，尚无所有权表。Shell 仅允许一个显式分配；memtest 使用自身临时页，不覆盖系统或整片 DDR。MMU/Cache 不支持动态关闭，避免未经 clean 的脏数据丢失。没有重启驱动或任意地址读写命令。固件异常时只能冷启动。原先 DMA 接管的限制仍适用，不允许其他 USB 查询。

参考：ARMv7-A/R Architecture Reference Manual (DDI0406)，Linux arch/arm/mm/cache-v7.S，SDK 的 OpenBoot 已验证硬件参数。独立实现，并未复制原厂源码。

当前版本为实验代码，必须逐项编译并验证实机。不要将预期日志当作已通过的测试。

## 本地构建验证（2026-11-25）

已通过本地 Clang/LLD 完整构建两个固件。11 个构建输入的 Git blob SHA 与远程源码逐一一致。

| 检查项 | 结果 |
| --- | --- |
| runtime.bin | 6720 字节 |
| runtime-safe.bin | 6532 字节 |
| ELF 入口 | 0x24000000 |
| VBAR 向量地址 | 0x240000c0（32 字节对齐） |
| 一级页表 | 0x24004000（16 KiB 对齐） |
| 运行环境结束地址 | 0x24014810 |
| 未解析符号 | 无 |
| OBL2 镜像长度限制 | 通过 |

物理页分配器在 Linux 主机上使用模拟映射内存，通过初始页数、分配/释放、拒绝保留区与重复释放、跨保留区查找及两种数据模式测试：

    sh runtime/test-host.sh

主机测试的 Cache 同步函数为空实现，只验证分配逻辑和软件读写，不代表目标 CPU、DDR、MMU 或 Cache 实机成功。GNU ARM 工具链构建及目标板验证仍待进行。

## v0.2：PMU、PWM Timer0 与 VIC 中断实验

v0.1 实机结果见 [HW_VALIDATION_V01.md](HW_VALIDATION_V01.md)。本版保留启动、DDR、物理页分配器和时钟参数，新增实验性 PMU 及单一 IRQ 源，未实现任务调度。上板前重新构建安全版。启动后依次：

    autotest

autotest 自动执行 PMU 基线、开启 MMU、MMU-only 性能测试、开启 D-Cache、Cache 性能测试、页分配器/DDR 测试和 Timer0 轮询探测；遇到失败立即停止并汇总。不会自动启动持续中断。若从 runtime-safe 启动，完整测试三种状态；若从自动开启 MMU/Cache 的 runtime 启动，无法回测关闭状态，会明确将相应阶段标记为 SKIP。若需要完整对比，请冷启动并加载 runtime-safe.bin。

benchmark 分配自身 1 MiB 临时页，使用 PMU CCNT 全速周期计数，不包含串口输出。写入计时包含 clean/invalidate（Cache 开启时），读计时包含数据校验；16 KiB 读取为附加测量，不保证热缓存。报告十六进制周期数和错误数，不是纯 DDR 峰值带宽。三种状态应在同次安全版启动中比较。测量前必须停止 timer。PMU 溢出时结果无效。

timer probe 仅在 MMU、D-Cache 已打开且 ROM 时钟配置匹配时运行，开启 PWM 门控（CLK_GATE_IP3 bit23），Timer0 输入使用名义 PCLK_PSYS 66.667 MHz、继承预分频器 0 的实际设置、独立分频 1，按该预分频值计算约 100 Hz 重载（无除法库依赖），实际频率未校准。保持 TCFG0 和其他定时器控制字段不变；若 Timer0 本身已运行则拒绝接管。先在 CPU IRQ 屏蔽、不使能新 VIC 线路的情况下使能 Timer0 外设中断源并轮询三次 pending，验证 W1C 清除；结束时关闭 Timer0 及其外设中断，保留其他通道中断使能。若轮询失败不要启用 IRQ。timer on 仅在 probe 通过后运行，独占四个 VIC，屏蔽并清除其他源，配置 VIC0 input 21，启用 Timer0。IRQ 入口保存 r0-r3、r12、lr，C 分发器确认来源，清除 PWM W1C pending 后写 VIC EOI，返回时恢复 SPSR。FIQ 保持屏蔽。未知 IRQ 停机。ticks 输出累计 32 位计数；间隔约 1 秒应增加约 100（需要实际测量）。timer off 屏蔽 IRQ 并停止 Timer0，保留计数。仅供独立实验，不与其他 PWM/VIC 驱动共存。

参考原厂 `bare-metal/no-os-examples/src/16.timer/{timer.c,int.c,int.h}` 和 Linux 3.0.8 的 `arch/arm/mach-s5pv210/clock.c` 中 timers 时钟门控。v0.2 尚未完成交叉编译、实机验证。v0.1 本地构建记录适用于旧提交而非本版。OpenBoot 不变。

## v0.2 自动测试实机结果与 Timer0 修订

[HW_VALIDATION_V02.md](HW_VALIDATION_V02.md) 记录 6 项通过、Timer0 保护拒绝的首轮测试。旧驱动误将 Timer1–4 任一运行状态视为共享分频冲突；本次修订继承原预分频值、打印原始 TCON/TCFG0/TCFG1/TINT_CSTAT，只配置 Timer0，保留其他通道中断使能和 pending 位。若 Timer0 自身已启动，仍停止并报告，不能仅凭其他通道运行就确定其来源。新修订尚未在板上验证；如失败请提供完整日志。定时器 IRQ 测试仍需独立执行，autotest 不会自动打开 CPU IRQ。
