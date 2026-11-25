/* OpenBoot DWC OTG 2.81a enumeration experiment. Included by main.c.
 * Private-test VID/PID 1209:0001 by default; USB_DNW_ID uses the existing
 * board driver hardware ID 04e8:1234 for local development only.
 * Neither variant is intended for distribution under these IDs.
 * EP0 only, PIO/polling, no USB DMA or writes to downloaded DDR images.
 * Preserve the board's known working PHY configuration. */
#define UREG(off) R(0xec000000u+(off))
#define UW(off,v) wr(0xec000000u+(off),(v))
#define UBIT(n) (1u<<(n))
#define GAHBCFG 0x008u
#define GUSBCFG 0x00cu
#define GRSTCTL 0x010u
#define GINTSTS 0x014u
#define GINTMSK 0x018u
#define GRXSTSP 0x020u
#define GRXFSIZ 0x024u
#define GNPTXFSIZ 0x028u
#define GNPTXSTS 0x02cu
#define DCFG 0x800u
#define DCTL 0x804u
#define DSTS 0x808u
#define DIEPMSK 0x810u
#define DOEPMSK 0x814u
#define DAINTMSK 0x81cu
#define DIEPCTL0 0x900u
#define DIEPINT0 0x908u
#define DIEPTSIZ0 0x910u
#define DOEPCTL0 0xb00u
#define DOEPINT0 0xb08u
#define DOEPTSIZ0 0xb10u
#define FIFO0 0x1000u
#define USB_RESET UBIT(12)
#define USB_ENUM UBIT(13)
#define RX_LEVEL UBIT(4)
#define EPENA UBIT(31)
#define SNAK UBIT(27)
#define CNAK UBIT(26)
#define STALL UBIT(21)
#define ACTIVE UBIT(15)
#define SOFT_DISCONNECT UBIT(1)
#define CLEAR_NAKS (UBIT(8)|UBIT(10))
#define XFRC UBIT(0)

static uint8_t usb_setup[8];
static unsigned usb_setup_valid,usb_config,usb_phase;
enum { USB_IDLE,USB_IN_DATA,USB_OUT_STATUS,USB_IN_STATUS };
/* No endpoint other than EP0 advertised at this milestone. */
static const uint8_t usb_device_desc[]={
#ifdef USB_DNW_ID
    18,1,0,2,0,0,0,64,0xe8,0x04,0x34,0x12,0x01,0x00,1,2,0,1
#else
    18,1,0,2,0,0,0,64,0x09,0x12,0x01,0x00,0x01,0x00,1,2,0,1
#endif
};
static const uint8_t usb_config_desc[]={
    9,2,18,0,1,1,0,0x80,50,
    9,4,0,0,0,0xff,0,0,0
};
static const uint8_t usb_other_desc[]={
    9,7,18,0,1,1,0,0x80,50,
    9,4,0,0,0,0xff,0,0,0
};
static const uint8_t usb_qualifier[]={10,6,0,2,0,0,0,64,1,0};
static const uint8_t usb_lang[]={4,3,9,4};
static const uint8_t usb_manufacturer[]={18,3,'O',0,'p',0,'e',0,'n',0,'B',0,'o',0,'o',0,'t',0};
static const uint8_t usb_product[]={24,3,'T',0,'Q',0,'2',0,'1',0,'0',0,' ',0,'O',0,'T',0,'G',0,' ',0,'1',0};

