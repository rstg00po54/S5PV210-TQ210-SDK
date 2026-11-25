.syntax unified
.cpu cortex-a8
.arm
.section .text.entry,"ax",%progbits
.global _start
_start:
    cpsid if
    adr r0, _start
    ldr r1, =0x24000000
    cmp r0, r1
    adreq r2, good
    adrne r2, bad
    ldr r3, =0xe2900000
1:  ldrb r0, [r2], #1
    cmp r0, #0
    beq 3f
    ldr r4, =0x100000
2:  ldr r1, [r3,#0x10]
    tst r1,#2
    bne 4f
    subs r4,r4,#1
    bne 2b
    b 3f
4:  strb r0,[r3,#0x20]
    b 1b
3:  b 3b
good: .asciz "\r\nDDR APP EXEC PASS: independent ARM image at 0x24000000\r\n"
bad: .asciz "\r\nDDR APP EXEC FAIL: wrong entry address\r\n"
.balign 4
.ltorg
.section .note.GNU-stack,"",%progbits
