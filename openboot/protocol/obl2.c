#include "../drivers/usb/handoff_internal.h"
#if defined(USB_HANDOFF_TEST) && !defined(SRAM_ONLY)
#ifdef USB_HANDOFF_BOOT
#define H_IMAGE_BASE DDR_IMAGE_BASE
#define H_IMAGE_LIMIT DDR_IMAGE_LIMIT
#define H_CHUNK 65536u
#define H_MAGIC 0x324c424fu /* OBL2 */
static uint32_t handoff_le32(const volatile uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}
#ifdef USB_HANDOFF_CRC_FAST
/* Generate the byte table in owned iRAM rather than growing the ROM frame.
   No MMU or data-cache change: USB DMA remains coherent. */
static uint32_t handoff_crc_table[256];
static unsigned handoff_crc_table_ready;
static uint32_t handoff_crc_step(uint32_t crc)
{
    return (crc >> 8) ^ handoff_crc_table[crc&255u];
}
static void handoff_crc_init(void)
{
    if (handoff_crc_table_ready)
    {
        return;
    }
    for (unsigned i = 0; i < 256u; i++)
    {
        uint32_t v = i;
        for (unsigned j = 0; j < 8u; j++)
        {
            v = (v >> 1) ^ ((0u-(v&1u))&0xedb88320u);
        }
        handoff_crc_table[i] = v;
    }
    uint32_t crc = 0xffffffffu;
    static const char check[] = "123456789";
    for (unsigned i = 0; i < 9u; i++)
    {
        crc = handoff_crc_step(crc ^ (uint8_t) check[i]);
    }
    if (~crc != 0xcbf43926u)
    {
        handoff_abort("CRC32 table selftest");
    }
    handoff_crc_table_ready = 1;
}
static uint32_t handoff_crc(uint32_t length)
{
    /* Cortex-A8 in the verified little-endian physical addressing state.
       One aligned volatile DDR load per four bytes; handle short tail
       separately, never read beyond the received length. */
    uint32_t ctl;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if (ctl&(5u | (1u << 25)))
    {
        handoff_abort("unexpected CRC CPU state");
    }
    handoff_crc_init();
    __asm__ volatile("dsb sy":::"memory");
    uint32_t crc = 0xffffffffu, offset = 0;
    while (length-offset >= 4u)
    {
        uint32_t end = offset+4096u;
        if (end > length)
        {
            end = length;
        }
        end &= ~3u;
        handoff_check();
        while (offset < end)
        {
            crc ^= MMIO32(H_IMAGE_BASE+offset);
            crc = handoff_crc_step(crc);
            crc = handoff_crc_step(crc);
            crc = handoff_crc_step(crc);
            crc = handoff_crc_step(crc);
            offset += 4u;
        }
    }
    while (offset < length)
    {
        crc = handoff_crc_step(crc ^ MMIO8(H_IMAGE_BASE+offset));
        offset++;
    }
    handoff_check();
    return ~crc;
}
#else
static uint32_t handoff_crc(uint32_t length)
{
    /* Reflected CRC-32/ISO-HDLC, 4 bits per lookup. */
    static const uint32_t table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= MMIO8(H_IMAGE_BASE+i);
        crc = (crc >> 4) ^ table[crc&15u];
        crc = (crc >> 4) ^ table[crc&15u];
        if ((i&0xfffu) == 0)
        {
            handoff_check();
        }
    }
    return ~crc;
}
#endif
#endif
void handoff_test(void)
{
#ifdef USB_HANDOFF_BOOT
    uint32_t ctl;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if (ctl&5u)
    {
        handoff_abort("MMU / data cache enabled");
    }
    handoff_arm();
    handoff_enable_out();
    handoff_notice("OB-DMA-BOOT-RDY!");
    handoff_wait_dma(512u, 16u);
    volatile uint8_t *header = (volatile uint8_t *)(uintptr_t) handoff_packet;
    uint32_t magic = handoff_le32(header), len = handoff_le32(header+4);
    uint32_t expected_crc = handoff_le32(header+8), flags = handoff_le32(header+12);
    if (magic != H_MAGIC || !len || len > H_IMAGE_LIMIT || flags > 1u ||
        (flags && (len < 4u || (len&3u))))
    {
        handoff_abort("invalid image header");
    }
    console_reg("DMA IMAGE BYTES=", len);
    console_reg("DMA EXPECTED CRC32=", expected_crc);
    /* One USB host transfer per DMA window. Round the final hardware
       window to 512 bytes, never beyond the reserved DDR region. */
    uint32_t offset = 0;
    while (offset < len)
    {
        unsigned n = len-offset;
        if (n > H_CHUNK)
        {
            n = H_CHUNK;
        }
        unsigned capacity = (n+511u)&~511u;
        handoff_arm_dma(H_IMAGE_BASE+offset, capacity);
        if (!offset)
        {
            handoff_notice("OB-DMA-HEADEROK!");
        }
        handoff_wait_dma(capacity, n);
        offset += n;
    }
    console_reg("DMA RECEIVED=", offset);
    uint32_t crc = handoff_crc(len);
    console_reg("DMA DDR CRC32=", crc);
    if (crc != expected_crc)
    {
        handoff_abort("DDR CRC32 mismatch");
    }
    console_write("HANDOFF DMA DDR VERIFY PASS\r\n");
    if (!flags)
    {
        handoff_notice("OB-DMA-VERIFYOK!");
        console_write("Data only; cold power-cycle before next download\r\n");
        for (;;)
        {
            handoff_check();
        }
    }
    handoff_arm();
    handoff_notice("OB-DMA-VERIFYOK!");
    handoff_wait_dma(512u, 16u);
    static const char go[] = "OB-DMA-EXEC-GO1!";
    for (unsigned i = 0; i < 16u; i++)
    {
        if (header[i] != (uint8_t) go[i])
        {
            handoff_abort("invalid GO marker");
        }
    }
    if (handoff_crc(len) != expected_crc)
    {
        handoff_abort("pre-execution CRC32 mismatch");
    }
    handoff_notice("OB-DMA-GO-ACK01!");
    /* Short grace interval for host USB completion before disconnect. */
    uint32_t n = 4000000u;
    __asm__ volatile("1: subs %0,%0,#1\n bne 1b":"+r"(n)::"cc", "memory");
    handoff_run();
#else
    handoff_check();
    handoff_arm();
    handoff_enable_out();
    console_write("HANDOFF EP2 READY; sending USB readiness marker\r\n");
    handoff_notice("OB-HANDOFF-RDY1!");
    for (unsigned packet = 0; packet < 2; packet++)
    {
        unsigned actual = handoff_receive_length(512u);
        unsigned expected = packet ? 37u : 512u;
        console_reg("HANDOFF PACKET BYTES=", actual);
        console_reg("HANDOFF EP2 CTL AFTER=", handoff_out_control());
        if (actual != expected)
        {
            handoff_abort("packet length mismatch");
        }
        volatile uint8_t *src = (volatile uint8_t *)(uintptr_t) handoff_packet;
        for (unsigned i = 0; i < actual; i++)
        {
            uint8_t v = (uint8_t)(i*37u+packet*91u+17u);
            if (src[i] != v)
            {
                handoff_abort("DMA payload mismatch");
            }
            MMIO8(DDR_IMAGE_BASE+packet*512u+i) = src[i];
        }
        __asm__ volatile("dsb sy":::"memory");
        for (unsigned i = 0; i < actual; i++)
        {
            if (MMIO8(DDR_IMAGE_BASE+packet*512u+i) != (uint8_t)(i*37u+packet*91u+17u))
            {
                handoff_abort("DDR readback mismatch");
            }
        }
        if (!packet)
        {
            handoff_arm();
        }
    }
    handoff_ack_out();
    console_write("HANDOFF SAME CONNECTION PASS: 549 bytes, DMA + DDR readback\r\n");
    handoff_notice("OB-HANDOFF-OK01!");
    console_write("No execution; cold power-cycle before next download\r\n");
    for (;;)
    {
        handoff_check();
    }
#endif
}
#endif
