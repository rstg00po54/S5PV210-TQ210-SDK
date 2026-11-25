#include "task.h"
#include "memory.h"
#include "timebase.h"

#define TASK_COUNT 4u
#define STACK_PAGES 2u
#define STACK_BYTES (STACK_PAGES * PAGE_SIZE)
#define STACK_GUARD 0x7461736bu
#define TEST_LIMIT 48u

enum task_state {
    TASK_UNUSED, TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_EXITED
};

struct task {
    struct irq_frame frame;
    thread_entry_t entry;
    void *arg;
    uintptr_t stack;
    volatile enum task_state state;
    uint32_t wake_tick;
    unsigned dispatches;
    unsigned sleep_calls;
    unsigned wakes;
};

static struct task tasks[TASK_COUNT];
static volatile unsigned enabled;
static volatile unsigned current;
static volatile unsigned fault;
static unsigned sleeping_skips;

static void irq_lock(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    isb();
}

static void irq_unlock(void)
{
    __asm__ volatile("cpsie i" ::: "memory");
    isb();
}

/* 入口运行在自己的 SVC 栈；普通 C 函数返回即结束线程。 */
static void thread_bootstrap(void) __attribute__((noreturn));
static void thread_bootstrap(void)
{
    struct task *t = &tasks[current];
    t->entry(t->arg);
    thread_exit();
}

int thread_create(thread_entry_t entry, void *arg)
{
    if (!entry || enabled || timer_running()) {
        return -1;
    }
    unsigned id;
    for (id = 1; id < TASK_COUNT; ++id) {
        if (tasks[id].state == TASK_UNUSED) {
            break;
        }
    }
    if (id == TASK_COUNT) {
        return -1;
    }
    uintptr_t stack = page_alloc(STACK_PAGES);
    if (!stack) {
        return -1;
    }
    struct task *t = &tasks[id];
    t->stack = stack;
    t->entry = entry;
    t->arg = arg;
    t->wake_tick = 0;
    t->dispatches = 0;
    t->sleep_calls = 0;
    t->wakes = 0;
    *(volatile uint32_t *)stack = STACK_GUARD;
    for (unsigned r = 0; r < 13u; ++r) {
        t->frame.r[r] = 0;
    }
    t->frame.sp = (uint32_t)(stack + STACK_BYTES);
    t->frame.lr = (uint32_t)(uintptr_t)thread_exit;
    t->frame.pc = (uint32_t)(uintptr_t)thread_bootstrap;
    t->frame.cpsr = 0x53u; /* SVC/ARM, FIQ 禁止，IRQ 允许 */
    t->frame.reserved = 0;
    t->state = TASK_READY;
    return (int)id;
}

/* 只修改状态时屏蔽 IRQ。下一次时钟中断会换出睡眠线程。
 * 唤醒以 tick 为单位，不是精确毫秒延时。 */
int thread_sleep(unsigned delay)
{
    if (!enabled || !current || !delay || delay > TEST_LIMIT) {
        return -1;
    }
    irq_lock();
    struct task *t = &tasks[current];
    t->wake_tick = timer_ticks() + delay;
    ++t->sleep_calls;
    t->state = TASK_SLEEPING;
    irq_unlock();
    while (t->state == TASK_SLEEPING) {
        __asm__ volatile("nop" ::: "memory");
    }
    return 0;
}

void thread_exit(void)
{
    if (!enabled || !current) {
        runtime_halt();
    }
    irq_lock();
    tasks[current].state = TASK_EXITED;
    irq_unlock();
    /* 不允许已退出的线程继续执行，也不能在自己的栈上释放栈。 */
    for (;;) {
        __asm__ volatile("nop" ::: "memory");
    }
}

static void save_frame(struct irq_frame *d, const struct irq_frame *s)
{
    volatile uint32_t *r = d->r;
    for (unsigned i = 0; i < 13u; ++i) {
        r[i] = s->r[i];
    }
    d->sp = s->sp;
    d->lr = s->lr;
    d->pc = s->pc;
    d->cpsr = s->cpsr;
    d->reserved = 0;
}

static int valid_stack(const struct task *t, const struct irq_frame *f)
{
    return t->stack && f->sp >= t->stack + 4u &&
           f->sp <= t->stack + STACK_BYTES && !(f->sp & 3u) &&
           *(volatile uint32_t *)t->stack == STACK_GUARD &&
           (f->cpsr & 0xffu) == 0x53u;
}

static struct irq_frame *return_to_shell(void)
{
    current = 0;
    tasks[0].state = TASK_RUNNING;
    enabled = 0;
    return &tasks[0].frame;
}

/* IRQ 已完成硬件确认。此处只切换软件状态，不分配内存、不打印。 */
struct irq_frame *task_on_irq(struct irq_frame *frame)
{
    if (!enabled) {
        return frame;
    }
    unsigned old = current;
    struct task *t = &tasks[old];
    save_frame(&t->frame, frame);
    if (old && !valid_stack(t, frame)) {
        fault = 1;
    }
    if (t->state == TASK_RUNNING) {
        t->state = TASK_READY;
    }
    uint32_t now = timer_ticks();
    unsigned live = 0;
    for (unsigned i = 1; i < TASK_COUNT; ++i) {
        struct task *p = &tasks[i];
        if (p->state == TASK_SLEEPING && (int32_t)(now - p->wake_tick) >= 0) {
            p->state = TASK_READY;
            ++p->wakes;
        }
        if (p->state == TASK_READY || p->state == TASK_SLEEPING) {
            ++live;
        }
    }
    if (fault || timer_test_failed() || now >= TEST_LIMIT || !live) {
        if (live || timer_test_failed()) {
            fault = 1;
        }
        return return_to_shell();
    }

