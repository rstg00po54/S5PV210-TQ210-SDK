# Runtime v0.4 实机验证（提交 32acee5）

用户在安全版执行 autotest，13/13 PASS。旧 11 项全部通过，C 线程生命周期与后续内存回归通过。

    A stack base=0x20100000
    B stack base=0x20102000
    Ticks: 17
    Task A
      dispatches=0x00000003
      work=0x00004E20
      sleeps=0x00000002
      wakes=0x00000002
      errors=0x00000000
    Task B
      dispatches=0x00000003
      work=0x00004E20
      sleeps=0x00000002
      wakes=0x00000002
      errors=0x00000000
    Sleeping slots skipped=0x0000000C
    Free pages restored=0x0003EE00
    TASK PASS: create, sleep, wake, exit, stack reclaim
    [AUTO] PASS: Post-task DDR memory self-test
    RESULT: PASS
    Passed: 13  Skipped: 0  Failed: 0

两个普通 C 线程各有 8 KiB 动态栈、20000 次工作循环、2 次休眠与唤醒、0 错误，结束后空闲页恢复 257536。17 个 tick 是硬件计数而非精确校准时间。仅证实有界实验，不代表通用线程 API、常驻调度或长期稳定性均已验证。
