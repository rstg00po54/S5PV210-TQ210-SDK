.syntax unified
.cpu cortex-a8
.arm
.section .text,"ax",%progbits
.global _start
_start:
    b entry
    .rept 7
    b .
    .endr
entry:
    /* Match original BL2's first branch to offset 0x20. No stack, CP15,
       C runtime, global clock changes, DDR, NAND, or SD. */
    cpsid if
    ldr r0, =0xe010046c
    ldr r1, [r0]
    orr r1, r1, #(1 << 22)
    orr r1, r1, #(1 << 17)
    str r1, [r0]
    dsb sy
    ldr r0, =0xe2700000
    mov r1, #0
    str r1, [r0]
    ldr r4, =0xe0200000
#ifdef GPIO_ONLY
    /* GPA0[1] is UART0 TX. GPIO output only; RX untouched. */
    ldr r1, [r4, #4]
    orr r1, r1, #2
    str r1, [r4, #4]
    ldr r1, [r4]
    bic r1, r1, #0xf0
    orr r1, r1, #0x10
    str r1, [r4]
1:  ldr r1, [r4, #4]
    eor r1, r1, #2
    str r1, [r4, #4]
    dsb sy
    ldr r2, =0x100000
2:  subs r2, r2, #1
    bne 2b
    b 1b
#else
    ldr r1, [r4]
    bic r1, r1, #0xff
    orr r1, r1, #0x22
    str r1, [r4]
    dsb sy
    ldr r4, =0xe2900000
    mov r1, #0
    str r1, [r4, #4]
    str r1, [r4, #8]
    str r1, [r4, #12]
    mov r1, #3
    str r1, [r4]
    mov r1, #5
    str r1, [r4, #4]
    /* Retain ROM PCLK. Cycle plausible divisors instead of assuming 66 MHz.
       Each message identifies its divisor; fractional slot intentionally 0. */
3:  mov r5, #0
4:  adr r6, divisors
    ldrb r7, [r6, r5]
    cmp r7, #255
    beq 3b
    ldr r2, =0x100000
5:  ldr r1, [r4, #0x10]
    tst r1, #4
    bne 6f
    subs r2, r2, #1
    bne 5b
6:  str r7, [r4, #0x28]
    mov r1, #0
    str r1, [r4, #0x2c]
    mov r8, #16
7:  adr r6, message
8:  ldrb r0, [r6], #1
    cmp r0, #0
    beq 9f
    bl tx
    b 8b
9:  adr r6, digits
    lsr r1, r7, #4
    ldrb r0, [r6, r1]
    bl tx
    and r1, r7, #15
    ldrb r0, [r6, r1]
    bl tx
    mov r0, #13
    bl tx
    mov r0, #10
    bl tx
    subs r8, r8, #1
    bne 7b
    add r5, r5, #1
    b 4b
tx:
    ldr r2, =0x100000
10: ldr r1, [r4, #0x10]
    tst r1, #2
    bne 11f
    subs r2, r2, #1
    bne 10b
    bx lr
11: strb r0, [r4, #0x20]
    bx lr
message: .asciz "OPENBOOT ENTRY UART0 DIV=0x"
digits: .ascii "0123456789ABCDEF"
divisors: .byte 12,35,6,3,25,26,53,71,89,17,8,5,2,44,107,255
#endif
.balign 4
.ltorg
