# v0.4 C 线程生命周期实验

## 新功能
- thread_create(entry, arg)：在实验启动前创建普通 C 入口线程，从物理页分配器获得独立 8 KiB 栈。
- thread_sleep(ticks)：在短 IRQ 临界区内设置睡眠状态与唤醒 tick，下一次 Timer1 IRQ 换出线程；轮转跳过未到期的睡眠线程。
- thread_exit()：标记退出，等待下次 IRQ 换出。C 入口正常 return 时自动调用。
- 回收：仅在 Shell 恢复并关闭定时器后回收线程栈，避免释放当前正在使用的栈。

状态：UNUSED -> READY -> RUNNING -> SLEEPING -> READY；RUNNING -> EXITED -> UNUSED。
Shell 始终可运行，最多 3 个工作线程槽。本实验创建 2 个普通 C 线程，各进行 20000 次工作循环、两次 sleep（A 5 tick，B 7 tick）、检查跨睡眠的局部数组并正常返回。检查实际唤醒次数、跳过睡眠槽、栈边界及保护字、回收前后页数。上限 48 个 IRQ，正常完成提前回 Shell。保留旧的 12 tick 汇编哨兵测试。

## 测试
安全版冷启动输入 autotest，预期 13/13 PASS。也可在 MMU 和 D-Cache 开启、Timer 停止时输入 tasktest。期待 TASK PASS: create, sleep, wake, exit, stack reclaim。初始空闲页 257536，无其他分配时应恢复为 257536。

## 限制
这是有界生命周期实验，不是常驻多任务 OS。创建只允许在实验启动前；sleep 和 exit 等待下一个 tick 才换出，不立即主动切换。只支持 SVC 整数上下文，未实现用户态、VFP/NEON、互斥锁、通用运行中的创建或线程安全分配器。工作线程不能并发访问串口或页分配器。未校准 tick 时长。严重异常或 CPU 卡在工作线程中仍需冷启动。新增代码尚未交叉编译和实机验证。
