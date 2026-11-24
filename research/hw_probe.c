/* CoreB hardware probe. No NAND program or erase commands. */
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define B(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define UART 0xe2900000u
#define CLK 0xe0100000u
#define NAND 0xb0e00000u

static volatile uint32_t scratch[4096] __attribute__((aligned(64)));
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void tx(char c) { while (!(R(UART+0x10)&2)) {} B(UART+0x20)=(uint8_t)c; }
static void puts0(const char *s) { while (*s) tx(*s++); }
static void hex(uint32_t x) {
    static const char d[]="0123456789ABCDEF";
    puts0("0x");
    for (int n=28;n>=0;n-=4) tx(d[(x>>n)&15]);
}
static void reg(const char *s, uint32_t v) { puts0(s); puts0(" = "); hex(v); puts0("\r\n"); }
static void uart_init(void) {
    R(0xe2700000)=0;
    R(CLK+0x46c)|=0x60000;
    R(0xe0200000)=(R(0xe0200000)&~0xffu)|0x22;
    barrier();
    R(UART+4)=0; R(UART+8)=0; R(UART+12)=0;
    R(UART)=3; R(UART+0x28)=35; R(UART+0x2c)=1; R(UART+4)=5;
}
static uint32_t midr(void) {
    uint32_t v; __asm__ volatile("mrc p15,0,%0,c0,c0,0":"=r"(v)); return v;
}
static uint32_t sctlr(void) {
    uint32_t v; __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(v)); return v;
}
static uint32_t cycles(void) {
    uint32_t v; __asm__ volatile("isb; mrc p15,0,%0,c9,c13,0":"=r"(v)::"memory"); return v;
}
static void pmu_start(void) {
    uint32_t v=7;
    __asm__ volatile("mcr p15,0,%0,c9,c12,0"::"r"(v):"memory");
    v=1u<<31;
    __asm__ volatile("mcr p15,0,%0,c9,c12,1; isb"::"r"(v):"memory");
}
static void dmc(const char *name, uint32_t base) {
    puts0(name); puts0("\r\n");
    reg(" CONCONTROL",R(base));
    reg(" MEMCONTROL",R(base+4));
    for (unsigned i=0;i<2;i++) {
        uint32_t cfg=R(base+8+4*i);
        reg(i?" MEMCONFIG1":" MEMCONFIG0",cfg);
        uint32_t mask=(cfg>>16)&255, hi=cfg>>24, inv=(~mask)&255;
        /* Address-window size only. CS may be disabled or unpopulated. */
        if (mask && !(inv&(inv+1)) && !(hi&inv)) {
            reg("  configured base",hi<<24);
            reg("  configured window MiB",(inv+1)*16);
        } else puts0("  unusual mask; decode manually\r\n");
    }
    reg(" TIMINGAREF",R(base+0x30));
    reg(" TIMINGROW",R(base+0x34));
    reg(" TIMINGDATA",R(base+0x38));
    reg(" TIMINGPOWER",R(base+0x3c));
}
static void ram_test(void) {
    puts0("RAM scratch test (16 KiB; not full capacity)\r\n");
    reg(" scratch address",(uint32_t)(uintptr_t)scratch);
    uint32_t before=cycles();
    for(unsigned i=0;i<4096;i++) scratch[i]=0x55aa0000u^i;
    barrier();
    uint32_t write_cycles=cycles()-before;
    before=cycles();
    uint32_t errors=0;
    for(unsigned i=0;i<4096;i++) if(scratch[i]!=(0x55aa0000u^i)) errors++;
    uint32_t read_cycles=cycles()-before;
    for(unsigned i=0;i<4096;i++) scratch[i]=~(0x55aa0000u^i);
    barrier();
    for(unsigned i=0;i<4096;i++) if(scratch[i]!=~(0x55aa0000u^i)) errors++;
    reg(" errors",errors);
    reg(" write CPU cycles",write_cycles);
    reg(" read+compare CPU cycles",read_cycles);
    puts0(errors?"RAM scratch FAIL\r\n":"RAM scratch PASS\r\n");
}
static void delay(void) { for(volatile unsigned i=0;i<10000;i++) __asm__ volatile("nop"); }
static void nand_id(void) {
    puts0("NAND CE0 READ ID only\r\n");
    R(CLK+0x464)|=1u<<24; barrier();
    /* Pinmux and conservative timing follow vendor SDK 14.nand. */
    R(0xe02002e0)=0x22333322;
    R(0xe0200300)=0x00002222;
    R(0xe0200320)=0x22222222;
    R(NAND+4)=7; /* enable controller, deselect both CE */
    R(NAND)=0x1412;
    R(NAND+4)=5; /* select CE0 only */
    barrier(); delay();
    B(NAND+8)=0x90; B(NAND+12)=0;
    barrier(); delay();
    uint8_t id[8];
    for(unsigned i=0;i<8;i++) id[i]=B(NAND+0x10);
    R(NAND+4)=7; barrier();
    for(unsigned i=0;i<8;i++) reg(" ID byte",id[i]);
    if(id[0]==0x01 && id[1]==0xd3 && id[2]==0xd1 && id[3]==0x95 && id[4]==0x58)
        puts0("ID matches Spansion S34ML08G1: 1024 MiB SLC, 2048+64 B/page, 64 pages/block, 8192 blocks\r\n");
    else if(id[0]==0xec && id[1]==0xd3 && id[2]==0x51 && id[3]==0x95)
        puts0("ID matches Samsung K9K8G08U0B family: 1024 MiB, 2048+64 B/page, 64 pages/block, 8192 blocks\r\n");
    else puts0("Unmatched ID: consult datasheet; no capacity assumed\r\n");
}
void probe_main(void) {
    uart_init();
    puts0("\r\n=== TQ210 CoreB hardware probe ===\r\n");
    reg("MIDR",midr()); reg("SCTLR",sctlr());
    reg("APLL_CON0",R(CLK+0x100)); reg("MPLL_CON",R(CLK+0x108));
    reg("CLK_SRC0",R(CLK+0x200)); reg("CLK_DIV0",R(CLK+0x300));
    reg("CLK_DIV6",R(CLK+0x318));
    dmc("DMC0",0xf0000000); dmc("DMC1",0xf1400000);
    pmu_start(); ram_test();
    nand_id();
    puts0("=== END ===\r\nPress r to repeat report after reconnecting serial\r\n");
}
void probe_loop(void) {
    for (;;) {
        probe_main();
        for (;;) {
            if ((R(UART+0x10)&1) && B(UART+0x24)=='r') break;
        }
    }
}
