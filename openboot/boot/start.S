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
