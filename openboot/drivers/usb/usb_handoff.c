#include "handoff_internal.h"
#if defined(USB_HANDOFF_TEST) && !defined(SRAM_ONLY)
static volatile uint32_t handoff_ep0_guard[128] __attribute__((aligned(32)));
volatile uint32_t handoff_packet[128] __attribute__((aligned(32)));
static volatile uint8_t handoff_in[32] __attribute__((aligned(32)));
static uint32_t handoff_address;
void handoff_abort(const char *reason)
{
    console_write("HANDOFF ABORT: ");
    console_write(reason);
    console_write("\r\n");
    /* Only called after validating the core. Stop accepting host traffic. */
    HW(H_DCTL, HR(H_DCTL)|HB(1)|HB(9));
    if (mmio_wait_mask(S5PV210_USB_OTG_BASE+H_GRSTCTL, HB(31), HB(31)))
    HW(0x008, HR(0x008)&~(HB(5)|HB(0)));
    console_write("Cold power-cycle before retry\r\n");
    boot_halt();
}
void handoff_guard(void)
{
    if (!(MMIO32(0xe0100464u)&HB(16)) || !(MMIO32(S5PV210_USB_PHY_CONTROL)&1u) ||
    HR(0x040) != 0x4f54281au ||(HR(H_GINTSTS)&1u))
    {
        console_write("HANDOFF BLOCKED: no expected device core\r\n");
        boot_halt();
    }
    uint32_t ep = HR(H_DOEPCTL2), in = HR(H_DIEPCTL1);
    console_reg("HANDOFF ROM EP1 IN=", in);
    console_reg("HANDOFF ROM EP2 OUT=", ep);
    handoff_address = HR(0x800)&(127u<<4);
    if (!handoff_address ||(HR(H_DCTL)&HB(1)) ||
    ((HR(0x808)>>1)&3u) != 0u || !(HR(0x008)&HB(5)) ||
    (ep&(HB(31)|HB(21))) || !(ep&HB(15)) ||
    ((ep>>18)&3u) != 2u ||(ep&0x7ffu) != 512u ||
    (HR(0x900)&H_EPENA) ||(in&(H_EPENA|HB(21))) ||
    !(in&HB(15)) ||((in>>18)&3u) != 2u ||(in&0x7ffu) != 512u ||
    (HR(H_GINTSTS)&H_RESET))
    handoff_abort("unexpected ROM endpoint/address state");
    /* NAK all OUT transfers and mask interrupts before replacing DMA
       pointers. EP0 SETUP can be special; host must stay quiescent. */
    HW(0x018, 0);
    HW(0x008, HR(0x008)&~HB(0));
    HW(H_DCTL, HR(H_DCTL)|HB(9));
    if (!mmio_wait_mask(S5PV210_USB_OTG_BASE+H_DCTL, HB(3), HB(3)))
    handoff_abort("global OUT NAK timeout");
    if (!mmio_wait_mask(S5PV210_USB_OTG_BASE+H_GRSTCTL, HB(31), HB(31)))
    handoff_abort("AHB idle timeout");
    if ((HR(H_DOEPCTL2)&H_EPENA) ||(HR(0x908)&H_DMAERR) ||
    (HR(0xb08)&(H_SETUP|H_DMAERR)) ||(HR(H_GINTSTS)&H_RESET))
    handoff_abort("traffic during ROM handoff");
    /* The inherited EP0 DMA pointers are not owned by OpenBoot.
       Redirect to a local bounded scratch buffer; no EP0 service in this test.
       Do not arm another EP0 transfer or change the USB address. */
    uint32_t guard =(uint32_t)(uintptr_t)handoff_ep0_guard;
    HW(0xb14, guard);
    HW(0x914, guard);
    if (HR(0xb14) != guard || HR(0x914) != guard)
    handoff_abort("EP0 DMA guard readback mismatch");
    HW(0xb08, 0xffffffffu);
    HW(0x908, 0xffffffffu);
    HW(H_DOEPINT2, 0xffffffffu);
    console_write("HANDOFF GUARD READY; original USB connection retained\r\n");
    console_reg("HANDOFF DCFG=", HR(0x800));
    console_reg("HANDOFF EP2 CTL=", HR(H_DOEPCTL2));
    console_reg("HANDOFF EP0 GUARD=", guard);
}
void handoff_check(void)
{
    if ((HR(H_GINTSTS)&H_RESET) ||(HR(0x800)&(127u<<4)) != handoff_address ||
    (HR(0xb08)&(H_SETUP|H_DMAERR)) ||(HR(0x908)&H_DMAERR) ||
    (HR(H_DOEPINT2)&H_DMAERR) ||(HR(H_DIEPINT1)&H_DMAERR))
    handoff_abort("reset / unexpected EP0 traffic / DMA error");
}
void handoff_arm(void)
{
    handoff_check();
    if (HR(H_DOEPCTL2)&H_EPENA)handoff_abort("EP2 still enabled");
    /* Write only endpoint type/MPS/active + CNAK/EPENA. SD0PID and
       SD1PID are both zero, so the hardware data sequence is retained. */
    HW(H_DOEPINT2, 0xffffffffu);
    HW(H_DOEPDMA2, (uint32_t)(uintptr_t)handoff_packet);
    HW(H_DOEPTSIZ2, (1u<<19)|512u);
    HW(H_DOEPCTL2, HB(15)|(2u<<18)|512u|H_CNAK|H_EPENA);
}
void handoff_notice(const char *message)
{
    handoff_check();
    uint32_t ctl = HR(H_DIEPCTL1);
    if (ctl&H_EPENA)handoff_abort("EP1 still enabled");
    if ((ctl&0x7ffu) != 512u || !(ctl&HB(15)) ||((ctl>>18)&3u) != 2u)
    handoff_abort("EP1 configuration changed");
    for (unsigned i = 0; i<16u; i++)handoff_in[i] =(uint8_t)message[i];
    __asm__ volatile("dsb sy":::"memory");
    HW(H_DIEPINT1, 0xffffffffu);
    HW(H_DIEPDMA1, (uint32_t)(uintptr_t)handoff_in);
    HW(H_DIEPTSIZ1, (1u<<19)|16u);
    /* Preserve the inherited TX FIFO selection and endpoint toggle.
       Do not issue SD0PID/SD1PID or core reset. */
    HW(H_DIEPCTL1, (ctl&((15u<<22)|HB(15)|(3u<<18)|0x7ffu))|H_CNAK|H_EPENA);
    if (HR(H_DCTL)&HB(2))HW(H_DCTL, HR(H_DCTL)|HB(8));
    console_reg("HANDOFF EP1 ARMED=", HR(H_DIEPCTL1));
    while (!(HR(H_DIEPINT1)&H_XFRC))handoff_check();
    handoff_check();
    HW(H_DIEPINT1, H_XFRC);
    console_write("HANDOFF EP1 NOTICE SENT\r\n");
}
#ifdef USB_HANDOFF_BOOT
#define H_CHUNK 65536u
void handoff_arm_dma(uint32_t address, unsigned bytes)
{
    handoff_check();
    if (HR(H_DOEPCTL2)&H_EPENA)handoff_abort("EP2 still enabled");
    if (!bytes || bytes>H_CHUNK ||(bytes&511u) ||(address&3u))
    handoff_abort("invalid DMA window");
    HW(H_DOEPINT2, 0xffffffffu);
    HW(H_DOEPDMA2, address);
    HW(H_DOEPTSIZ2, ((bytes>>9)<<19)|bytes);
    HW(H_DOEPCTL2, HB(15)|(2u<<18)|512u|H_CNAK|H_EPENA);
}
void handoff_wait_dma(unsigned capacity, unsigned expected)
{
    while (!(HR(H_DOEPINT2)&H_XFRC))handoff_check();
    handoff_check();
    if (!mmio_wait_mask(S5PV210_USB_OTG_BASE+H_GRSTCTL, HB(31), HB(31)))
    handoff_abort("DMA completion AHB timeout");
    unsigned residual = HR(H_DOEPTSIZ2)&0x7ffffu;
    if (residual>capacity || capacity-residual != expected)
    handoff_abort("DMA receive length mismatch");
    __asm__ volatile("dsb sy":::"memory");
    HW(H_DOEPINT2, H_XFRC);
}
void handoff_run(void)
{
    uint32_t ctl, zero = 0;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if (ctl&5u)handoff_abort("MMU / data cache enabled");
    handoff_check();
    if ((HR(H_DOEPCTL2)|HR(H_DIEPCTL1))&H_EPENA)
    handoff_abort("endpoint still active at GO");
    HW(H_DCTL, HR(H_DCTL)|HB(1)|HB(9));
    if (!mmio_wait_mask(S5PV210_USB_OTG_BASE+H_GRSTCTL, HB(31), HB(31)))
    handoff_abort("AHB busy at GO");
    HW(0x008, HR(0x008)&~(HB(5)|HB(0)));
    if (HR(0x008)&HB(5))handoff_abort("DMA disable failed");
    console_write("HANDOFF DDR EXEC ENTRY=0x24000000\r\n");
    (void)mmio_wait_mask(S5PV210_UART0_BASE + UART_UTRSTAT, 4, 4);
    __asm__ volatile("cpsid if; dsb sy; mcr p15,0,%0,c7,c5,0; mcr p15,0,%0,c7,c5,6; dsb sy; isb"::"r"(zero):"memory");
    void (*entry)(void) =(void (*)(void))(uintptr_t)DDR_IMAGE_BASE;
    entry();
    console_write("DDR APP RETURNED; stopping\r\n");
    boot_halt();
}
#endif
#endif
