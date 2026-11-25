#include "platform.h"
#include "nand.h"
#include "timebase.h"

#define NF_BASE 0xb0e00000u
#define NFCONF MMIO32(NF_BASE + 0x00u)
#define NFCONT MMIO32(NF_BASE + 0x04u)
#define NFCMD MMIO8(NF_BASE + 0x08u)
#define NFADDR MMIO8(NF_BASE + 0x0cu)
#define NFDATA MMIO8(NF_BASE + 0x10u)
#define NFSTAT MMIO32(NF_BASE + 0x28u)
#define GATE MMIO32(CLOCK_BASE + 0x464u)
#define NF_GATE (1u << 28)
#define MP01 MMIO32(0xe02002e0u)
#define MP03 MMIO32(0xe0200320u)
#define MP06 MMIO32(0xe0200380u)
#define CE0 (1u << 1)
#define CE1 (1u << 2)
#define ENABLE 1u
#define PAGE 2048u
#define OOB 64u
#define PPB 64u
/* Expand from 512 MiB to 1 GiB only after exact ID and ONFI CRC checks. */
#define LIMIT_PAGES 524288u
#define HALF_PAGES 262144u
#define ECCREG(off) MMIO32(0xb0e20000u + (off))
#define ECCCONF ECCREG(0x00u)
#define ECCCONT ECCREG(0x20u)
#define ECCSTAT ECCREG(0x30u)
#define ECCSECSTAT ECCREG(0x40u)
#define DECODE_DONE (1u << 24)
#define ECC_BUSY (1u << 31)
#define ENCODE_DONE (1u << 25)
#define BLOCKS (LIMIT_PAGES / PPB)
#define NO_BLOCK 0xffffffffu
static uint8_t id[5], buf[PAGE + OOB], copy[PAGE + OOB];
static unsigned initialized, geometry, page_limit;
static uint8_t param[768];
static uint8_t bbt[BLOCKS / 8u], expected[PAGE + OOB], injected[PAGE + OOB];
static unsigned bbt_valid, armed = NO_BLOCK;
static int phase3(void);
static int onfi(void);

