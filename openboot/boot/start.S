.syntax unified
.cpu cortex-a8
.arm
.section .text.entry,"ax",%progbits
.global _start
_start:
    b reset
.section .vectors,"ax",%progbits
.balign 32
.global vectors
vectors:
    b reset
    b trap
    b trap
    b trap
    b trap
    b trap
    b trap
    b trap
.section .text.start,"ax",%progbits
reset:
    cpsid if
#ifdef ROM_CLOCKS
    /* Emit a stackless marker using the validated ROM-clock UART setup. */
    ldr r0, =0xe010046c
    ldr r1, [r0]
    orr r1, r1, #(1 << 22)
    orr r1, r1, #(1 << 17)
    str r1, [r0]
    dsb sy
    ldr r0, =0xe2700000
    mov r1, #0
    str r1, [r0]
    ldr r0, =0xe0200000
    ldr r1, [r0]
    bic r1, r1, #0xff
    orr r1, r1, #0x22
    str r1, [r0]
    dsb sy
    ldr r4, =0xe2900000
    mov r1, #0
    str r1, [r4, #4]
    str r1, [r4, #8]
    str r1, [r4, #12]
    str r1, [r4, #0x2c]
    mov r1, #3
    str r1, [r4]
    mov r1, #35
    str r1, [r4, #0x28]
    mov r1, #5
    str r1, [r4, #4]
    ldr r2, =0x100000
7:  ldr r1, [r4, #0x10]
    tst r1, #2
    bne 8f
    subs r2, r2, #1
    bne 7b
    b 9f
8:  mov r1, #0x3e
    strb r1, [r4, #0x20]
9:
#endif
    cps #0x13
    ldr sp, =__stack_top
    /* ROM handoff must be physical, uncached for data. Do not turn off an
       unknown live MMU/cache mapping. Fail closed if that assumption is false. */
    mrc p15, 0, r0, c1, c0, 0
    tst r0, #5
    bne halt
    bic r0, r0, #(1 << 13)
    mcr p15, 0, r0, c1, c0, 0
    ldr r0, =vectors
    mcr p15, 0, r0, c12, c0, 0
    dsb sy
    isb
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl boot_main
halt:
    b halt
trap:
    cpsid if
    cps #0x13
    ldr sp, =__stack_top
    bl boot_fault
    b halt
.ltorg
/* This position-independent routine is copied to tested DDR and called. */
.section .text.ddr_stub,"ax",%progbits
.global ddr_stub_begin, ddr_stub_end
ddr_stub_begin:
    movw r0, #0x2100
    movt r0, #0x5a5a
    bx lr
ddr_stub_end:
.section .note.GNU-stack,"",%progbits
