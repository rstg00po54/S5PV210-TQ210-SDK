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
    /* Preserve the vendor BL2 clock/DDR configuration. */
    ldr r0, =0xe010046c
    ldr r1, [r0]
    orr r1, r1, #0x60000
    str r1, [r0]
    ldr r0, =0xe0200000
    ldr r1, [r0]
    ldr r2, =0x00ff00ff
    bic r1, r1, r2
    ldr r2, =0x00220022
    orr r1, r1, r2
    str r1, [r0]
    dsb
    ldr r4, =0xe2900000
    ldr r5, =0xe2900400
    mov r0, r4
    bl init_uart
    mov r0, r5
    bl init_uart
repeat:
    mov r0, r4
    adr r6, msg0
    bl send_string
    mov r0, r5
    adr r6, msg1
    bl send_string
    ldr r7, =10000000
delay:
    subs r7, r7, #1
    bne delay
    b repeat

init_uart:
    mov r1, #0
    str r1, [r0, #4]
    str r1, [r0, #8]
    str r1, [r0, #12]
    mov r1, #3
    str r1, [r0]
    mov r1, #35
    str r1, [r0, #0x28]
    mov r1, #1
    str r1, [r0, #0x2c]
    mov r1, #5
    str r1, [r0, #4]
    bx lr

send_string:
    ldrb r1, [r6], #1
    cmp r1, #0
    bxeq lr
    ldr r2, =100000
wait_tx:
    ldr r3, [r0, #0x10]
    tst r3, #2
    bne tx
    subs r2, r2, #1
    bne wait_tx
    bx lr
tx:
    strb r1, [r0, #0x20]
    b send_string
msg0: .asciz "UART0 DDR TEST UUUUUUUU\r\n"
msg1: .asciz "UART1 DDR TEST UUUUUUUU\r\n"
.balign 4
.ltorg
.section .note.GNU-stack,"",%progbits
