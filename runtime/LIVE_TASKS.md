# Runtime v0.5 常驻后台线程实验

安全版先执行 autotest；预期 15/15 PASS，新增连续调度至少 60 tick（跨越原来的 48 tick 上限）、栈回收及内存回归。也可以正常版启动后直接使用：

    tasks start
    ps
    tasks stop

启动两个内置 C 线程，分别使用 5/7 tick 休眠间隔，持续增加工作计数；Shell 本身也作为可运行线程。ps 是短 IRQ 临界区内取得的状态快照，打印时恢复 IRQ。等待后再次 ps，观察 tick、work、wakes 增长。tasks stop 在 Shell 屏蔽 IRQ 后停止 Timer1、回收后台栈。若需要重复，可再次启动。没有外部时长上限。自动测试验证至少 60 tick、两线程唤醒及工作量、停止前后页数；不等于交互或长期压力测试。

常驻期间 Shell 只允许 help/ps/tasks stop/ticks/meminfo/cpuinfo/mmu，禁止其他 Timer 和测试命令，以免冲突。工作线程禁止串口、内存分配及共享可变资源操作；暂不支持任意程序、运行中创建、锁或用户态。UART 仍为轮询，Shell 被抢占时可能丢失快速粘贴的字符，建议逐条键入。若异常导致调度停止，可在 Shell 输入 tasks stop 回收；硬异常或无法返回 Shell 仍需冷启动。IRQ 汇编入口和 OpenBoot 不变。新增代码尚未完成交叉编译或实机验证。
