# Runtime v0.2 自动测试实机记录（2026-11-25）

用户在安全版启动后执行 autotest，前三种 PMU 测试均零错误，MMU 与 D-Cache 启用通过，DDR 页分配/内存自测通过。最后 PWM Timer0 因旧驱动对其他通道运行状态的保护检查而提前退出，尚未验证实际 Timer0 计数或 IRQ。

| 状态 | WRITE_CYCLES | READ_VERIFY_CYCLES | 16K_READ_CYCLES | ERRORS |
| --- | --- | --- | --- | --- |
| MMU OFF / D-Cache OFF | 0x01376B74 | 0x0169C458 | 0x00057E24 | 0 |
| MMU ON / D-Cache OFF | 0x006001DA | 0x014C6FB2 | 0x00050F5D | 0 |
| MMU ON / D-Cache ON | 0x00551FD1 | 0x00556A8F | 0x0000FAC8 | 0 |

以标称 400 MHz、PMU 全速计数估算，写入分别约 19.60 / 63.57 / 71.70 MiB/s，读取校验约 16.87 / 18.36 / 71.46 MiB/s。写入包含缓存维护，读取包含比较；不等于裸 DDR 峰值带宽。实际 CPU 频率尚未外部校准。

原始失败信息：

    [AUTO] PWM Timer0 polling probe (no IRQ enabled)
    Other PWM timers running; refusing shared prescaler change.
    [AUTO] FAIL: PWM Timer0 polling probe (no IRQ enabled)
    RESULT: FAIL
    Passed: 6  Skipped: 0  Failed: 1

修订：Timer0/1 共享 prescaler0，Timer2/3/4 共享 prescaler1。保留两个预分频值、其他 timer 控制位、其他 timer 中断使能及 W1C 状态。Timer0 已运行则不接管。独立 Timer0 mux 采用 S5PV210 /1，计算重载并记录继承寄存器。轮询时 CPU IRQ 屏蔽，不启用 VIC 线路，结束时停止 Timer0。仍需实机确认。参考原厂 Linux 3.0.8 arch/arm/plat-samsung/include/plat/regs-timer.h 和 pwm-clock.c。

## 第二轮实机测试与 Timer1 选择（2026-11-25）

提交 dcefec1 的安全版 0x2AA4 字节、CRC32 0xCAD6510F，经 OpenBoot DMA 验证后执行。autotest 前六项再次通过，三种 PMU 测试均零数据错误。

| 状态 | WRITE_CYCLES | READ_VERIFY_CYCLES | 16K_READ_CYCLES |
| --- | --- | --- | --- |
| MMU OFF / D-Cache OFF | 0x01376B98 | 0x0169C488 | 0x00057E42 |
| MMU ON / D-Cache OFF | 0x006001E3 | 0x014C6FEB | 0x00050EE5 |
| MMU ON / D-Cache ON | 0x00551F92 | 0x00556A8F | 0x0000FC15 |

原始日志：

    TCON inherited=0x00108009
    TCFG0 inherited=0x00004100
    TCFG1 inherited=0x00000000
    TINT_CSTAT inherited=0x00000230
    Timer0 already running; refusing takeover.
    RESULT: FAIL
    Passed: 6  Skipped: 0  Failed: 1

TCON 的 Timer0 start/reload、Timer2 reload（未启动）、Timer4 start 置位；Timer1 start 位未置位。TCFG0 表示 Timer0/1 预分频为 1，Timer2/3/4 为 66。TINT_CSTAT 表示 Timer4 IRQ enable、Timer0/4 pending；不确定继承状态的初始化者。依据原厂 regs-timer.h 及 int.h，改用 Timer1，独立 TCNTB1=0x18、TCMPB1=0x1c、TCNTO1=0x20；TCON 位8–11，TCFG1 位4–7，TINT_CSTAT enable bit1/pending bit6，VIC0 input22。保留共享分频及其他通道的控制和 pending。名义 100 Hz 重载 666667（0x000A2C2B，实际频率未校准）。仅选择 Timer1，不擅自停止 Timer0；轮询后停止 Timer1，手动 IRQ 实验仍独占 VIC，不保证继承 IRQ 配置不变。尚未实机验证 Timer1。
