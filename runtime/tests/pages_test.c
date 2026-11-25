#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include "memory.h"

void cache_sync_range(uintptr_t start,size_t bytes)
{
    (void)start;
    (void)bytes;
}
void console_write(const char *s) { fputs(s,stdout); }
void console_dec(uint32_t n) { printf("%u",n); }

int main(void)
{
    void *p = mmap((void *)0x20100000, 0x100000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(p != (void *)-1);
    page_init();
    assert(page_free_count() == 257536);
    assert(!page_alloc(0));
    assert(!page_alloc(257537));
    assert(page_free(DDR_BASE, 1));
    assert(page_free(IMAGE_BASE, 1));
    uintptr_t a = page_alloc(16);
    assert(a == 0x20100000);
    assert(page_free_count() == 257520);
    assert(!memory_selftest());
    assert(page_free_count() == 257520);
    assert(!page_free(a, 16));
    assert(page_free(a, 16));
    assert(page_free_count() == 257536);

    /* Fill the first extent up to the reserved runtime image. */
    a = page_alloc(0x3f00);
    assert(a == 0x20100000);
    uintptr_t b = page_alloc(1);
    assert(b == 0x25000000);
    assert(!page_free(b, 1));
    assert(!page_free(a, 0x3f00));
    assert(page_free_count() == 257536);
    assert(!memory_selftest());
    assert(munmap(p, 0x100000) == 0);
    puts("PAGE ALLOCATOR HOST TEST PASS");
    return 0;
}
