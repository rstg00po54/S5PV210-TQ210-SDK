/* SPDX-License-Identifier: GPL-2.0+ */
/* Read-only SRAM NAND bootstrap, BCH8 register sequence from TQ210 U-Boot. */
#include "openboot/platform.h"
#ifdef NAND_BOOT
#define NF 0xb0e00000u
#define BCH 0xb0e20000u
#define SEL 0x85u
#define OFF 0x87u
#define BIT(n) (1u << (n))
#define PAGE 2048u
#define SLOT_PAGE 512u
#define MAX_IMAGE 0x100000u
#define MAGIC 0x324c4254u /* TBL2 */

static void delay(void)
{
    for (volatile unsigned i = 0; i < 1000; i++)
        __asm__ volatile("nop");
}

static void select_nand(unsigned v)
{
    mmio_write32(NF+4, v);
}

static void column(unsigned col)
{
    MMIO8(NF+12) = col;
    MMIO8(NF+12) = col >> 8;
}

static int begin_page(unsigned p, unsigned col)
{
    select_nand(SEL);
    MMIO8(NF+8) = 0;
    column(col);
    MMIO8(NF+12) = p;
    MMIO8(NF+12) = p >> 8;
    MMIO8(NF+12) = p >> 16;
    MMIO8(NF+8) = 0x30;
    delay();
    return mmio_wait_mask(NF+0x28, 1, 1);
}

static int good_block(unsigned p)
{
    for (unsigned i = 0; i < 2; i++) {
        if (!begin_page(p+i, PAGE) || MMIO8(NF+16) != 0xff) {
            select_nand(OFF);
            return 0;
        }
        select_nand(OFF);
    }
    return 1;
}

static int read_page(unsigned p, uint8_t *dst)
{
    for (unsigned s = 0; s < 4; s++) {
        uint32_t bc = MMIO32(BCH), ct = MMIO32(BCH+0x20) & ~BIT(2);
        uint32_t sec, erl[4], erp[2], seen[8], bits = 0;
        if (!begin_page(p, s*512u))
            goto fail;
        mmio_write32(BCH, (511u<<16)|3);
        mmio_write32(BCH+0x30, BIT(24));
        mmio_write32(BCH+0x20, ct & ~BIT(16));
        select_nand(SEL|BIT(5));
        mmio_write32(BCH+0x20, (ct & ~BIT(16))|BIT(2));
        select_nand(SEL & ~BIT(7));
        for (unsigned i = 0; i < 512; i++)
            dst[i] = MMIO8(NF+16);
        MMIO8(NF+8) = 0x05;
        column(PAGE+12u+s*13u);
        MMIO8(NF+8) = 0xe0;
        delay();
        for (unsigned i = 0; i < 13; i++)
            (void)MMIO8(NF+16);
        select_nand(SEL);
        int ok = mmio_wait_mask(BCH+0x30, BIT(31)|BIT(24), BIT(24));
        sec = MMIO32(BCH+0x40);
        for (unsigned i = 0; i < 4; i++)
            erl[i] = MMIO32(BCH+0xc0+4*i);
        for (unsigned i = 0; i < 2; i++)
            erp[i] = MMIO32(BCH+0xf0+4*i);
        mmio_write32(BCH, bc);
        mmio_write32(BCH+0x20, ct);
        mmio_write32(BCH+0x30, BIT(24));
        select_nand(OFF);
        unsigned count = sec & 15u;
        if (!ok || count > 8 || (sec & BIT(29)))
            goto fail;
        for (unsigned i = 0; i < count; i++) {
            unsigned pos = (erl[i/2] >> (16*(i%2))) & 1023u;
            unsigned pat = (erp[i/4] >> (8*(i%4))) & 255u;
            if (pos >= 525 || !pat)
                goto fail;
            for (unsigned j = 0; j < i; j++)
                if (seen[j] == pos)
                    goto fail;
            seen[i] = pos;
            for (unsigned x = pat; x; x >>= 1)
                bits += x & 1u;
            if (bits > 8)
                goto fail;
            if (pos < 512)
                dst[pos] ^= pat;
        }
        dst += 512;
    }
    return 1;
fail:
    select_nand(OFF);
    console_reg("NAND BCH/read failure page=", p);
    return 0;
}

