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
    mrs r8, cpsr
    mov r9, sp
    cpsid if
    /* Exact gate, pinmux and UART setup used by the confirmed entry probe. */
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
    mov r1, #35
    str r1, [r4, #0x28]
    mov r1, #0
    str r1, [r4, #0x2c]
    mov r1, #5
    str r1, [r4, #4]
    ldr r6, =banner
    bl print
    mov r7, r8
    ldr r6, =cpsr_name
    bl field
    mov r7, r9
    ldr r6, =sp_name
    bl field
report:
    ldr r6, =cp15_name
    bl print
    mrc p15,0,r7,c0,c0,0
    ldr r6, =midr_name
    bl field
    mrc p15,0,r7,c1,c0,0
    ldr r6, =sctlr_name
    bl field
    mrc p15,0,r7,c12,c0,0
    ldr r6, =vbar_name
    bl field
    ldr r8, =registers
1:  ldr r6, [r8], #4
    cmp r6, #0
    beq 2f
    /* Label is printed before access, so a stalled read is identifiable. */
    bl print
    ldr r0, [r8], #4
    ldr r7, [r0]
    bl hexline
    b 1b
2:  ldr r6, =done
    bl print
    /* Repeat on any received byte. No changes to the clock tree or DMC. */
3:  ldr r0, [r4, #0x10]
    tst r0, #1
    beq 3b
    ldrb r0, [r4, #0x24]
    b report
/* field / print / hexline / tx use dedicated LR saves; no stack. */
field:
    mov r11, lr
    bl print
    bl hexline
    bx r11
print:
    mov r10, lr
4:  ldrb r0, [r6], #1
    cmp r0, #0
    bxeq r10
    bl tx
    b 4b
hexline:
    mov r10, lr
    mov r0, #'0'
    bl tx
    mov r0, #'x'
    bl tx
    ldr r6, =digits
    mov r5, #28
5:  mov r0, r7, lsr r5
    and r0, r0, #15
    ldrb r0, [r6, r0]
    bl tx
    subs r5, r5, #4
    bpl 5b
    mov r0, #13
    bl tx
    mov r0, #10
    bl tx
    bx r10
tx:
    ldr r2, =0x100000
6:  ldr r1, [r4, #0x10]
    tst r1, #2
    bne 7f
    subs r2, r2, #1
    bne 6b
    bx lr
7:  strb r0, [r4, #0x20]
    bx lr
.balign 4
registers:
    .word apll, 0xe0100100, mpll, 0xe0100108
    .word epll, 0xe0100110, vpll, 0xe0100120
    .word src0, 0xe0100200, src1, 0xe0100204
    .word src4, 0xe0100210, src6, 0xe0100218
    .word div0, 0xe0100300, div1, 0xe0100304
    .word div4, 0xe0100310, div6, 0xe0100318
    .word divstat0, 0xe0101000, divstat1, 0xe0101004
    .word gate0, 0xe0100400, gate1, 0xe0100404
    .word 0
banner: .asciz "\r\nOPENBOOT ROM STATE v0.2 UART DIV=0x23\r\n"
cpsr_name: .asciz "ENTRY_CPSR="
sp_name: .asciz "ENTRY_SP="
cp15_name: .asciz "CP15 SNAPSHOT\r\n"
midr_name: .asciz "MIDR="
sctlr_name: .asciz "SCTLR="
vbar_name: .asciz "VBAR="
apll: .asciz "APLL_CON0="
mpll: .asciz "MPLL_CON="
epll: .asciz "EPLL_CON0="
vpll: .asciz "VPLL_CON="
src0: .asciz "CLK_SRC0="
src1: .asciz "CLK_SRC1="
src4: .asciz "CLK_SRC4="
src6: .asciz "CLK_SRC6="
div0: .asciz "CLK_DIV0="
div1: .asciz "CLK_DIV1="
div4: .asciz "CLK_DIV4="
div6: .asciz "CLK_DIV6="
divstat0: .asciz "CLK_DIV_STAT0="
divstat1: .asciz "CLK_DIV_STAT1="
gate0: .asciz "CLK_GATE_0400="
gate1: .asciz "CLK_GATE_0404="
done: .asciz "ROM STATE DONE; send any byte to repeat snapshot\r\n"
digits: .ascii "0123456789ABCDEF"
.balign 4
.ltorg
