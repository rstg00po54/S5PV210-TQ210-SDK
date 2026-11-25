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
/* Conservative 512 MiB CE0 diagnostic window; not a capacity claim. */
#define LIMIT_PAGES 262144u
static uint8_t id[5], buf[PAGE + OOB], copy[PAGE + OOB];
static unsigned initialized, geometry;

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
    initialized = geometry = 0;
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
    console_write(geometry ? "NAND INIT PASS (candidate geometry, raw CE0)\r\n" :
                  "NAND ID PASS; unknown geometry, page operations disabled\r\n");
    return 0;
}
static int read_page(unsigned page, uint8_t *p)
{
    if (!initialized || !geometry || page >= LIMIT_PAGES) return -1;
    select_chip();
    NFCMD = 0x00u;
    NFADDR = 0;
    NFADDR = 0;
    NFADDR = (uint8_t)page;
    NFADDR = (uint8_t)(page >> 8);
    NFADDR = (uint8_t)(page >> 16);
    NFCMD = 0x30u;
    if (ready()) { deselect(); return -1; }
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
    /* Report candidate markers only, not a definitive bad-block table. */
    for (unsigned b = 0; b < blocks; ++b) {
        unsigned bad = 0;
        for (unsigned p = 0; p < 2u; ++p) {
            if (read_page(b * PPB + p, buf)) return -1;
            if (buf[PAGE] != 0xffu) bad = 1;
        }
        if (bad) { ++suspect; console_reg("OOB marker candidate block=", b); }
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
    if (equal(args, "info")) {
        show_id();
        if (geometry) console_write("Candidate geometry: 2048+64 B, 64 pages/block; CE0 diagnostic limit 512 MiB, total capacity unverified.\r\n");
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
    if (prefix(args, "scan ") && !number(args + 5, &n) && n && n <= 4096u) return scan(n);
    console_write("Usage: nand init/id/info/status/regs/autotest/read PAGE/oob PAGE/scan [1..4096]\r\n");
    return -1;
}
