/* TQ210 CoreB OpenBoot v0.1, experimental cold USB handoff. No flash I/O. */
#include <stdint.h>
#include <stddef.h>
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define B(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define CLK 0xe0100000u
#define UART 0xe2900000u
#define TIMEOUT 2000000u
static unsigned console_ready;
static void wr(uint32_t a, uint32_t v) { R(a)=v; __asm__ volatile("dsb sy" ::: "memory"); }
static int wait_mask(uint32_t a, uint32_t mask, uint32_t expected) {
    for (unsigned i=0;i<TIMEOUT;i++) if ((R(a)&mask)==expected) return 1;
    return 0;
}
static void tx(char c) { if (wait_mask(UART+0x10,2,2)) B(UART+0x20)=(uint8_t)c; }
static void out(const char *s) { while (*s) tx(*s++); }
static void hex(uint32_t v) { static const char d[]="0123456789ABCDEF"; out("0x"); for(int i=28;i>=0;i-=4) tx(d[(v>>i)&15]); }
static void reg(const char *s,uint32_t v) { out(s); hex(v); out("\r\n"); }
static void stop(void) { for (;;) __asm__ volatile("nop"); }
void boot_fault(void) { if(console_ready) { uint32_t a,s; __asm__ volatile("mrc p15,0,%0,c6,c0,0":"=r"(a)); __asm__ volatile("mrc p15,0,%0,c5,c0,0":"=r"(s)); out("EXCEPTION (DFAR/DFSR meaningful for data abort)\r\n"); reg("DFAR=",a); reg("DFSR=",s); } stop(); }
static void uart(unsigned divisor,unsigned slot) {
    wr(CLK+0x46c,R(CLK+0x46c)|0x420000u);
    wr(0xe0200000,(R(0xe0200000)&~255u)|0x22u);
    wr(UART+4,0); wr(UART+8,0); wr(UART+12,0); wr(UART,3);
    wr(UART+0x28,divisor); wr(UART+0x2c,slot); wr(UART+4,5);
    console_ready=1;
}
#ifdef ROM_CLOCKS
/* Verified cold iROM console, no PLL/mux/divider writes. */
static void early_console(void) { uart(35,0); }
static int clock_init(void) {
    out("[1] KEEP ROM CLOCKS\r\n");
    reg("APLL=",R(CLK+0x100)); reg("MPLL=",R(CLK+0x108));
    reg("CLK_SRC0=",R(CLK+0x200)); reg("CLK_DIV0=",R(CLK+0x300));
    reg("CLK_SRC6=",R(CLK+0x218)); reg("CLK_DIV6=",R(CLK+0x318));
    if(R(CLK+0x100)!=0xa0c80601u || R(CLK+0x108)!=0xa29b0c01u ||
       R(CLK+0x200)!=0x111u || R(CLK+0x300)!=0x14141251u ||
       R(CLK+0x218)!=0 || R(CLK+0x318)!=0x00071000u) {
        out("Unexpected ROM clock state; DDR init blocked\r\n"); return 0;
    }
    out("[1] ROM CLOCK CHECK PASS\r\n"); return 1;
}
#define DDR_REFRESH 0x400u
#else
#define DDR_REFRESH 0x618u
/* Before changing clocks, select oscillator and use a known 24 MHz UART clock
   (PCLK_PSYS = FIN when divider is 1). This depends on the board's 24 MHz crystal. */
static void early_console(void) {
    /* Establish known conservative dividers before bypassing PLLs. */
    wr(CLK+0x300,0x14131440);
    if(!wait_mask(CLK+0x1000,0xffu,0)) stop();
    wr(CLK+0x200,0);
    for(volatile unsigned i=0;i<10000;i++) __asm__ volatile("nop");
    wr(CLK+0x300,0);
    if(!wait_mask(CLK+0x1000,0xffu,0)) stop();
    /* 24 MHz / (16 * 13) ~115384 baud. */
    uart(12,0);
}
static int pll(uint32_t off,uint32_t val) {
    wr(CLK+off,val);
    return wait_mask(CLK+off,1u<<29,1u<<29);
}
static int clock_init(void) {
    out("[1] PLL setup\r\n");
    wr(CLK+0x00,0xffff); wr(CLK+0x08,0xffff); wr(CLK+0x10,0xffff);
    /* CLK_SRC0 already bypasses PLLs. Program A/M/E; VPLL is not selected
       in the verified 0x111 mux snapshot, so leave it alone. */
    if(!pll(0x100,0x80fa0601)) {out("APLL LOCK TIMEOUT\r\n");return 0;}
    if(!pll(0x108,0x829b0c01)) {out("MPLL LOCK TIMEOUT\r\n");return 0;}
    if(!pll(0x110,0x80600602)) {out("EPLL LOCK TIMEOUT\r\n");return 0;}
    /* Avoid logging during divider/mux changes: UART timing changes too. */
    (void)wait_mask(UART+0x10,4,4);
    wr(CLK+0x300,0x14131440);
    if(!wait_mask(CLK+0x1000,0xffu,0)) return 0;
    wr(CLK+0x218,0); wr(CLK+0x318,0);
    wr(CLK+0x200,0x111);
    for(volatile unsigned i=0;i<10000;i++) __asm__ volatile("nop");
    /* Known-working UART0 setup from vendor-initialized CoreB probe. */
    uart(35,1);
    reg("APLL=",R(CLK+0x100)); reg("MPLL=",R(CLK+0x108));
    reg("CLK_SRC0=",R(CLK+0x200)); reg("CLK_DIV0=",R(CLK+0x300));
    out("[1] CLOCK OK\r\n");
    return 1;
}
#endif
#ifndef SRAM_ONLY
static void sync(void) { __asm__ volatile("dsb sy; isb" ::: "memory"); }
static const uint32_t directcmd[12]={0x07000000,0x01000000,0x00020000,0x00030000,0x00010400,0x00000542,0x01000000,0x05000000,0x05000000,0x00000442,0x00010780,0x00010400};
static void pause(void) { for(volatile unsigned i=0;i<1000;i++) __asm__ volatile("nop"); }
static int dmc_init(uint32_t base,int bank) {
    out(bank?"[3] DMC1 PHY\r\n":"[2] DMC0 PHY\r\n");
    reg("BEFORE CONCONTROL=",R(base)); reg("BEFORE MEMCONTROL=",R(base+4));
    reg("BEFORE PHY=",R(base+0x40));
    wr(base+0x18,0x00101008); wr(base+0x1c,0x46);
    wr(base+0x18,0x0010100a); wr(base+0x18,0x0010100b);
    /* Match vendor DLL poll/force sequence, but with bounded wait. */
    if(!wait_mask(base+0x40,3,0)) {reg("PHYSTATUS TIMEOUT=",R(base+0x40));return 0;}
    reg("PHY POLL DONE=",R(base+0x40));
    uint32_t v=R(base+0x18)|0x6a000000;
    wr(base+0x18,v); wr(base+0x18,v&~2u);
    wr(base+0x00,0x0fff1010); wr(base+0x04,0x002024c0);
    wr(base+0x08,bank?0x40e01323:0x20e01343);
    wr(base+0x0c,bank?0xffe01323:0x1ee01343);
    wr(base+0x14,0xff000000); wr(base+0x28,0xffff00ff);
    wr(base+0x30,DDR_REFRESH); wr(base+0x34,0x27233309);
    wr(base+0x38,bank?0x23240303:0x23240304); wr(base+0x3c,0x08c80232);
    for(unsigned i=0;i<16;i++) wr(base+0x60+8*i,0x00ff0000);
    for(unsigned i=0;i<16;i++) wr(base+0x64+8*i,0);
    out(bank?"[3] DMC1 DDR2 commands\r\n":"[2] DMC0 DDR2 commands\r\n");
    /* Explicit gaps are conservative; original emitted consecutive writes. */
    for(unsigned cs=0;cs<2;cs++) for(unsigned i=0;i<12;i++) {wr(base+0x10,directcmd[i]|(cs<<20));pause();}
    wr(base,R(base)|0x20); wr(base+4,(R(base+4)&~0x3fu)|0x13);
    reg("CONCONTROL=",R(base)); reg("MEMCONTROL=",R(base+4));
    reg("TIMINGAREF=",R(base+0x30));
    reg("MEMCONFIG0=",R(base+8)); reg("PHYSTATUS=",R(base+0x40));
    return 1;
}
static void drive_strength(void) {
    for(uint32_t a=0xe02003cc;a<=0xe02005ec;a+=0x20)
        wr(a,(a==0xe02004cc||a==0xe02005ec)?0x3fff:0xffff);
}
#define RAM0 0x20010000u
#define RAM1 0x40010000u
static uint32_t pattern(unsigned i,unsigned bank,unsigned pass) {return (0x55aa0000u^i^(bank?0x12340000u:0))^(pass?0xffffffffu:0);}
static int ram_test(void) {
    volatile uint32_t *const p[2]={(volatile uint32_t *)RAM0,(volatile uint32_t *)RAM1};
    out("[4] DDR scratch: 16 KiB per bank, two patterns\r\n");
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned bank=0;bank<2;bank++) {reg("WRITE ",bank?RAM1:RAM0);for(unsigned i=0;i<4096;i++) p[bank][i]=pattern(i,bank,pass);}
        sync();
        for(unsigned bank=0;bank<2;bank++) {reg("READ ",bank?RAM1:RAM0);for(unsigned i=0;i<4096;i++) if(p[bank][i]!=pattern(i,bank,pass)) {reg("FAIL AT ",(bank?RAM1:RAM0)+4*i);reg("EXPECTED ",pattern(i,bank,pass));reg("ACTUAL ",p[bank][i]);return 0;}}
    }
    out("[4] DDR SCRATCH PASS (not capacity verification)\r\n"); return 1;
}
extern const uint32_t ddr_stub_begin[],ddr_stub_end[];
static int ddr_exec(void) {
    volatile uint32_t *dst=(volatile uint32_t *)RAM0;
    size_t n=((uintptr_t)ddr_stub_end-(uintptr_t)ddr_stub_begin)/4;
    for(size_t i=0;i<n;i++) dst[i]=ddr_stub_begin[i];
    sync();
    uint32_t zero=0; __asm__ volatile("mcr p15,0,%0,c7,c5,0; dsb sy; isb"::"r"(zero):"memory");
    uint32_t (*f)(void)=(uint32_t (*)(void))(uintptr_t)RAM0;
    uint32_t result=f(); reg("[5] DDR execution result=",result);
    return result==0x5a5a2100u;
}
/* Optional destructive (RAM only) sample probe. Keep CPU/stack in SRAM. A bus
   hang can require power-cycle, so this is never run automatically. */
