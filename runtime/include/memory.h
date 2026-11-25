#ifndef RUNTIME_MEMORY_H
#define RUNTIME_MEMORY_H
#include "platform.h"

int mmu_enable(void);
int cache_enable(void);
void cache_sync_range(uintptr_t start, size_t bytes);
void mmu_info(void);
void page_init(void);
uintptr_t page_alloc(unsigned count);
int page_free(uintptr_t addr, unsigned count);
unsigned page_free_count(void);
int memory_selftest(void);
void memory_info(void);
void runtime_autotest(void);
void shell_run(void) __attribute__((noreturn));
#endif