static void delay_bus(void)
{
    dsb();
    for (unsigned i = 0; i < 256u; ++i) {
        __asm__ volatile("nop" ::: "memory");
    }
}
static void select_chip(void) { NFCONT = ENABLE | CE1; dsb(); }
static void deselect(void) { NFCONT = ENABLE | CE0 | CE1; dsb(); }
static int ready(void)
{
    /* Allow tWB before sampling ready; bounded polling, not calibrated time. */
    delay_bus();
    for (unsigned n = 2000000u; n; --n) {
        if (NFSTAT & 1u) return 0;
    }
    console_write("NAND ready timeout; cold restart if repeated\r\n");
    return -1;
}
static void hex8(uint8_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc(h[v >> 4]);
    uart_putc(h[v & 15u]);
}
static void dump(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        if ((i & 15u) == 0u) console_reg("OFFSET=", i);
        hex8(p[i]);
        uart_putc(' ');
        if ((i & 15u) == 15u || i + 1u == n) console_write("\r\n");
    }
}
static void get_id(uint8_t *p)
{
    select_chip();
    NFCMD = 0x90u;
    NFADDR = 0;
    delay_bus();
    for (unsigned i = 0; i < 5u; ++i) p[i] = NFDATA;
    deselect();
}
static int same(const uint8_t *a, const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void show_id(void)
{
    console_write("NAND CE0 ID: ");
    for (unsigned i = 0; i < 5u; ++i) { hex8(id[i]); uart_putc(' '); }
    console_write("\r\n");
}
static void regs(void)
{
    console_reg("CLK_GATE_IP1=", GATE);
    console_reg("MP0_1CON=", MP01);
    console_reg("MP0_3CON=", MP03);
    console_reg("MP0_6CON=", MP06);
    if (GATE & NF_GATE) {
        console_reg("NFCONF=", NFCONF);
        console_reg("NFCONT=", NFCONT);
        console_reg("NFSTAT=", NFSTAT);
    }
}
static int init(void)
{
    uint8_t other[5];
    initialized = geometry = page_limit = 0;
    bbt_valid = 0; armed = NO_BLOCK;
    GATE |= NF_GATE;
    dsb();
    if (!(GATE & NF_GATE)) return -1;
    /* Deassert both controller chip selects before routing CE0. */
    deselect();
    /* Slow asynchronous timings, raw 8-bit SLC, 2 KiB/5 cycles. */
    NFCONF = (3u << 23) | (7u << 12) | (7u << 8) | (7u << 4) | (1u << 1);
    /* MP0_3[0..7] are NAND controls/RnB; select NAND for all. */
    MP03 = 0x22222222u;
    MP06 = 0x22222222u;
    MP01 = (MP01 & ~(15u << 8)) | (3u << 8);
    dsb();
    select_chip();
    NFCMD = 0xffu;
    if (ready()) { deselect(); return -1; }
    deselect();
    get_id(id);
    get_id(other);
    show_id();
    if (!same(id, other, 5u) || id[0] == 0u || id[0] == 0xffu ||
        id[1] == 0u || id[1] == 0xffu) {
        console_write("NAND ID invalid or unstable\r\n");
        return -1;
    }
    initialized = 1;
    /* Only known candidate IDs and expected extended geometry. */
    geometry = ((id[0] == 0xecu && id[1] == 0xdcu) ||
                (id[0] == 0x01u && id[1] == 0xd3u)) &&
               (id[3] & 0x7fu) == 0x15u;
    page_limit = geometry ? HALF_PAGES : 0u;
    console_write(geometry ? "NAND INIT PASS (candidate geometry, raw CE0)\r\n" :
                  "NAND ID PASS; unknown geometry, page operations disabled\r\n");
    return 0;
}
static int begin_read(unsigned page, unsigned col)
{
    if (!initialized || !geometry || page >= page_limit || col >= PAGE + OOB) return -1;
    select_chip();
    NFCMD = 0x00u;
    NFADDR = (uint8_t)col;
    NFADDR = (uint8_t)(col >> 8);
    NFADDR = (uint8_t)page;
    NFADDR = (uint8_t)(page >> 8);
    NFADDR = (uint8_t)(page >> 16);
    NFCMD = 0x30u;
    if (ready()) { deselect(); return -1; }
    return 0;
}
static int read_page(unsigned page, uint8_t *p)
{
    if (begin_read(page, 0)) return -1;
    for (unsigned i = 0; i < PAGE + OOB; ++i) p[i] = NFDATA;
    deselect();
    return 0;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int prefix(const char *a, const char *b)
{
    while (*b) if (*a++ != *b++) return 0;
    return 1;
}
static int number(const char *s, unsigned *v)
{
    unsigned n = 0;
    if (!*s) return -1;
    while (*s >= '0' && *s <= '9') {
        if (n > 100000u) return -1;
        n = n * 10u + (unsigned)(*s++ - '0');
    }
    if (*s || n >= LIMIT_PAGES) return -1;
    *v = n;
    return 0;
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (le16(p + 2) << 16);
}
static uint16_t onfi_crc(const uint8_t *p)
{
    uint16_t crc = 0x4f4eu;
    for (unsigned i = 0; i < 254u; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (unsigned b = 0; b < 8u; ++b)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x8005u : 0u));
    }
    return crc;
}
static int onfi(void)
{
    static const uint8_t sig[4] = {'O','N','F','I'};
    static const uint8_t known[5] = {0x01,0xd3,0xd1,0x95,0x58};
    uint8_t signature[4];
    unsigned good = 0, selected = 0;
    if (!initialized || !geometry) return -1;
    page_limit = HALF_PAGES;
    bbt_valid = 0; armed = NO_BLOCK;
    select_chip(); NFCMD = 0x90u; NFADDR = 0x20u; delay_bus();
    for (unsigned i = 0; i < 4u; ++i) signature[i] = NFDATA;
    deselect();
    if (!same(signature, sig, 4u)) {
        console_write("ONFI signature FAIL; keeping 512 MiB limit\r\n");
        return -1;
    }
    select_chip(); NFCMD = 0xecu; NFADDR = 0;
    if (ready()) { deselect(); return -1; }
    for (unsigned i = 0; i < sizeof(param); ++i) param[i] = NFDATA;
    deselect();
    for (unsigned i = 0; i < 3u; ++i) {
        const uint8_t *p = param + 256u*i;
        if (same(p, sig, 4u) && onfi_crc(p) == le16(p + 254)) {
            if (good && !same(param + 256u*selected + 80u, p + 80u, 33u)) {
                console_write("ONFI valid copies disagree; refusing expansion\r\n");
                return -1;
            }
            selected = i; ++good;
        }
    }
    console_reg("ONFI valid CRC copies=", good);
    if (!good) return -1;
    const uint8_t *p = param + 256u*selected;
    console_write("ONFI model: ");
    for (unsigned i = 44; i < 64u; ++i)
        uart_putc(p[i] >= 32u && p[i] <= 126u ? (char)p[i] : '?');
    console_write("\r\n");
    console_reg("Data bytes/page=", le32(p+80));
    console_reg("Spare bytes/page=", le16(p+84));
    console_reg("Pages/block=", le32(p+92));
    console_reg("Blocks/LUN=", le32(p+96));
    console_reg("LUN count=", p[100]);
    console_reg("Address cycles=", p[101]);
    console_reg("Bits/cell=", p[102]);
    console_reg("Minimum ECC bits/512B=", p[112]);
    if (!same(id, known, 5u) || le32(p+80) != PAGE ||
        le16(p+84) != OOB || le32(p+92) != PPB ||
        le32(p+96) != 4096u || p[100] != 2u ||
        p[101] != 0x23u || p[102] != 1u || p[112] != 1u ||
        (le16(p+6) & 1u)) {
        console_write("ONFI geometry not expected; keeping 512 MiB limit\r\n");
        return -1;
    }
    page_limit = LIMIT_PAGES;
    console_write("ONFI PASS: 1 GiB geometry; full CE0 address range enabled\r\n");
    return 0;
}
static uint32_t fingerprint(const uint8_t *p, unsigned n)
{
    uint32_t v = 2166136261u;
    for (unsigned i = 0; i < n; ++i) v = (v ^ p[i]) * 16777619u;
    return v;
}
static int boundary(void)
{
    static const unsigned pages[] = {0u, 1u, HALF_PAGES-1u, HALF_PAGES,
                                     HALF_PAGES+1u, LIMIT_PAGES-1u};
    unsigned different = 0;
    if (page_limit != LIMIT_PAGES) {
        console_write("Run nand onfi successfully first.\r\n"); return -1;
    }
    for (unsigned i = 0; i < sizeof(pages)/sizeof(pages[0]); ++i) {
        unsigned p = pages[i];
        if (read_page(p, buf) || read_page(p, copy) ||
            !same(buf, copy, PAGE + OOB)) return -1;
        console_reg("Boundary page=", p);
        console_reg("Raw fingerprint=", fingerprint(buf, PAGE + OOB));
    }
    for (unsigned i = 0; i < 4u; ++i) {
        if (read_page(i, buf) || read_page(i+HALF_PAGES, copy)) return -1;
        if (!same(buf, copy, PAGE + OOB)) ++different;
    }
    console_reg("Different low/high page pairs=", different);
    console_write(different ? "BOUNDARY PASS: stable samples, low/high data differ\r\n" :
                  "BOUNDARY INCONCLUSIVE: low/high samples identical (blank or alias)\r\n");
    return different ? 0 : -1;
}
static int bench(void)
{
    uint32_t ctrl, start, end, overflow, bit = 1u<<31, sum = 0;
    unsigned read = 0;
    if (page_limit < 512u) return -1;
    __asm__ volatile("mrc p15,0,%0,c9,c12,0" : "=r"(ctrl));
    __asm__ volatile("mcr p15,0,%0,c9,c14,2" :: "r"(bit) : "memory");
    __asm__ volatile("mcr p15,0,%0,c9,c12,3" :: "r"(bit) : "memory");
    uint32_t mode = (ctrl & ~8u) | 5u;
    __asm__ volatile("mcr p15,0,%0,c9,c12,0" :: "r"(mode) : "memory");
    __asm__ volatile("mcr p15,0,%0,c9,c12,1" :: "r"(bit) : "memory");
    dsb(); isb();
    __asm__ volatile("mrc p15,0,%0,c9,c13,0" : "=r"(start));
    for (; read < 512u; ++read) {
        if (read_page(read, buf)) break;
        sum ^= fingerprint(buf, PAGE + OOB);
    }
    dsb(); isb();
    __asm__ volatile("mrc p15,0,%0,c9,c13,0" : "=r"(end));
    __asm__ volatile("mrc p15,0,%0,c9,c12,3" : "=r"(overflow));
    __asm__ volatile("mcr p15,0,%0,c9,c12,2" :: "r"(bit) : "memory");
    isb();
    console_reg("Main bytes=", read * PAGE);
    console_reg("OOB bytes=", read * OOB);
    console_reg("Read+hash cycles=", end-start);
    console_reg("Fingerprint XOR=", sum);
    if (read != 512u || (overflow & bit) || end == start) {
        console_write("NAND BENCH FAIL\r\n"); return -1;
    }
    console_write("NAND BENCH PASS (includes OOB, hash, polling; CPU clock needed for MiB/s)\r\n");
    return 0;
}
static void random_column(unsigned col)
{
    NFCMD = 0x05u;
    NFADDR = (uint8_t)col;
    NFADDR = (uint8_t)(col >> 8);
    NFCMD = 0xe0u;
    delay_bus();
}
static int ecc_probe(unsigned page)
{
    unsigned completed = 0, blank = 0, nonzero = 0;
    if (read_page(page, buf)) return -1;
    uint32_t saved_conf = ECCCONF, saved_cont = ECCCONT & ~(1u<<2);
    for (unsigned sector = 0; sector < 4u; ++sector) {
        unsigned col = sector * 512u, parity = PAGE + 12u + sector*13u;
        unsigned erased = 1;
        for (unsigned i=0; i<512u; ++i) if (buf[col+i] != 0xffu) erased=0;
        for (unsigned i=0; i<13u; ++i) if (buf[parity+i] != 0xffu) erased=0;
        if (erased) { ++blank; continue; }
        if (begin_read(page, col)) break;
        /* Experimental decode: feed data and stored parity through NFDATA
           using random data output; no writes to NAND data register. */
        NFCONT |= 1u<<7;
        ECCCONF = (511u<<16)|3u;
        ECCSTAT = DECODE_DONE;
        ECCCONT = saved_cont & ~(1u<<16);
        NFCONT |= 1u<<5;
        ECCCONT |= 1u<<2;
        NFCONT &= ~(1u<<7);
        dsb();
        for (unsigned i=0; i<512u; ++i) copy[col+i] = NFDATA;
        random_column(parity);
        for (unsigned i=0; i<13u; ++i) copy[parity+i] = NFDATA;
        NFCONT |= 1u<<7;
        dsb();
        unsigned budget = 2000000u;
        while (budget && ((ECCSTAT & (ECC_BUSY|DECODE_DONE)) != DECODE_DONE)) --budget;
        console_reg("ECC sector=", sector);
        console_reg("NFECCSTAT=", ECCSTAT);
        if (budget) {
            uint32_t sec=ECCSECSTAT;
            console_reg("NFECCSECSTAT=", sec);
            if (sec & 31u) ++nonzero;
            for (unsigned i=0;i<4u;++i) console_reg("ECC ERL=", ECCREG(0xc0u+i*4u));
            for (unsigned i=0;i<2u;++i) console_reg("ECC ERP=", ECCREG(0xf0u+i*4u));
        }
        deselect();
        if (!budget || !same(buf+col,copy+col,512u) ||
            !same(buf+parity,copy+parity,13u)) break;
        ++completed;
    }
    ECCCONF = saved_conf;
    ECCCONT = saved_cont;
    ECCSTAT = DECODE_DONE;
    deselect();
    console_reg("ECC completed sectors=",completed);
    console_reg("Erased sectors skipped=",blank);
    console_reg("Nonzero ECC status count=",nonzero);
    if (completed+blank != 4u) {
        console_write("ECC PROBE FAIL: timeout or data mismatch; raw reads remain available\r\n");
        return -1;
    }
    console_write("ECC PROBE COMPLETE: diagnostic status only, correction not enabled\r\n");
    return 0;
}

