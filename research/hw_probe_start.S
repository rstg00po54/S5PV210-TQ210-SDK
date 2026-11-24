.syntax unified
.cpu cortex-a8
.arm
.section .text.start,"ax"
.global _start
_start:
    cpsid if
    cps #0x13
    ldr sp, =__stack_top
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl probe_loop
2:  b 2b
.ltorg
.section .note.GNU-stack,"",%progbits
