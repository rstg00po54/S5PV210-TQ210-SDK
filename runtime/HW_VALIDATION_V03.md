# Runtime v0.3 实机验证

用户在提交 9814c59 的安全版运行 autotest，前九项回归及新增抢占式上下文切换、调度后内存测试全部通过。

    A stack top=0x24015038
    B stack top=0x24016038
    Ticks: 12
    SCHED ticks=0x0000000C
    A slices=0x00000004
    B slices=0x00000004
    A work=0x0009C1A3
    B work=0x0009C180
    SCHED PASS: preemptive contexts, stacks and register guards
    [AUTO] PASS: Preemptive thread context switch
    [AUTO] PASS: Post-scheduler DDR memory self-test
    RESULT: PASS
    Passed: 11  Skipped: 0  Failed: 0

12 次 Timer1 IRQ 触发三线程轮转，A/B 各完成 4 个时间片，分别执行 639395/639360 次计数循环；被测试的寄存器哨兵、栈指针及保护字通过，Shell 恢复正常。实际时钟频率尚未校准。不是所有寄存器、任意线程代码、长期稳定性或用户态隔离的完整证明。v0.4 复用已验证的汇编上下文切换，不改 OpenBoot。