    /* 轮转查找 READY，跳过睡眠、退出和空槽；Shell 始终可运行。 */
    for (unsigned step = 1; step <= TASK_COUNT; ++step) {
        unsigned next = (old + step) & (TASK_COUNT - 1u);
        struct task *p = &tasks[next];
        if (p->state == TASK_SLEEPING) {
            ++sleeping_skips;
        }
        if (p->state == TASK_READY) {
            current = next;
            p->state = TASK_RUNNING;
            ++p->dispatches;
            return &p->frame;
        }
    }
    fault = 1;
    return return_to_shell();
}

/* 只有回到 Shell 且定时器停止后，才回收动态栈。 */
static int reap_all(void)
{
    int rc = 0;
    for (unsigned i = 1; i < TASK_COUNT; ++i) {
        struct task *t = &tasks[i];
        if (t->stack) {
            if (page_free(t->stack, STACK_PAGES)) {
                rc = -1;
            }
            t->stack = 0;
        }
        t->state = TASK_UNUSED;
    }
    return rc;
}

/* 两个 C 线程各自保留局部数组，跨睡眠和抢占后验证其内容。 */
struct test_result {
    unsigned id, delay;
    volatile unsigned work, slept, errors, done;
};

static struct test_result results[2];

static void test_worker(void *arg)
{
    struct test_result *r = arg;
    volatile uint32_t local[32];
    for (unsigned i = 0; i < 32u; ++i) {
        local[i] = 0x12340000u ^ (r->id << 12) ^ i;
    }
    for (unsigned pass = 0; pass < 2u; ++pass) {
        for (unsigned i = 0; i < 10000u; ++i) {
            ++r->work;
        }
        uint32_t before = timer_ticks();
        if (thread_sleep(r->delay) ||
            timer_ticks() - before < r->delay) {
            ++r->errors;
        } else {
            ++r->slept;
        }
        for (unsigned i = 0; i < 32u; ++i) {
            if (local[i] != (0x12340000u ^ (r->id << 12) ^ i)) {
                ++r->errors;
            }
        }
    }
    r->done = 1;
    /* 自动进入 thread_exit()。 */
}

int task_selftest(void)
{
    if (enabled || timer_running() || (cpu_sctlr() & 5u) != 5u) {
        console_write("TASK requires MMU/Cache ON and timer stopped\r\n");
        return -1;
    }
    if (timer_probe()) {
        return -1;
    }
    irq_lock();
    unsigned before = page_free_count();
    current = 0;
    fault = 0;
    sleeping_skips = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].dispatches = 0;
    for (unsigned i = 0; i < 2u; ++i) {
        struct test_result *r = &results[i];
        r->id = i + 1u;
        r->delay = i ? 7u : 5u;
        r->work = r->slept = r->errors = r->done = 0;
        if (thread_create(test_worker, r) != (int)(i + 1u)) {
            (void)reap_all();
            console_write("TASK create FAIL\r\n");
            return -1;
        }
    }
    console_write("TASK: two C threads, dynamic 8 KiB stacks, sleep and exit\r\n");
    console_reg("A stack base=", (uint32_t)tasks[1].stack);
    console_reg("B stack base=", (uint32_t)tasks[2].stack);
    enabled = 1;
    if (timer_start_bounded(TEST_LIMIT)) {
        enabled = 0;
        (void)reap_all();
        return -1;
    }
    unsigned budget = 100000000u;
    while (enabled && --budget) {
        __asm__ volatile("nop" ::: "memory");
    }
    /* 此处只在 Shell 执行；先屏蔽 IRQ，再回收所有线程栈。 */
    timer_stop();
    enabled = 0;
    int ok = !fault && !timer_test_failed() && budget && !current &&
             timer_ticks() < TEST_LIMIT && sleeping_skips &&
             page_free_count() == before - 2u * STACK_PAGES;
    for (unsigned i = 0; i < 2u; ++i) {
        struct test_result *r = &results[i];
        struct task *t = &tasks[i + 1u];
        console_write(i ? "Task B\r\n" : "Task A\r\n");
        console_reg("  dispatches=", t->dispatches);
        console_reg("  work=", r->work);
        console_reg("  sleeps=", t->sleep_calls);
        console_reg("  wakes=", t->wakes);
        console_reg("  errors=", r->errors);
        if (t->state != TASK_EXITED || !r->done || r->errors ||
            r->work != 20000u || r->slept != 2u || t->wakes != 2u ||
            t->sleep_calls != 2u || !t->dispatches ||
            *(volatile uint32_t *)t->stack != STACK_GUARD) {
            ok = 0;
        }
    }
    console_reg("Sleeping slots skipped=", sleeping_skips);
    if (reap_all() || page_free_count() != before) {
        ok = 0;
    }
    console_reg("Free pages restored=", page_free_count());
    console_write(ok ? "TASK PASS: create, sleep, wake, exit, stack reclaim\r\n" :
                       "TASK FAIL: lifecycle, stack or allocation check\r\n");
    return ok ? 0 : -1;
}
