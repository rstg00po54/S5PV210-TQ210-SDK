/* Experimental same-address ROM handoff, S5PV210 DWC OTG 2.81a.
 * Controlled host: no EP0 requests/reset/reconfiguration after stage1.
 * Preserve DMA mode and hardware EP2 data toggle. No reset or flash I/O.
 * Fail closed on unexpected inherited state. */
#define HR(o) R(0xec000000u+(o))
#define HW(o,v) wr(0xec000000u+(o),(v))
#define HB(n) (1u<<(n))
#define H_EPENA HB(31)
#define H_SNAK HB(27)
#define H_CNAK HB(26)
#define H_DPID HB(16)
#define H_XFRC HB(0)
#define H_RESET HB(12)
#define H_SETUP (HB(3)|HB(6))
#define H_DMAERR HB(2)
#define H_DCTL 0x804u
#define H_GINTSTS 0x014u
#define H_GRSTCTL 0x010u
#define H_DOEPCTL2 0xb40u
#define H_DOEPINT2 0xb48u
#define H_DOEPTSIZ2 0xb50u
#define H_DOEPDMA2 0xb54u
static volatile uint32_t handoff_ep0_guard[128] __attribute__((aligned(32)));
static volatile uint32_t handoff_packet[128] __attribute__((aligned(32)));
static uint32_t handoff_address;
static void handoff_abort(const char *reason){
    out("HANDOFF ABORT: ");out(reason);out("\r\n");
    /* Only called after validating the core. Stop accepting host traffic. */
    HW(H_DCTL,HR(H_DCTL)|HB(1)|HB(9));
    if(wait_mask(0xec000000u+H_GRSTCTL,HB(31),HB(31)))
        HW(0x008,HR(0x008)&~(HB(5)|HB(0)));
    out("Cold power-cycle before retry\r\n");stop();
}
static void handoff_guard(void){
    if(!(R(0xe0100464u)&HB(16))||!(R(0xe010e80cu)&1u)||
       HR(0x040)!=0x4f54281au||(HR(H_GINTSTS)&1u)){
        out("HANDOFF BLOCKED: no expected device core\r\n");stop();
    }
    uint32_t ep=HR(H_DOEPCTL2);
    handoff_address=HR(0x800)&(127u<<4);
    if(!handoff_address||(HR(H_DCTL)&HB(1))||
       ((HR(0x808)>>1)&3u)!=0u||!(HR(0x008)&HB(5))||
       (ep&(HB(31)|HB(21)))||!(ep&HB(15))||
       ((ep>>18)&3u)!=2u||(ep&0x7ffu)!=512u||
       (HR(0x900)&H_EPENA)||(HR(0x920)&H_EPENA)||
       (HR(H_GINTSTS)&H_RESET))
        handoff_abort("unexpected ROM endpoint/address state");
    /* NAK all OUT transfers and mask interrupts before replacing DMA
       pointers. EP0 SETUP can be special; host must stay quiescent. */
    HW(0x018,0);
    HW(0x008,HR(0x008)&~HB(0));
    HW(H_DCTL,HR(H_DCTL)|HB(9));
    if(!wait_mask(0xec000000u+H_DCTL,HB(3),HB(3)))
        handoff_abort("global OUT NAK timeout");
    if(!wait_mask(0xec000000u+H_GRSTCTL,HB(31),HB(31)))
        handoff_abort("AHB idle timeout");
    if((HR(H_DOEPCTL2)&H_EPENA)||(HR(0x908)&H_DMAERR)||
       (HR(0xb08)&(H_SETUP|H_DMAERR))||(HR(H_GINTSTS)&H_RESET))
        handoff_abort("traffic during ROM handoff");
    /* The inherited EP0 DMA pointers are not owned by OpenBoot.
       Redirect to a local bounded scratch buffer; no EP0 service in this test.
       Do not arm another EP0 transfer or change the USB address. */
    uint32_t guard=(uint32_t)(uintptr_t)handoff_ep0_guard;
    HW(0xb14,guard);HW(0x914,guard);
    if(HR(0xb14)!=guard||HR(0x914)!=guard)
        handoff_abort("EP0 DMA guard readback mismatch");
    HW(0xb08,0xffffffffu);HW(0x908,0xffffffffu);
    HW(H_DOEPINT2,0xffffffffu);
    out("HANDOFF GUARD READY; original USB connection retained\r\n");
    reg("HANDOFF DCFG=",HR(0x800));reg("HANDOFF EP2 CTL=",HR(H_DOEPCTL2));
    reg("HANDOFF EP0 GUARD=",guard);
}
static void handoff_check(void){
    if((HR(H_GINTSTS)&H_RESET)||(HR(0x800)&(127u<<4))!=handoff_address||
       (HR(0xb08)&(H_SETUP|H_DMAERR))||(HR(0x908)&H_DMAERR)||
       (HR(H_DOEPINT2)&H_DMAERR))
        handoff_abort("reset / unexpected EP0 traffic / DMA error");
}
static void handoff_arm(void){
    handoff_check();
    if(HR(H_DOEPCTL2)&H_EPENA)handoff_abort("EP2 still enabled");
    /* Write only endpoint type/MPS/active + CNAK/EPENA. SD0PID and
       SD1PID are both zero, so the hardware data sequence is retained. */
    HW(H_DOEPINT2,0xffffffffu);
    HW(H_DOEPDMA2,(uint32_t)(uintptr_t)handoff_packet);
    HW(H_DOEPTSIZ2,(1u<<19)|512u);
    HW(H_DOEPCTL2,HB(15)|(2u<<18)|512u|H_CNAK|H_EPENA);
}
static void handoff_test(void){
    handoff_check();
    handoff_arm();
    HW(H_DCTL,HR(H_DCTL)|HB(10));
    out("HANDOFF EP2 READY; send test packets on original host handle\r\n");
    for(unsigned packet=0;packet<2;packet++){
        while(!(HR(H_DOEPINT2)&H_XFRC))handoff_check();
        handoff_check();
        if(!wait_mask(0xec000000u+H_GRSTCTL,HB(31),HB(31)))
            handoff_abort("DMA completion AHB timeout");
        unsigned remaining=HR(H_DOEPTSIZ2)&0x7ffffu;
        if(remaining>512u)handoff_abort("invalid DMA residual");
        unsigned actual=512u-remaining,expected=packet?37u:512u;
        reg("HANDOFF PACKET BYTES=",actual);
        reg("HANDOFF EP2 CTL AFTER=",HR(H_DOEPCTL2));
        if(actual!=expected)handoff_abort("packet length mismatch");
        volatile uint8_t *src=(volatile uint8_t *)(uintptr_t)handoff_packet;
        for(unsigned i=0;i<actual;i++){
            uint8_t v=(uint8_t)(i*37u+packet*91u+17u);
            if(src[i]!=v)handoff_abort("DMA payload mismatch");
            B(0x24000000u+packet*512u+i)=src[i];
        }
        __asm__ volatile("dsb sy":::"memory");
        for(unsigned i=0;i<actual;i++)
            if(B(0x24000000u+packet*512u+i)!=(uint8_t)(i*37u+packet*91u+17u))
                handoff_abort("DDR readback mismatch");
        if(!packet)handoff_arm();
    }
    HW(H_DOEPINT2,0xffffffffu);
    out("HANDOFF SAME CONNECTION PASS: 549 bytes, DMA + DDR readback\r\n");
    out("No execution; cold power-cycle before next download\r\n");
    for(;;)handoff_check();
}