static int autotest(void)
{
    if (init() || !geometry) return -1;
    /* Compare raw repeated reads; this does not validate ECC or contents. */
    for (unsigned page = 0; page < 4u; ++page) {
        if (read_page(page, buf) || read_page(page, copy) ||
            !same(buf, copy, PAGE + OOB)) return -1;
        console_reg("Repeated read matches page=", page);
    }
    console_write("NAND AUTOTEST PASS: stable ID and four repeat reads (no ECC)\r\n");
    return 0;
}
static int scan(unsigned blocks)
{
    unsigned suspect = 0;
    if (!blocks || blocks > page_limit / PPB) return -1;
    /* Report candidate markers only, not a definitive bad-block table. */
    for (unsigned b = 0; b < blocks; ++b) {
        unsigned bad = 0;
        for (unsigned p = 0; p < 2u; ++p) {
            if (begin_read(b * PPB + p, PAGE)) return -1;
            uint8_t marker = NFDATA;
            deselect();
            if (marker != 0xffu) bad = 1;
        }
        if (bad) { ++suspect; console_reg("OOB marker candidate block=", b); }
        if ((b & 1023u) == 1023u) console_reg("Scan blocks complete=", b+1u);
    }
    console_reg("Blocks sampled=", blocks);
    console_reg("Non-FF first OOB candidates=", suspect);
    return 0;
}