static void alias_probe(uint32_t base) {
    uint32_t original[6];
    static const uint32_t off[]={0,0x01000000,0x02000000,0x04000000,0x08000000,0x10000000};
    out("Alias sample: NOT a full capacity test\r\n");
    for(unsigned i=0;i<6;i++) {reg("PROBE ",base+off[i]);original[i]=R(base+off[i]);wr(base+off[i],0xa11a0000u+i);}
    for(unsigned i=0;i<6;i++) {reg("ADDR ",base+off[i]);reg("VALUE ",R(base+off[i]));}
    /* Restore in reverse order, including when two addresses alias. */
    for(unsigned i=6;i>0;i--) wr(base+off[i-1],original[i-1]);
    out("Sample complete; inspect values for aliases\r\n");
}
#endif
void boot_main(void) {
    wr(0xe2700000,0);
    early_console();
#ifdef ROM_CLOCKS
    out("\r\nTQ210 OpenBoot v0.3 ROM CLOCKS\r\n[0] SRAM C ENTRY OK\r\n");
#else
    out("\r\nTQ210 OpenBoot v0.1 EXPERIMENTAL\r\n[0] SRAM ENTRY OK\r\n");
#endif
    if(!clock_init()) {out("CLOCK FAIL\r\n");stop();}
#ifdef SRAM_ONLY
    out("SRAM TEST PASS; DDR not touched\r\n");
    for(;;) if(R(UART+0x10)&1) { (void)B(UART+0x24); out("SRAM TEST PASS; DDR not touched\r\n"); }
#else
    drive_strength();
    if(!dmc_init(0xf0000000,0)) {out("DMC0 FAIL\r\n");stop();}
    if(!dmc_init(0xf1400000,1)) {out("DMC1 FAIL\r\n");stop();}
    if(!ram_test()) stop();
    if(!ddr_exec()) {out("DDR EXEC FAIL\r\n");stop();}
    out("[5] DDR EXEC PASS; back in SRAM\r\n");
    out("OpenBoot diagnostic complete. a=optional DMC0 alias probe, b=DMC1.\r\n");
    for(;;) if(R(UART+0x10)&1) {unsigned c=B(UART+0x24);if(c=='a')alias_probe(RAM0);if(c=='b')alias_probe(RAM1);}
#endif
    stop();
}
