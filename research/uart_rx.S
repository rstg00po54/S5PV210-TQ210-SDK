.syntax unified
.cpu cortex-a8
.arm
.section .text.start,"ax"
.global _start
_start:
    cpsid if
    cps #0x13
    ldr r0, =0xe2700000
    mov r1, #0
    str r1, [r0]
    /* Keep known-working BL2 clock setup and UART0 configuration. */
    ldr r0, =0xe010046c
    ldr r1, [r0]
    orr r1, r1, #0x60000
    str r1, [r0]
    ldr r0, =0xe0200000
    ldr r1, [r0]
    bic r1, r1, #0xff
    orr r1, r1, #0x22
    str r1, [r0]
    dsb
    ldr r4, =0xe2900000
    mov r1, #0
    str r1, [r4, #4]
    str r1, [r4, #8]
    str r1, [r4, #12]
    mov r1, #3
    str r1, [r4]
    mov r1, #35
    str r1, [r4, #0x28]
    mov r1, #1
    str r1, [r4, #0x2c]
    mov r1, #5
    str r1, [r4, #4]
    adr r6, greeting
    bl print
rx_wait:
    ldr r2, [r4, #0x10]
    tst r2, #1
    beq rx_wait
    ldr r9, [r4, #0x14]   /* UERSTAT before reading URXH */
    ldrb r8, [r4, #0x24]
    adr r6, prefix
    bl print
    mov r0, r8
    bl hexbyte
    adr r6, errprefix
    bl print
    and r0, r9, #15
    bl hexbyte
    adr r6, newline
    bl print
    b rx_wait

/* No stack: each non-leaf routine saves LR in a dedicated register. */
print:
    mov r10, lr
1:  ldrb r0, [r6], #1
    cmp r0, #0
    bxeq r10
    bl putc
    b 1b

hexbyte:
    mov r11, lr
    mov r7, r0
    adr r5, digits
    lsr r1, r7, #4
    ldrb r0, [r5, r1]
    bl putc
    and r1, r7, #15
    ldrb r0, [r5, r1]
    bl putc
    bx r11

putc:
    ldr r2, [r4, #0x10]
    tst r2, #2
    beq putc
    strb r0, [r4, #0x20]
    bx lr

greeting: .asciz "UART0 RX READY - send one byte\r\n"
prefix: .asciz "RX 0x"
errprefix: .asciz " ERR 0x"
newline: .asciz "\r\n"
digits: .ascii "0123456789ABCDEF"
.balign 4
.ltorg
.section .note.GNU-stack,"",%progbits