static int usb_wait(uint32_t off,uint32_t mask,uint32_t val) {
    return wait_mask(0xec000000u+off,mask,val);
}
static void usb_arm_setup(void) {
    /* Three setup slots, one 64-byte receive packet; EP0 MPS encoding 0 = 64. */
    UW(DOEPTSIZ0,(3u<<29)|(1u<<19)|64u);
    UW(DOEPCTL0,ACTIVE|CNAK|EPENA);
}
static void usb_arm_status_out(void) {
    UW(DOEPTSIZ0,(3u<<29)|(1u<<19)|64u);
    UW(DOEPCTL0,ACTIVE|CNAK|EPENA);
}
static void usb_stall(void) {
    UW(DIEPCTL0,ACTIVE|STALL|SNAK);
    UW(DOEPCTL0,ACTIVE|STALL|CNAK|EPENA);
    usb_phase=USB_IDLE;
}
static int usb_reply(const uint8_t *data,unsigned n,unsigned requested,unsigned phase) {
    if(n>requested)n=requested;
    if(n>64u)return 0;
    /* All descriptors fit within one packet, including the status ZLP. */
    unsigned words=(n+3u)>>2;
    if(phase==USB_IN_DATA)usb_arm_status_out();
    UW(DIEPINT0,0x0fu);
    UW(DIEPTSIZ0,(1u<<19)|n);
    UW(DIEPCTL0,ACTIVE|CNAK|EPENA);
    if(words) {
        unsigned timeout=TIMEOUT;
        while((UREG(GNPTXSTS)&0xffffu)<words && --timeout) {}
        if(!timeout) {out("EP0 TX FIFO TIMEOUT\r\n");return 0;}
        for(unsigned i=0;i<n;i+=4u) {
            uint32_t w=0;
            for(unsigned j=0;j<4u && i+j<n;j++)w|=(uint32_t)data[i+j]<<(8u*j);
            UREG(FIFO0)=w;
        }
        sync();
    }
    usb_phase=phase;
    return 1;
}
static void usb_request(void) {
    unsigned type=usb_setup[0],req=usb_setup[1];
    unsigned value=(unsigned)usb_setup[2]|((unsigned)usb_setup[3]<<8);
    unsigned index=(unsigned)usb_setup[4]|((unsigned)usb_setup[5]<<8);
    unsigned len=(unsigned)usb_setup[6]|((unsigned)usb_setup[7]<<8);
    reg("SETUP TYPE/REQ=",(type<<8)|req);
    reg("SETUP VALUE=",value);
    reg("SETUP LENGTH=",len);
    const uint8_t *p=0;unsigned n=0;
    uint8_t small[2]={0,0};
    if(type==0x80u && req==6u) {
        unsigned dt=value>>8,di=value&255u;
        if(dt==1u&&di==0u){p=usb_device_desc;n=sizeof(usb_device_desc);}
        else if(dt==2u&&di==0u){p=usb_config_desc;n=sizeof(usb_config_desc);}
        else if(dt==6u&&di==0u){p=usb_qualifier;n=sizeof(usb_qualifier);}
        else if(dt==7u&&di==0u){p=usb_other_desc;n=sizeof(usb_other_desc);}
        else if(dt==3u) {
            if(di==0u){p=usb_lang;n=sizeof(usb_lang);}
            else if(di==1u){p=usb_manufacturer;n=sizeof(usb_manufacturer);}
            else if(di==2u){p=usb_product;n=sizeof(usb_product);}
        }
        if(p&&usb_reply(p,n,len,USB_IN_DATA))return;
    } else if(type==0u&&req==5u&&value<=127u&&index==0u&&len==0u) {
        /* DWC OTG uses DCFG address before sending SET_ADDRESS status IN. */
        UW(DCFG,(UREG(DCFG)&~(127u<<4))|(value<<4));
        reg("SET ADDRESS=",value);
        if(usb_reply(0,0,0,USB_IN_STATUS))return;
    } else if(type==0u&&req==9u&&value<=1u&&index==0u&&len==0u) {
        usb_config=value;
        if(usb_reply(0,0,0,USB_IN_STATUS)) {
            reg("SET CONFIGURATION=",value);return;
        }
    } else if(type==0x80u&&req==8u&&value==0u&&index==0u&&len==1u) {
        small[0]=(uint8_t)usb_config;
        if(usb_reply(small,1,len,USB_IN_DATA))return;
    } else if((type==0x80u||type==0x81u||type==0x82u)&&req==0u&&value==0u&&len==2u) {
        if(usb_reply(small,2,len,USB_IN_DATA))return;
    } else if(type==0x81u&&req==10u&&value==0u&&index==0u&&len==1u) {
        if(usb_reply(small,1,len,USB_IN_DATA))return;
    } else if(type==1u&&req==11u&&value==0u&&index==0u&&len==0u) {
        if(usb_reply(0,0,0,USB_IN_STATUS))return;
    }
    out("EP0 STALL: unsupported request\r\n");
    usb_stall();
}
static void usb_bus_reset(void) {
    usb_phase=USB_IDLE;usb_config=0;usb_setup_valid=0;
    UW(DCFG,UREG(DCFG)&~(127u<<4));
    UW(DIEPMSK,0x0fu);UW(DOEPMSK,0x0fu);
    UW(DAINTMSK,0x10001u);
    UW(DIEPINT0,0xffffffffu);UW(DOEPINT0,0xffffffffu);
    UW(DIEPCTL0,ACTIVE|SNAK);
    UW(DCTL,CLEAR_NAKS);
    usb_arm_setup();
    out("USB BUS RESET\r\n");
}
static void usb_rx(void) {
    uint32_t st=UREG(GRXSTSP); /* Pop one receive status, then exactly its data. */
    unsigned ep=st&15u,n=(st>>4)&0x7ffu,kind=(st>>17)&15u;
    if(kind==6u) {
        usb_setup_valid=0;
        for(unsigned i=0;i<n;i+=4u) {
            uint32_t w=UREG(FIFO0);
            if(ep==0u&&n==8u)for(unsigned j=0;j<4u;j++)usb_setup[i+j]=(uint8_t)(w>>(8u*j));
        }
        usb_setup_valid=(ep==0u&&n==8u);
    } else {
        for(unsigned i=0;i<n;i+=4u){uint32_t w=UREG(FIFO0);(void)w;}
    }
    if(ep!=0u)return;
    if(kind==4u&&usb_setup_valid) {
        usb_setup_valid=0;usb_phase=USB_IDLE;
        UW(DOEPINT0,0x0bu);
        usb_request();
    } else if(kind==3u&&(usb_phase==USB_IN_DATA||usb_phase==USB_OUT_STATUS)) {
        usb_phase=USB_IDLE;usb_arm_setup();
    }
}
/* Keep the delay counter in a CPU register, not uncached SRAM.
 * Approximate settling delay at the verified 400 MHz ARM clock. */
