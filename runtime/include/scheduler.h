#ifndef RUNTIME_SCHEDULER_H
#define RUNTIME_SCHEDULER_H
#include "platform.h"

/* 与 arch/arm/context.S 的偏移严格对应。仅支持 ARM SVC 内核线程。 */
struct irq_frame {
    uint32_t r[13];
    uint32_t sp, lr, pc, cpsr, reserved;
};
_Static_assert(sizeof(struct irq_frame) == 72, "IRQ frame size");
_Static_assert(offsetof(struct irq_frame, sp) == 52, "SP offset");
_Static_assert(offsetof(struct irq_frame, lr) == 56, "LR offset");
_Static_assert(offsetof(struct irq_frame, pc) == 60, "PC offset");
_Static_assert(offsetof(struct irq_frame, cpsr) == 64, "CPSR offset");

struct irq_frame *scheduler_on_irq(struct irq_frame *frame);
int scheduler_selftest(void);
void scheduler_worker(void);
extern const char scheduler_worker_end[];
#endif