/* In-memory, conservative marker table. Never writes factory markers. */
static int marker_bad(unsigned block)
{
    static const unsigned pages[] = {0u,1u,PPB-1u};
    unsigned bad = 0;
    for (unsigned i=0; i<3u; ++i) {
        if (begin_read(block*PPB+pages[i], PAGE)) return -1;
        if (NFDATA != 0xffu) bad=1;
        deselect();
    }
    return (int)bad;
}
static void quarantine(unsigned block)
{
    bbt[block>>3] |= (uint8_t)(1u<<(block&7u));
}
static int bbt_scan(void)
{
    unsigned count=0;
    bbt_valid=0; armed=NO_BLOCK;
    if (page_limit != LIMIT_PAGES) return -1;
    for (unsigned i=0;i<sizeof(bbt);++i) bbt[i]=0;
    for (unsigned b=0;b<BLOCKS;++b) {
        int rc=marker_bad(b);
        if (rc<0) return -1;
        if (rc) { quarantine(b); ++count; console_reg("BBT candidate=",b); }
        if ((b&1023u)==1023u) console_reg("BBT scanned=",b+1u);
    }
    bbt_valid=1;
    console_reg("BBT candidates=",count);
    console_write("BBT READY (RAM only; first, second and last page markers)\r\n");
    return 0;
}
static int is_ff(const uint8_t *p, unsigned n)
{
    for(unsigned i=0;i<n;++i) if(p[i]!=0xffu) return 0;
    return 1;
}
static int blank_block(unsigned b)
{
    if (!bbt_valid || page_limit!=LIMIT_PAGES || b<64u || b>=BLOCKS-32u ||
        (bbt[b>>3] & (1u<<(b&7u)))) return -1;
    int m=marker_bad(b);
    if(m) { if(m>0) quarantine(b); return -1; }
    for(unsigned p=0;p<PPB;++p) {
        if(read_page(b*PPB+p,buf)) return -1;
        if(!is_ff(buf,PAGE+OOB)) return 0;
        if(read_page(b*PPB+p,copy) || !same(buf,copy,PAGE+OOB)) return -1;
    }
    return 1;
}
static int candidates(void)
{
    unsigned found=0;
    if(!bbt_valid) return -1;
    /* Sample only. A blank block can still belong to a live partition. */
    for(unsigned b=BLOCKS-33u;b>=BLOCKS-64u;--b) {
        if(blank_block(b)==1) {
            console_reg("Blank candidate block=",b);
            console_write("Decimal block: "); console_dec(b); console_write("\r\n");
            if(++found==3u) break;
        }
    }
    console_write("Candidates are NOT proven free/reserved. Verify partition map and backup before arming.\r\n");
    return found?0:-1;
}
static int prepare_scratch(unsigned block)
{
    armed=NO_BLOCK;
    if(blank_block(block)!=1) {
        console_write("SCRATCH REFUSED: not twice-read all-FF, protected, or BBT invalid\r\n");
        return -1;
    }
    armed=block;
    console_reg("Scratch armed (one use, RAM only)=",block);
    console_write("WARNING: blank is not proof of ownership. Only continue after backup and partition review.\r\n");
    console_write("Explicit command: nand rwtest BLOCK (programs and erases the armed block)\r\n");
    return 0;
}
static void row_address(unsigned page)
{
    NFADDR=(uint8_t)page; NFADDR=(uint8_t)(page>>8); NFADDR=(uint8_t)(page>>16);
}
static int write_status(void)
{
    NFCMD=0x70u;
    delay_bus();
    uint8_t st=NFDATA;
    console_reg("Write status=",st);
    return ((st&0xc1u)==0xc0u)?0:-1;
}
static int writable(void)
{
    select_chip(); NFCMD=0x70u; delay_bus();
    uint8_t st=NFDATA; deselect();
    return ((st&0xc0u)==0xc0u)?0:-1;
}
static int write_wait(void)
{
    /* tBERS max 10 ms; bounded generous polling budget, not a calibrated timeout. */
    delay_bus();
    for(unsigned n=20000000u;n;--n) if(NFSTAT&1u) return write_status();
    console_write("Write/erase timeout; cold restart, do not retry blindly\r\n");
    return -1;
}
static int ecc_wait(uint32_t flag)
{
    for(unsigned n=2000000u;n;--n)
        if((ECCSTAT&(ECC_BUSY|flag))==flag) return 0;
    return -1;
}
static void ecc_begin(uint32_t mode)
{
    NFCONT|=1u<<7;
    ECCCONF=(511u<<16)|3u;
    ECCSTAT=DECODE_DONE|ENCODE_DONE;
    ECCCONT=mode;
    NFCONT|=1u<<5;
    ECCCONT=mode|(1u<<2);
    NFCONT&=~(1u<<7);
    dsb();
}
/* Only called inside a one-shot, explicitly armed scratch experiment. */
static int program_test_page(unsigned page,uint8_t *data,int encode)
{
    uint32_t conf=ECCCONF,cont=ECCCONT&~(1u<<2);
    int rc=-1;
    if(page>=page_limit || writable()) return -1;
    select_chip(); NFCMD=0x80u; NFADDR=0; NFADDR=0; row_address(page);
    for(unsigned sec=0;sec<4u;++sec) {
        if(encode) ecc_begin(1u<<16);
        for(unsigned i=0;i<512u;++i) NFDATA=data[sec*512u+i];
        NFCONT|=1u<<7;
        dsb();
        if(encode) {
            if(ecc_wait(ENCODE_DONE)) {
                console_write("ECC encode timeout; aborting unconfirmed program\r\n");
                goto abort;
            }
            for(unsigned i=0;i<13u;++i)
                data[PAGE+12u+sec*13u+i]=(uint8_t)(ECCREG(0x90u+(i>>2)*4u)>>((i&3u)*8u));
        }
    }
    for(unsigned i=0;i<OOB;++i) NFDATA=data[PAGE+i];
    dsb(); NFCMD=0x10u;
    rc=write_wait();
    goto finish;
abort:
    NFCMD=0xffu; (void)ready();
finish:
    NFCONT|=1u<<7;
    ECCCONF=conf; ECCCONT=cont; ECCSTAT=DECODE_DONE|ENCODE_DONE;
    deselect();
    return rc;
}
/* Decode into RAM only. Do not program corrected bytes into NAND. */
static int corrected_page(unsigned page,uint8_t *out,unsigned *errors)
{
    uint32_t conf=ECCCONF,cont=ECCCONT&~(1u<<2);
    unsigned total=0;
    int rc=-1;
    if(read_page(page,buf)) return -1;
    for(unsigned sec=0;sec<4u;++sec) {
        unsigned col=sec*512u,parity=PAGE+12u+sec*13u;
        uint8_t *dst=out+col;
        if(is_ff(buf+col,512u)&&is_ff(buf+parity,13u)) {
            for(unsigned i=0;i<512u;++i) dst[i]=0xffu;
            continue;
        }
        if(begin_read(page,col)) goto finish;
        ecc_begin(0);
        for(unsigned i=0;i<512u;++i) dst[i]=NFDATA;
        random_column(parity);
        for(unsigned i=0;i<13u;++i) copy[parity+i]=NFDATA;
        NFCONT|=1u<<7;
        dsb();
        if(ecc_wait(DECODE_DONE)) goto finish;
        uint32_t status=ECCSECSTAT;
        unsigned n=status&15u;
        console_reg("ECC sector status=",status);
        if(!same(dst,buf+col,512u)||!same(copy+parity,buf+parity,13u)||n>8u) goto finish;
        /* A nonblank sector must not be accepted as hardware free-page. */
        if(status&(1u<<29)) goto finish;
        for(unsigned i=0;i<n;++i) {
            unsigned pos=(ECCREG(0xc0u+(i>>1)*4u)>>((i&1u)*16u))&1023u;
            unsigned pat=(ECCREG(0xf0u+(i>>2)*4u)>>((i&3u)*8u))&255u;
            if(pos>=525u||!pat) goto finish;
            if(pos<512u) dst[pos]^=(uint8_t)pat;
            console_reg("Corrected byte location=",pos);
            console_reg("Error pattern=",pat);
        }
        total+=n;
        deselect();
    }
    *errors=total; rc=0;
finish:
    NFCONT|=1u<<7;
    ECCCONF=conf; ECCCONT=cont; ECCSTAT=DECODE_DONE|ENCODE_DONE;
    deselect();
    return rc;
}
static int erase_scratch(unsigned b)
{
    if(writable()) return -1;
    select_chip(); NFCMD=0x60u; row_address(b*PPB);
    dsb(); NFCMD=0xd0u;
    int rc=write_wait(); deselect();
    if(rc) return -1;
    for(unsigned i=0;i<PPB;++i)
        if(read_page(b*PPB+i,buf)||!is_ff(buf,PAGE+OOB)) return -1;
    return 0;
}
static int rwtest(unsigned b)
{
    unsigned permit=armed, errors=0, page=b*PPB;
    armed=NO_BLOCK;
    if(b!=permit||blank_block(b)!=1) {
        console_write("RWTEST REFUSED: arm exact blank block first; authorization consumed\r\n");
        return -1;
    }
    console_write("RWTEST: two pages in one authorized scratch block; final erase only on success\r\n");
    for(unsigned i=0;i<PAGE;++i) expected[i]=(uint8_t)(i*37u+(i>>3)+0x5au);
    for(unsigned i=PAGE;i<PAGE+OOB;++i) expected[i]=0xffu;
    if(program_test_page(page,expected,1) || read_page(page,buf) ||
       !same(buf,expected,PAGE+OOB) || is_ff(expected+PAGE+12u,52u)) goto fail;
    console_write("PAGE PROGRAM + ECC ENCODE + RAW VERIFY PASS\r\n");
    if(corrected_page(page,injected,&errors)||errors||!same(injected,expected,PAGE)) goto fail;
    console_write("CLEAN ECC READ PASS\r\n");
    for(unsigned i=0;i<PAGE+OOB;++i) injected[i]=expected[i];
    injected[17]^=1u;
    for(unsigned i=0;i<8u;++i) injected[512u+23u+i*17u]^=1u;
    if(program_test_page(page+1u,injected,0)||read_page(page+1u,buf)||
       !same(buf,injected,PAGE+OOB)) goto fail;
    if(corrected_page(page+1u,injected,&errors)||errors!=9u||
       !same(injected,expected,PAGE)) goto fail;
    console_write("BCH8 INJECTION PASS: 1 and 8 single-bit errors restored in RAM\r\n");
    if(erase_scratch(b)) goto fail;
    console_write("BLOCK ERASE + ALL 64 PAGES RAW FF VERIFY PASS\r\n");
    console_write("NAND RWTEST PASS\r\n");
    return 0;
fail:
    quarantine(b);
    console_write("NAND RWTEST FAIL: block quarantined in RAM; no automatic retry/cleanup. Preserve log.\r\n");
    return -1;
}
static int phase3(void)
{
    if(nand_command("testall")) return -1;
    if(bbt_scan()) return -1;
    return candidates();
}

