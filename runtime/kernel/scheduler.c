#include "scheduler.h"
#include "timebase.h"

#define THREADS 3u
#define STACK_WORDS 1024u
#define TEST_TICKS 12u
#define GUARD 0x51accafeu
#define SVC_FIQ_MASKED 0x53u

/* 线程0 是 Shell；线程1/2 是独立的测试线程。
 * 暂不允许线程调用分配器或串口，避免尚未实现的并发锁问题。 */
static struct irq_frame contexts[THREADS];
static uint32_t stacks[2][STACK_WORDS] __attribute__((aligned(8)));
static volatile uint32_t progress[2];
static uint32_t observed[2], slices[2];
static volatile unsigned running, error, current;

static uint32_t sentinel(unsigned thread, unsigned reg)
{
    return 0xa5000000u | (thread << 16) | reg;
}

static void copy_frame(struct irq_frame *dst, const struct irq_frame *src)
{
    /* 显式复制，避免 freestanding 编译时引入 memcpy。 */
    volatile uint32_t *d = dst->r;
    for (unsigned i = 0; i < 13u; ++i) {
        d[i] = src->r[i];
    }
    dst->sp = src->sp;
    dst->lr = src->lr;
    dst->pc = src->pc;
    dst->cpsr = src->cpsr;
    dst->reserved = 0;
}

static void init_worker(unsigned id)
{
    unsigned i = id - 1u;
    struct irq_frame *f = &contexts[id];
    progress[i] = 0;
    observed[i] = 0;
    slices[i] = 0;
    stacks[i][0] = GUARD;
    for (unsigned r = 0; r < 13u; ++r) {
        f->r[r] = sentinel(id, r);
    }
    f->r[0] = (uint32_t)(uintptr_t)&progress[i];
    f->sp = (uint32_t)(uintptr_t)&stacks[i][STACK_WORDS];
    f->lr = sentinel(id, 14u);
    f->pc = (uint32_t)(uintptr_t)scheduler_worker;
    f->cpsr = SVC_FIQ_MASKED; /* ARM SVC, IRQ 允许，FIQ 屏蔽 */
    f->reserved = 0;
}

static int verify_worker(unsigned id, const struct irq_frame *f)
{
    unsigned i = id - 1u;
    uint32_t top = (uint32_t)(uintptr_t)&stacks[i][STACK_WORDS];
    if (f->r[0] != (uint32_t)(uintptr_t)&progress[i] ||
        f->lr != sentinel(id, 14u) ||
        (f->sp != top && f->sp != top - 8u) ||
        f->pc < (uint32_t)(uintptr_t)scheduler_worker ||
        f->pc >= (uint32_t)(uintptr_t)scheduler_worker_end ||
        (f->cpsr & 0xffu) != SVC_FIQ_MASKED ||
        stacks[i][0] != GUARD) {
        return -1;
    }
    for (unsigned r = 2; r < 13u; ++r) {
        if (f->r[r] != sentinel(id, r)) {
            return -1;
        }
    }
    /* 必须执行过真实指令，而不是仅切换一个任务编号。 */
    uint32_t now = progress[i];
    if (now == observed[i]) {
        return -1;
    }
    observed[i] = now;
    ++slices[i];
    return 0;
}

struct irq_frame *scheduler_on_irq(struct irq_frame *frame)
{
    if (!running) {
        return frame;
    }
    unsigned id = current;
    copy_frame(&contexts[id], frame);
    if (id && verify_worker(id, frame)) {
        error = 1;
    }
    if (timer_test_failed()) {
        error = 1;
    }
    if (error || timer_ticks() >= TEST_TICKS) {
        /* 回到保存的 Shell 上下文。前台负责最终停止硬件。 */
        current = 0;
        running = 0;
        return &contexts[0];
    }
    /* 每次 Timer1 IRQ 轮转：Shell -> A -> B -> Shell。 */
    current = (id == THREADS - 1u) ? 0u : id + 1u;
    return &contexts[current];
}

int scheduler_selftest(void)
{
    if (timer_running() || running || (cpu_sctlr() & 5u) != 5u) {
        console_write("SCHED requires MMU/Cache ON and timer stopped\r\n");
        return -1;
    }
    if (timer_probe()) {
        return -1;
    }
    __asm__ volatile("cpsid i" ::: "memory");
    isb();
    current = 0;
    error = 0;
    init_worker(1);
    init_worker(2);
    console_write("SCHED: Shell -> A -> B, 12 timer IRQs\r\n");
    console_reg("A stack top=", contexts[1].sp);
    console_reg("B stack top=", contexts[2].sp);
    running = 1;
    if (timer_start_bounded(TEST_TICKS)) {
        running = 0;
        return -1;
    }

    /* 仅在 Shell 获得时间片时执行，绝不主动 yield。 */
    unsigned budget = 100000000u;
    while (running && --budget) {
        __asm__ volatile("nop" ::: "memory");
    }
    timer_stop();
    running = 0;

    console_reg("SCHED ticks=", timer_ticks());
    console_reg("A slices=", slices[0]);
    console_reg("B slices=", slices[1]);
    console_reg("A work=", progress[0]);
    console_reg("B work=", progress[1]);
    int ok = !error && !timer_test_failed() && budget &&
             timer_ticks() == TEST_TICKS && current == 0u &&
             slices[0] == 4u && slices[1] == 4u &&
             progress[0] && progress[1] &&
             stacks[0][0] == GUARD && stacks[1][0] == GUARD;
    console_write(ok ? "SCHED PASS: preemptive contexts, stacks and register guards\r\n" :
                       "SCHED FAIL: context, progress or tick count\r\n");
    return ok ? 0 : -1;
}
