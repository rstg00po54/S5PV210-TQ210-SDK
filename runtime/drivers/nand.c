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
static uint8_t id[5], buf[PAGE + OOB], copy[PAGE + OOB];
static unsigned initialized, geometry, page_limit;
static uint8_t param[768];
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
int nand_command(const char *args)
{
    unsigned n;
    if (timer_running()) {
        console_write("Stop timer before NAND diagnostics.\r\n");
        return -1;
    }
    if (equal(args, "regs")) { regs(); return 0; }
    if (equal(args, "init")) return init();
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
            initialized = geometry = 0;
            return -1;
        }
        return 0;
    }
    if (equal(args, "status")) {
        select_chip(); NFCMD = 0x70u; delay_bus();
        console_reg("NAND STATUS=", NFDATA); deselect(); return 0;
    }
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
