#include "memory.h"

#define PAGES ((DDR_END - DDR_BASE) / PAGE_SIZE)
static uint32_t used[PAGES / 32u];
static unsigned free_pages;

static int reserved(uintptr_t p)
{
    return p < DDR_BASE + 0x100000u ||
           (p >= 0x40000000u && p < 0x40100000u) ||
           (p >= IMAGE_BASE && p < IMAGE_END);
}
static int busy(unsigned i)
{
    return (used[i >> 5] >> (i & 31u)) & 1u;
}
static void mark(unsigned i, int value)
{
    uint32_t bit = 1u << (i & 31u);
    if (value) {
        used[i >> 5] |= bit;
    } else {
        used[i >> 5] &= ~bit;
    }
}
void page_init(void)
{
    free_pages = 0;
    for (unsigned i = 0; i < PAGES / 32u; ++i) {
        used[i] = 0;
    }
    for (unsigned i = 0; i < PAGES; ++i) {
        if (reserved(DDR_BASE + (uintptr_t)i * PAGE_SIZE)) {
            mark(i, 1);
        } else {
            ++free_pages;
        }
    }
}
unsigned page_free_count(void) { return free_pages; }

uintptr_t page_alloc(unsigned count)
{
    if (!count || count > free_pages) {
        return 0;
    }
    unsigned run = 0;
    for (unsigned i = 0; i < PAGES; ++i) {
        run = busy(i) ? 0u : run + 1u;
        if (run == count) {
            unsigned first = i + 1u - count;
            for (unsigned j = first; j <= i; ++j) {
                mark(j, 1);
            }
            free_pages -= count;
            return DDR_BASE + (uintptr_t)first * PAGE_SIZE;
        }
    }
    return 0;
}
/* Trusted callers must release the original extent; no allocation ownership table yet. */
int page_free(uintptr_t addr, unsigned count)
{
    if (addr < DDR_BASE || addr >= DDR_END || (addr & (PAGE_SIZE - 1u)) ||
        !count || count > (DDR_END - addr) / PAGE_SIZE) {
        return -1;
    }
    unsigned first = (addr - DDR_BASE) / PAGE_SIZE;
    for (unsigned i = 0; i < count; ++i) {
        if (!busy(first + i) || reserved(addr + (uintptr_t)i * PAGE_SIZE)) {
            return -1;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        mark(first + i, 0);
    }
    free_pages += count;
    return 0;
}
void memory_info(void)
{
    console_write("Physical DDR: 1024 MiB\r\nReserved: 18 MiB\r\nFree pages: ");
    console_dec(free_pages);
    console_write(" (");
    console_dec(free_pages >> 8);
    console_write(" MiB, rounded down)\r\n");
}
int memory_selftest(void)
{
    unsigned before = page_free_count();
    uintptr_t a = page_alloc(4), b = page_alloc(2);
    if (!a || !b || (a < b + 2u * PAGE_SIZE && b < a + 4u * PAGE_SIZE)) {
        if (a) { (void)page_free(a, 4); }
        if (b) { (void)page_free(b, 2); }
        return -1;
    }
    volatile uint32_t *p = (volatile uint32_t *)a;
    unsigned errors = 0;
    for (unsigned pass = 0; pass < 2u; ++pass) {
        uint32_t mask = pass ? 0xaa55cc33u : 0x55aa33ccu;
        for (unsigned i = 0; i < 4096u; ++i) {
            p[i] = (uint32_t)(a + 4u * i) ^ mask;
        }
        cache_sync_range(a, 4u * PAGE_SIZE);
        for (unsigned i = 0; i < 4096u; ++i) {
            if (p[i] != ((uint32_t)(a + 4u * i) ^ mask)) {
                ++errors;
            }
        }
    }
    int ra = page_free(a, 4), rb = page_free(b, 2);
    return (errors || ra || rb || page_free_count() != before) ? -1 : 0;
}