int nand_command(const char *args)
{
    unsigned n;
    if (timer_running()) {
        console_write("Stop timer before NAND diagnostics.\r\n");
        return -1;
    }
    if (equal(args, "phase3")) return phase3();
    if (equal(args, "regs")) { regs(); return 0; }
    if (equal(args, "init")) return init();
    if (equal(args, "testall")) {
        static const char *const steps[] = {
            "autotest", "onfi", "info", "boundary", "bench", "scan all", "ecc 0"
        };
        unsigned passed = 0, failed = 0, skipped = 0;
        int full = 0;
        console_write("\r\n=== NAND TEST ALL (READ ONLY) ===\r\n");
        for (unsigned i = 0; i < sizeof(steps)/sizeof(steps[0]); ++i) {
            console_write("\r\n[NAND] "); console_write(steps[i]); console_write("\r\n");
            if (i > 0u && (!initialized || !geometry)) {
                ++skipped; console_write("[NAND] SKIP: initialization failed\r\n"); continue;
            }
            if ((i == 3u || i == 5u) && !full) {
                ++skipped; console_write("[NAND] SKIP: full geometry not verified\r\n"); continue;
            }
            int rc = nand_command(steps[i]);
            if (i == 1u) full = (rc == 0 && page_limit == LIMIT_PAGES);
            if (rc) { ++failed; console_write("[NAND] FAIL/INCONCLUSIVE\r\n"); }
            else { ++passed; console_write("[NAND] COMPLETE\r\n"); }
        }
        console_reg("Completed=", passed);
        console_reg("Failed/inconclusive=", failed);
        console_reg("Skipped=", skipped);
        console_write("ECC completion is diagnostic only; no correction or data integrity guarantee.\r\n");
        console_write("=== NAND TEST ALL END ===\r\n");
        return (failed || skipped) ? -1 : 0;
    }
    if (equal(args, "autotest")) {
        int rc = autotest();
        if (rc) console_write("NAND AUTOTEST FAIL\r\n");
        return rc;
    }
    if (!initialized) {
        console_write("Run nand init first.\r\n");
        return -1;
    }
    if (equal(args, "id")) { get_id(copy); show_id();
        if (!same(id, copy, 5u)) {
            console_write("NAND ID changed; reinitialize\r\n");
            initialized = geometry = page_limit = bbt_valid = 0; armed = NO_BLOCK;
            return -1;
        }
        return 0;
    }
    if (equal(args, "status")) {
        select_chip(); NFCMD = 0x70u; delay_bus();
        console_reg("NAND STATUS=", NFDATA); deselect(); return 0;
    }
    if (equal(args, "bbt")) return bbt_scan();
    if (equal(args, "candidates")) return candidates();
    if (prefix(args, "scratch ") && !number(args+8,&n)) return prepare_scratch(n);
    if (prefix(args, "rwtest ") && !number(args+7,&n)) return rwtest(n);
    if (equal(args, "onfi")) return onfi();
    if (equal(args, "boundary")) return boundary();
    if (equal(args, "bench")) return bench();
    if (prefix(args, "ecc ") && !number(args + 4, &n)) return ecc_probe(n);
    if (equal(args, "info")) {
        show_id();
        if (geometry) { console_write("Geometry: 2048+64 B, 64 pages/block\r\n"); console_reg("Enabled page count=",page_limit); }
        else console_write("Geometry unknown; reads disabled.\r\n");
        return 0;
    }
    if (!geometry) { console_write("Unknown geometry; reads disabled.\r\n"); return -1; }
    if (prefix(args, "read ") && !number(args + 5, &n)) {
        if (read_page(n, buf)) return -1;
        dump(buf, PAGE); return 0;
    }
    if (prefix(args, "oob ") && !number(args + 4, &n)) {
        if (read_page(n, buf)) return -1;
        dump(buf + PAGE, OOB); return 0;
    }
    if (equal(args, "scan")) return scan(64u);
    if (equal(args, "scan all")) return scan(page_limit / PPB);
    if (prefix(args, "scan ") && !number(args + 5, &n) && n && n <= page_limit / PPB) return scan(n);
    console_write("Usage: nand init/id/info/status/regs/autotest/read PAGE/oob PAGE/scan [N|all]/onfi/boundary/bench/ecc PAGE\r\n");
    return -1;
}
