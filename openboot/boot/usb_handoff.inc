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
#define H_DIEPCTL1 0x920u
#define H_DIEPINT1 0x928u
#define H_DIEPTSIZ1 0x930u
#define H_DIEPDMA1 0x934u
#define H_DOEPCTL2 0xb40u
#define H_DOEPINT2 0xb48u
#define H_DOEPTSIZ2 0xb50u
#define H_DOEPDMA2 0xb54u
static volatile uint32_t handoff_ep0_guard[128] __attribute__((aligned(32)));
static volatile uint32_t handoff_packet[128] __attribute__((aligned(32)));
static volatile uint8_t handoff_in[32] __attribute__((aligned(32)));
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
    uint32_t ep=HR(H_DOEPCTL2),in=HR(H_DIEPCTL1);
    reg("HANDOFF ROM EP1 IN=",in);
    reg("HANDOFF ROM EP2 OUT=",ep);
    handoff_address=HR(0x800)&(127u<<4);
    if(!handoff_address||(HR(H_DCTL)&HB(1))||
       ((HR(0x808)>>1)&3u)!=0u||!(HR(0x008)&HB(5))||
       (ep&(HB(31)|HB(21)))||!(ep&HB(15))||
       ((ep>>18)&3u)!=2u||(ep&0x7ffu)!=512u||
       (HR(0x900)&H_EPENA)||(in&(H_EPENA|HB(21)))||
       !(in&HB(15))||((in>>18)&3u)!=2u||(in&0x7ffu)!=512u||
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
       (HR(H_DOEPINT2)&H_DMAERR)||(HR(H_DIEPINT1)&H_DMAERR))
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
static void handoff_notice(const char *message){
    handoff_check();
    uint32_t ctl=HR(H_DIEPCTL1);
    if(ctl&H_EPENA)handoff_abort("EP1 still enabled");
    if((ctl&0x7ffu)!=512u||!(ctl&HB(15))||((ctl>>18)&3u)!=2u)
        handoff_abort("EP1 configuration changed");
    for(unsigned i=0;i<16u;i++)handoff_in[i]=(uint8_t)message[i];
    __asm__ volatile("dsb sy":::"memory");
    HW(H_DIEPINT1,0xffffffffu);
    HW(H_DIEPDMA1,(uint32_t)(uintptr_t)handoff_in);
    HW(H_DIEPTSIZ1,(1u<<19)|16u);
    /* Preserve the inherited TX FIFO selection and endpoint toggle.
       Do not issue SD0PID/SD1PID or core reset. */
    HW(H_DIEPCTL1,(ctl&((15u<<22)|HB(15)|(3u<<18)|0x7ffu))|H_CNAK|H_EPENA);
    if(HR(H_DCTL)&HB(2))HW(H_DCTL,HR(H_DCTL)|HB(8));
    reg("HANDOFF EP1 ARMED=",HR(H_DIEPCTL1));
    while(!(HR(H_DIEPINT1)&H_XFRC))handoff_check();
    handoff_check();
    HW(H_DIEPINT1,H_XFRC);
    out("HANDOFF EP1 NOTICE SENT\r\n");
}
#ifdef USB_HANDOFF_BOOT
#define H_IMAGE_BASE 0x24000000u
#define H_IMAGE_LIMIT 0x01000000u
#define H_CHUNK 65536u
#define H_MAGIC 0x324c424fu /* OBL2 */
static uint32_t handoff_le32(const volatile uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void handoff_arm_dma(uint32_t address,unsigned bytes){
    handoff_check();
    if(HR(H_DOEPCTL2)&H_EPENA)handoff_abort("EP2 still enabled");
    if(!bytes||bytes>H_CHUNK||(bytes&511u)||(address&3u))
        handoff_abort("invalid DMA window");
    HW(H_DOEPINT2,0xffffffffu);
    HW(H_DOEPDMA2,address);
    HW(H_DOEPTSIZ2,((bytes>>9)<<19)|bytes);
    HW(H_DOEPCTL2,HB(15)|(2u<<18)|512u|H_CNAK|H_EPENA);
}
static void handoff_wait_dma(unsigned capacity,unsigned expected){
    while(!(HR(H_DOEPINT2)&H_XFRC))handoff_check();
    handoff_check();
    if(!wait_mask(0xec000000u+H_GRSTCTL,HB(31),HB(31)))
        handoff_abort("DMA completion AHB timeout");
    unsigned residual=HR(H_DOEPTSIZ2)&0x7ffffu;
    if(residual>capacity||capacity-residual!=expected)
        handoff_abort("DMA receive length mismatch");
    __asm__ volatile("dsb sy":::"memory");
    HW(H_DOEPINT2,H_XFRC);
}
#ifdef USB_HANDOFF_CRC_FAST
/* Generate the byte table in owned iRAM rather than growing the ROM frame.
   No MMU or data-cache change: USB DMA remains coherent. */
static uint32_t handoff_crc_table[256];
static unsigned handoff_crc_table_ready;
static uint32_t handoff_crc_step(uint32_t crc){
    return (crc>>8)^handoff_crc_table[crc&255u];
}
static void handoff_crc_init(void){
    if(handoff_crc_table_ready)return;
    for(unsigned i=0;i<256u;i++){
        uint32_t v=i;
        for(unsigned j=0;j<8u;j++)
            v=(v>>1)^((0u-(v&1u))&0xedb88320u);
        handoff_crc_table[i]=v;
    }
    uint32_t crc=0xffffffffu;
    static const char check[]="123456789";
    for(unsigned i=0;i<9u;i++)crc=handoff_crc_step(crc^(uint8_t)check[i]);
    if(~crc!=0xcbf43926u)handoff_abort("CRC32 table selftest");
    handoff_crc_table_ready=1;
}
static uint32_t handoff_crc(uint32_t length){
    /* Cortex-A8 in the verified little-endian physical addressing state.
       One aligned volatile DDR load per four bytes; handle short tail
       separately, never read beyond the received length. */
    uint32_t ctl;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if(ctl&(5u|HB(25)))handoff_abort("unexpected CRC CPU state");
    handoff_crc_init();
    __asm__ volatile("dsb sy":::"memory");
    uint32_t crc=0xffffffffu,offset=0;
    while(length-offset>=4u){
        uint32_t end=offset+4096u;
        if(end>length)end=length;
        end&=~3u;
        handoff_check();
        while(offset<end){
            crc^=R(H_IMAGE_BASE+offset);
            crc=handoff_crc_step(crc);
            crc=handoff_crc_step(crc);
            crc=handoff_crc_step(crc);
            crc=handoff_crc_step(crc);
            offset+=4u;
        }
    }
    while(offset<length){
        crc=handoff_crc_step(crc^B(H_IMAGE_BASE+offset));
        offset++;
    }
    handoff_check();
    return ~crc;
}
#else
static uint32_t handoff_crc(uint32_t length){
    /* Reflected CRC-32/ISO-HDLC, 4 bits per lookup. */
    static const uint32_t table[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu
    };
    uint32_t crc=0xffffffffu;
    for(uint32_t i=0;i<length;i++){
        crc^=B(H_IMAGE_BASE+i);
        crc=(crc>>4)^table[crc&15u];
        crc=(crc>>4)^table[crc&15u];
        if((i&0xfffu)==0)handoff_check();
    }
    return ~crc;
}
#endif
static void handoff_run(void){
    uint32_t ctl,zero=0;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if(ctl&5u)handoff_abort("MMU / data cache enabled");
    handoff_check();
    if((HR(H_DOEPCTL2)|HR(H_DIEPCTL1))&H_EPENA)
        handoff_abort("endpoint still active at GO");
    HW(H_DCTL,HR(H_DCTL)|HB(1)|HB(9));
    if(!wait_mask(0xec000000u+H_GRSTCTL,HB(31),HB(31)))
        handoff_abort("AHB busy at GO");
    HW(0x008,HR(0x008)&~(HB(5)|HB(0)));
    if(HR(0x008)&HB(5))handoff_abort("DMA disable failed");
    out("HANDOFF DDR EXEC ENTRY=0x24000000\r\n");
    (void)wait_mask(UART+0x10,4,4);
    __asm__ volatile("cpsid if; dsb sy; mcr p15,0,%0,c7,c5,0; mcr p15,0,%0,c7,c5,6; dsb sy; isb"::"r"(zero):"memory");
    void (*entry)(void)=(void (*)(void))(uintptr_t)H_IMAGE_BASE;
    entry();
    out("DDR APP RETURNED; stopping\r\n");stop();
}
#endif
static void handoff_test(void){
#ifdef USB_HANDOFF_BOOT
    uint32_t ctl;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(ctl));
    if(ctl&5u)handoff_abort("MMU / data cache enabled");
    handoff_arm();
    HW(H_DCTL,HR(H_DCTL)|HB(10));
    handoff_notice("OB-DMA-BOOT-RDY!");
    handoff_wait_dma(512u,16u);
    volatile uint8_t *header=(volatile uint8_t *)(uintptr_t)handoff_packet;
    uint32_t magic=handoff_le32(header),len=handoff_le32(header+4);
    uint32_t expected_crc=handoff_le32(header+8),flags=handoff_le32(header+12);
    if(magic!=H_MAGIC||!len||len>H_IMAGE_LIMIT||flags>1u||
       (flags&&(len<4u||(len&3u))))
        handoff_abort("invalid image header");
    reg("DMA IMAGE BYTES=",len);
    reg("DMA EXPECTED CRC32=",expected_crc);
    /* One USB host transfer per DMA window. Round the final hardware
       window to 512 bytes, never beyond the reserved DDR region. */
    uint32_t offset=0;
    while(offset<len){
        unsigned n=len-offset;
        if(n>H_CHUNK)n=H_CHUNK;
        unsigned capacity=(n+511u)&~511u;
        handoff_arm_dma(H_IMAGE_BASE+offset,capacity);
        if(!offset)handoff_notice("OB-DMA-HEADEROK!");
        handoff_wait_dma(capacity,n);
        offset+=n;
    }
    reg("DMA RECEIVED=",offset);
    uint32_t crc=handoff_crc(len);
    reg("DMA DDR CRC32=",crc);
    if(crc!=expected_crc)handoff_abort("DDR CRC32 mismatch");
    out("HANDOFF DMA DDR VERIFY PASS\r\n");
    if(!flags){
        handoff_notice("OB-DMA-VERIFYOK!");
        out("Data only; cold power-cycle before next download\r\n");
        for(;;)handoff_check();
    }
    handoff_arm();
    handoff_notice("OB-DMA-VERIFYOK!");
    handoff_wait_dma(512u,16u);
    static const char go[]="OB-DMA-EXEC-GO1!";
    for(unsigned i=0;i<16u;i++)if(header[i]!=(uint8_t)go[i])
        handoff_abort("invalid GO marker");
    if(handoff_crc(len)!=expected_crc)handoff_abort("pre-execution CRC32 mismatch");
    handoff_notice("OB-DMA-GO-ACK01!");
    /* Short grace interval for host USB completion before disconnect. */
    uint32_t n=4000000u;
    __asm__ volatile("1: subs %0,%0,#1\n bne 1b":"+r"(n)::"cc","memory");
    handoff_run();
#else
    handoff_check();
    handoff_arm();
    HW(H_DCTL,HR(H_DCTL)|HB(10));
    out("HANDOFF EP2 READY; sending USB readiness marker\r\n");
    handoff_notice("OB-HANDOFF-RDY1!");
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
    handoff_notice("OB-HANDOFF-OK01!");
    out("No execution; cold power-cycle before next download\r\n");
    for(;;)handoff_check();
#endif
}