static void usb_settle(void) {
    uint32_t n=40000000u;
    __asm__ volatile("1: subs %0,%0,#1\n bne 1b":"+r"(n)::"cc","memory");
}
static int usb_start(void) {
    if(!(R(0xe010e80cu)&1u)||!(R(0xe0100464u)&UBIT(16))||
       UREG(0x040u)!=0x4f54281au||(UREG(GINTSTS)&1u)) {
        out("USB ENUM BLOCKED: unexpected hardware/mode\r\n");return 0;
    }
    uint32_t cfg=UREG(GUSBCFG);
    out("USB SOFT DISCONNECT; taking ownership from iROM\r\n");
    reg("PRE GAHBCFG=",UREG(GAHBCFG));
    reg("PRE DCTL=",UREG(DCTL));
    reg("PRE GRSTCTL=",UREG(GRSTCTL));
    out("[USB 1] mask interrupts\r\n");
    UW(GINTMSK,0);
    out("[USB 2] assert soft disconnect\r\n");
    UW(DCTL,UREG(DCTL)|SOFT_DISCONNECT);
    reg("DISCONNECTED DCTL=",UREG(DCTL));
    out("[USB 3] disconnect settle\r\n");
    usb_settle();
    out("[USB 4] wait AHB idle\r\n");
    if(!usb_wait(GRSTCTL,UBIT(31),UBIT(31))) {
        reg("USB AHB IDLE TIMEOUT GRSTCTL=",UREG(GRSTCTL));return 0;
    }
    out("[USB 5] core soft reset\r\n");
    UW(GRSTCTL,1);
    if(!usb_wait(GRSTCTL,1,0)) {
        reg("USB CORE RESET TIMEOUT GRSTCTL=",UREG(GRSTCTL));return 0;
    }
    reg("CORE RESET DONE GRSTCTL=",UREG(GRSTCTL));
    out("[USB 6] reassert disconnect; disable DMA/IRQ\r\n");
    UW(DCTL,SOFT_DISCONNECT);
    UW(GAHBCFG,0);
    reg("PIO GAHBCFG=",UREG(GAHBCFG));
    out("[USB 7] force device mode; settle\r\n");
    UW(GUSBCFG,(cfg&~UBIT(29))|UBIT(30));
    usb_settle();
    if(UREG(GINTSTS)&1u){out("USB DEVICE MODE FAILED\r\n");return 0;}
    reg("DEVICE GUSBCFG=",UREG(GUSBCFG));
    out("[USB 8] configure FIFO sizes\r\n");
    UW(DCTL,SOFT_DISCONNECT);
    UW(GRXFSIZ,1024u);UW(GNPTXFSIZ,(256u<<16)|1024u);
    out("[USB 9] flush TX FIFO\r\n");
    UW(GRSTCTL,UBIT(5)|(16u<<6));
    if(!usb_wait(GRSTCTL,UBIT(5),0)){
        reg("USB TX FLUSH TIMEOUT GRSTCTL=",UREG(GRSTCTL));return 0;
    }
    out("[USB 10] flush RX FIFO\r\n");
    UW(GRSTCTL,UBIT(4));
    if(!usb_wait(GRSTCTL,UBIT(4),0)){
        reg("USB RX FLUSH TIMEOUT GRSTCTL=",UREG(GRSTCTL));return 0;
    }
    out("[USB 11] initialize EP0\r\n");
    UW(DCFG,0); /* High-speed capable UTMI device; host may negotiate FS. */
    UW(GINTSTS,0xffffffffu);
    UW(DIEPMSK,0x0fu);UW(DOEPMSK,0x0fu);UW(DAINTMSK,0x10001u);
    usb_phase=USB_IDLE;usb_config=0;usb_setup_valid=0;
    UW(DIEPCTL0,ACTIVE|SNAK);usb_arm_setup();
    UW(GINTMSK,USB_RESET|USB_ENUM|RX_LEVEL|UBIT(18)|UBIT(19));
#ifdef USB_DNW_ID
    out("USB RECONNECT: local DNW driver ID 04e8:1234; EP0 only\r\n");
#else
    out("USB RECONNECT: private test VID:PID 1209:0001\r\n");
#endif
    UW(DCTL,CLEAR_NAKS);
    reg("GAHBCFG=",UREG(GAHBCFG));reg("GUSBCFG=",UREG(GUSBCFG));
    return 1;
}
static void usb_enum_test(void) {
    if(!usb_start())return;
    for(;;) {
        uint32_t st=UREG(GINTSTS);
        if(st&USB_RESET){UW(GINTSTS,USB_RESET);usb_bus_reset();}
        if(st&USB_ENUM){
            UW(GINTSTS,USB_ENUM);
            reg("USB ENUM SPEED FIELD=",(UREG(DSTS)>>1)&3u);
            UW(DCTL,CLEAR_NAKS);
        }
        /* Process IN completion before a fresh SETUP, so old XFRC cannot
           accidentally finish a newly queued reply. */
        uint32_t in=UREG(DIEPINT0);
        if(in&XFRC){
            UW(DIEPINT0,XFRC);
            if(usb_phase==USB_IN_STATUS){
                usb_phase=USB_IDLE;usb_arm_setup();
                if(usb_config)out("OPENBOOT USB CONFIGURED\r\n");
            }else if(usb_phase==USB_IN_DATA)usb_phase=USB_OUT_STATUS;
        }
        if(UREG(GINTSTS)&RX_LEVEL)usb_rx();
        uint32_t o=UREG(DOEPINT0);
        if(o&XFRC)UW(DOEPINT0,XFRC);
        /* UART escape; cold reboot needed to recover the original ROM USB. */
        if(R(UART+0x10)&1u){
            unsigned ch=B(UART+0x24);
            if(ch=='q'){
                UW(DCTL,UREG(DCTL)|SOFT_DISCONNECT);
                out("USB TEST STOPPED; cold reboot to restore iROM\r\n");return;
            }
        }
    }
}
