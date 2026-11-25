#ifndef RUNTIME_TASK_H
#define RUNTIME_TASK_H

#include "scheduler.h"

typedef void (*thread_entry_t)(void *arg);

/* 创建只能在调度实验开始前调用；线程入口返回时自动退出。 */
int thread_create(thread_entry_t entry, void *arg);
int thread_sleep(unsigned ticks);
void thread_exit(void) __attribute__((noreturn));

struct irq_frame *task_on_irq(struct irq_frame *frame);
int task_selftest(void);

#endif
