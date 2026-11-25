.syntax unified
.cpu cortex-a8
.arm
.section .text.irq,"ax",%progbits
.global irq_entry
.type irq_entry,%function
irq_entry:
    /* IRQ 栈：完整保存被打断线程的整数上下文（72 字节，8 字节对齐）。 */
    sub sp,sp,#72
    stmia sp,{r0-r12}
    sub r1,lr,#4
    str r1,[sp,#60]
    mrs r1,spsr
    str r1,[sp,#64]
    mov r2,#0
    str r2,[sp,#68]
    /* 目前所有线程均为 SVC；其他模式不允许进入调度路径。 */
    and r2,r1,#0x1f
    cmp r2,#0x13
    bne irq_bad_mode
    mov r0,sp
    cps #0x13
    mov r1,sp
    mov r2,lr
    cps #0x12
    str r1,[r0,#52]
    str r2,[r0,#56]

    /* 先确认、清除硬件中断，再由调度器选择返回到哪个线程。 */
    bl irq_dispatch
    mov r0,sp
    bl scheduler_on_irq

    /* r0 指向所选上下文：恢复 SVC 的独立 SP/LR，再恢复异常状态。 */
    ldr r1,[r0,#52]
    ldr r2,[r0,#56]
    cps #0x13
    mov sp,r1
    mov lr,r2
    cps #0x12
    ldr r1,[r0,#64]
    msr spsr_cxsf,r1
    ldr lr,[r0,#60]
    add sp,sp,#72
    ldmia r0,{r0-r12}
    movs pc,lr
irq_bad_mode:
    mov r2,r1
    ldr r1,[sp,#60]
    mov r0,#6
    b runtime_fault
.size irq_entry,.-irq_entry

.section .text.worker,"ax",%progbits
.global scheduler_worker
.global scheduler_worker_end
.type scheduler_worker,%function
scheduler_worker:
    /* 两个测试线程执行相同代码、不同栈及计数器。r2-r12 是哨兵。
       不调用 yield，不主动切换，完全依赖 Timer1 抢占。 */
1:  push {r2,r3}
    ldr r1,[r0]
    add r1,r1,#1
    str r1,[r0]
    pop {r2,r3}
    b 1b
scheduler_worker_end:
.size scheduler_worker,.-scheduler_worker
.section .note.GNU-stack,"",%progbits
