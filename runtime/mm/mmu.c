#include "memory.h"

#define SECTION_SIZE (1u << 20)
#define SEC_TYPE     2u
#define SEC_XN       (1u << 4)
#define SEC_AP_RW    (1u << 10) /* privileged RW, user no access */
#define SEC_NORMAL   ((1u << 12) | (1u << 3) | (1u << 2)) /* TEX=001 C=B=1 */
#define SCTLR_M      1u
#define SCTLR_C      (1u << 2)
#define SCTLR_I      (1u << 12)

/* ARMv7 short descriptors, TTBR0 N=0: 4096 sections, 16 KiB aligned. */
static uint32_t l1[4096] __attribute__((aligned(16384)));

static void map_section(uint32_t address, uint32_t attrs)
{
    l1[address >> 20] = address | SEC_TYPE | SEC_AP_RW | attrs;
}

static void invalidate_data_caches(void)
{
    uint32_t clidr, old;
    __asm__ volatile("mrc p15,1,%0,c0,c0,1" : "=r"(clidr));
    __asm__ volatile("mrc p15,2,%0,c0,c0,0" : "=r"(old));
    dsb();
    for (unsigned level = 0; level < ((clidr >> 24) & 7u); ++level) {
        unsigned type = (clidr >> (level * 3u)) & 7u;
        if (type < 2u || type > 4u) {
            continue;
        }
        uint32_t sel = level << 1, ccsidr;
        __asm__ volatile("mcr p15,2,%0,c0,c0,0" :: "r"(sel) : "memory");
        isb();
        __asm__ volatile("mrc p15,1,%0,c0,c0,0" : "=r"(ccsidr));
        unsigned line_shift = (ccsidr & 7u) + 4u;
        unsigned max_way = (ccsidr >> 3) & 1023u;
        unsigned max_set = (ccsidr >> 13) & 32767u;
        unsigned way_shift = max_way ? (unsigned)__builtin_clz(max_way) : 0u;
        for (unsigned way = 0; way <= max_way; ++way) {
            for (unsigned set = 0; set <= max_set; ++set) {
                uint32_t operand = sel | (set << line_shift) |
                                   (max_way ? way << way_shift : 0u);
                __asm__ volatile("mcr p15,0,%0,c7,c6,2" :: "r"(operand) : "memory");
            }
        }
    }
    __asm__ volatile("mcr p15,2,%0,c0,c0,0" :: "r"(old) : "memory");
    dsb();
    isb();
}

int mmu_enable(void)
{
    uint32_t ctl = cpu_sctlr(), zero = 0, domain = 1u;
    if (ctl & SCTLR_M) {
        return 0;
    }
    if (ctl & SCTLR_C) {
        return -1;
    }
    for (unsigned i = 0; i < 4096; ++i) {
        l1[i] = 0;
    }
    for (uint32_t a = DDR_BASE; a < DDR_END; a += SECTION_SIZE) {
        map_section(a, SEC_NORMAL | (a == IMAGE_BASE ? 0u : SEC_XN));
    }
    /* Strongly ordered, XN, privileged-only MMIO. Everything else faults. */
    map_section(0xe0100000u, SEC_XN);
    map_section(0xe0200000u, SEC_XN);
    map_section(0xe2700000u, SEC_XN);
    map_section(0xe2900000u, SEC_XN);
    map_section(0xf0000000u, SEC_XN);
    map_section(0xf1400000u, SEC_XN);

    invalidate_data_caches(); /* only while D-cache is disabled */
    dsb();
    __asm__ volatile("mcr p15,0,%0,c2,c0,2" :: "r"(zero) : "memory"); /* TTBCR */
    __asm__ volatile("mcr p15,0,%0,c2,c0,0" :: "r"((uint32_t)(uintptr_t)l1) : "memory");
    __asm__ volatile("mcr p15,0,%0,c3,c0,0" :: "r"(domain) : "memory"); /* domain 0 client */
    __asm__ volatile("mcr p15,0,%0,c8,c7,0" :: "r"(zero) : "memory");
    __asm__ volatile("mcr p15,0,%0,c7,c5,0" :: "r"(zero) : "memory");
    __asm__ volatile("mcr p15,0,%0,c7,c5,6" :: "r"(zero) : "memory");
    dsb();
    isb();
    /* Short descriptor TEX/AP encoding, low vectors, no remap or AFE. */
    ctl &= ~((1u << 28) | (1u << 29) | (1u << 13));
    ctl |= SCTLR_M | SCTLR_I;
    __asm__ volatile("mcr p15,0,%0,c1,c0,0" :: "r"(ctl) : "memory");
    isb();
    return (cpu_sctlr() & SCTLR_M) ? 0 : -1;
}
int cache_enable(void)
{
    uint32_t ctl = cpu_sctlr();
    if (!(ctl & SCTLR_M)) {
        return -1;
    }
    if (!(ctl & SCTLR_C)) {
        invalidate_data_caches();
        dsb();
        ctl |= SCTLR_C | SCTLR_I;
        __asm__ volatile("mcr p15,0,%0,c1,c0,0" :: "r"(ctl) : "memory");
        isb();
    }
    return (cpu_sctlr() & SCTLR_C) ? 0 : -1;
}
void cache_sync_range(uintptr_t start, size_t bytes)
{
    if (bytes && (cpu_sctlr() & SCTLR_C)) {
        uint32_t ctr;
        __asm__ volatile("mrc p15,0,%0,c0,c0,1" : "=r"(ctr));
        uintptr_t line = 4u << ((ctr >> 16) & 15u);
        uintptr_t end = start + bytes;
        dsb();
        for (uintptr_t p = start & ~(line - 1u); p < end; p += line) {
            __asm__ volatile("mcr p15,0,%0,c7,c14,1" :: "r"(p) : "memory");
        }
    }
    dsb();
}
void mmu_info(void)
{
    uint32_t ctl = cpu_sctlr(), ttbr, dacr, ttbcr;
    __asm__ volatile("mrc p15,0,%0,c2,c0,0" : "=r"(ttbr));
    __asm__ volatile("mrc p15,0,%0,c2,c0,2" : "=r"(ttbcr));
    __asm__ volatile("mrc p15,0,%0,c3,c0,0" : "=r"(dacr));
    console_write(ctl & SCTLR_M ? "MMU: ON\r\n" : "MMU: OFF\r\n");
    console_write(ctl & SCTLR_I ? "I-Cache: ON\r\n" : "I-Cache: OFF\r\n");
    console_write(ctl & SCTLR_C ? "D-Cache: ON\r\n" : "D-Cache: OFF\r\n");
    console_reg("TTBR0=", ttbr);
    console_reg("TTBCR=", ttbcr);
    console_reg("DACR=", dacr);
}