static int nand_init(void)
{
    static const uint8_t id[] = {0x01,0xd3,0xd1,0x95,0x58};
    mmio_write32(0xe0100464u, MMIO32(0xe0100464u)|BIT(28));
    mmio_write32(0xe0200320u, 0x22222222u);
    mmio_write32(0xe0200380u, 0x22222222u);
    mmio_write32(0xe02002e0u, (MMIO32(0xe02002e0u)&~(15u<<8))|(3u<<8));
    select_nand(OFF);
    mmio_write32(NF, 0x01807772u);
    select_nand(SEL);
    MMIO8(NF+8) = 0xff;
    delay();
    if (!mmio_wait_mask(NF+0x28, 1, 1))
        goto fail;
    MMIO8(NF+8) = 0x90;
    MMIO8(NF+12) = 0;
    delay();
    for (unsigned i = 0; i < sizeof(id); i++)
        if (MMIO8(NF+16) != id[i])
            goto fail;
    select_nand(OFF);
    console_write("NAND ID PASS\r\n");
    return 1;
fail:
    select_nand(OFF);
    return 0;
}

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

#ifndef NAND_READ_TEST
static uint32_t crc32(const uint8_t *p, unsigned n)
{
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            c = (c>>1) ^ (0xedb88320u & (0u-(c&1u)));
    }
    return ~c;
}
#endif

void nand_boot(void)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)DDR_IMAGE_BASE;
    if (!nand_init())
        boot_halt();
    /* Existing four-page BL1 is a known BCH8 read target. */
    if (!good_block(0))
        boot_halt();
    for (unsigned i = 0; i < 4; i++)
        if (!read_page(i, dst+i*PAGE))
            boot_halt();
    uint32_t sum = 0;
    for (unsigned i = 16; i < 8192; i++)
        sum += dst[i];
    if (word(dst) != 8192 || word(dst+8) != sum) {
        console_write("BL1 checksum FAIL\r\n");
        boot_halt();
    }
    console_reg("BL1 checksum=", sum);
    console_write("NAND BL1 BCH8 READ PASS\r\n");
#ifdef NAND_READ_TEST
    console_write("NAND READ TEST PASS; no write or BL2 jump\r\n");
#else
    if (!good_block(SLOT_PAGE) || !read_page(SLOT_PAGE, dst))
        boot_halt();
    uint32_t len = word(dst+4), expected = word(dst+8);
    if (word(dst) != MAGIC || !len || len > MAX_IMAGE || (len&3u) ||
        word(dst+12) != DDR_IMAGE_BASE || word(dst+16) != crc32(dst,16)) {
        console_write("BL2 header FAIL\r\n");
        boot_halt();
    }
    unsigned pages = (len+PAGE-1)/PAGE;
    for (unsigned i = 0; i < pages; i++) {
        unsigned p = SLOT_PAGE+1+i;
        if (((p&63u)==0 && !good_block(p)) || !read_page(p,dst+i*PAGE))
            boot_halt();
    }
    if (crc32(dst,len) != expected) {
        console_write("BL2 CRC FAIL\r\n");
        boot_halt();
    }
    console_write("NAND BL2 CRC PASS; entry 0x24000000\r\n");
    (void)mmio_wait_mask(S5PV210_UART0_BASE+UART_UTRSTAT,4,4);
    uint32_t zero = 0;
    __asm__ volatile("cpsid if; dsb sy; mcr p15,0,%0,c7,c5,0; mcr p15,0,%0,c7,c5,6; dsb sy; isb"::"r"(zero):"memory");
    ((void (*)(void))(uintptr_t)DDR_IMAGE_BASE)();
#endif
    boot_halt();
}
#endif
