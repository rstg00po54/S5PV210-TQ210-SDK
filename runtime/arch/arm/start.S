.syntax unified
.cpu cortex-a8
.arm

.section .text.entry,"ax",%progbits
.global _start
_start:
    cpsid if
    /* The loader guarantees ARM SVC, little endian, M=C=0. */
    mrc p15,0,r0,c1,c0,0
    tst r0,#5
    bne bad_entry
    mrs r0,cpsr
    and r0,r0,#0x1f
    cmp r0,#0x13
    bne bad_entry

    cps #0x11
    ldr sp,=__fiq_stack_top
    cps #0x12
    ldr sp,=__irq_stack_top
    cps #0x17
    ldr sp,=__abt_stack_top
    cps #0x1b
    ldr sp,=__und_stack_top
    cps #0x1f
    ldr sp,=__sys_stack_top
    cps #0x13
    ldr sp,=__svc_stack_top

    /* Low vectors, ARM exception entry, little-endian exceptions. */
    mrc p15,0,r0,c1,c0,0
    bic r0,r0,#(1 << 13)
    bic r0,r0,#(1 << 25)
    bic r0,r0,#(1 << 30)
    mcr p15,0,r0,c1,c0,0
    isb
    ldr r0,=vectors
    mcr p15,0,r0,c12,c0,0
    isb

    ldr r0,=__bss_start
    ldr r1,=__bss_end
    mov r2,#0
1:  cmp r0,r1
    strlo r2,[r0],#4
    blo 1b
    dsb sy
    bl runtime_main

bad_entry:
    /* No stack and no MMIO when the entry ABI is wrong. */
    b bad_entry
.ltorg

.section .text.vectors,"ax",%progbits
.balign 32
.global vectors
vectors:
    b _start
    b fault_undef
    b fault_svc
    b fault_pabort
    b fault_dabort
    b fault_reserved
    b irq_entry
    b fault_fiq

/* Non-IRQ exceptions remain fatal. */
.macro FAULT name, number, correction
\name:
    cpsid if
    mov r0,#\number
    sub r1,lr,#\correction
    mrs r2,spsr
    b runtime_fault
.endm
FAULT fault_undef,1,4
FAULT fault_svc,2,4
FAULT fault_pabort,3,4
FAULT fault_dabort,4,8
FAULT fault_reserved,5,0
FAULT fault_fiq,7,4
/* IRQ 入口及上下文切换独立实现于 context.S。 */
.section .note.GNU-stack,"",%progbits
