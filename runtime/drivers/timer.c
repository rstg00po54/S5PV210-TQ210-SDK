#include "timebase.h"

/* S5PV210 PWM timer 0, VIC0 input 21. Exclusive bare-metal ownership. */
#define PWM_BASE 0xe2500000u
#define PWM(off) MMIO32(PWM_BASE + (off))
#define TCFG0 0x00u
#define TCFG1 0x04u
#define TCON 0x08u
#define TCNTB0 0x0cu
#define TCMPB0 0x10u
#define TCNTO0 0x14u
#define TINT_CSTAT 0x44u
#define T0_ENABLE 1u
#define T0_PENDING (1u << 5)
#define T0_IRQ (1u << 21)
#define VIC_BASE(i) (0xf2000000u + ((i) << 20))
#define VIC(i,off) MMIO32(VIC_BASE(i) + (off))
#define IRQ_STATUS 0x00u
#define INT_SELECT 0x0cu
#define INT_ENABLE 0x10u
#define INT_CLEAR 0x14u
#define SOFT_CLEAR 0x1cu
#define PRIORITY_MASK 0x24u
#define VECTOR_SLOT 0x100u
#define VECTOR_PRIORITY 0x200u
#define VECTOR_ADDR 0xf00u
#define RELOAD 6667u

static volatile uint32_t ticks;
static volatile unsigned active;
static unsigned probed;

static void irq_mask(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    isb();
}

static void vic_disable_all(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        VIC(i, INT_CLEAR) = 0xffffffffu;
        VIC(i, SOFT_CLEAR) = 0xffffffffu;
        VIC(i, INT_SELECT) = 0u;
        VIC(i, VECTOR_ADDR) = 0u;
    }
    dsb();
}

static int prepare(void)
{
    if ((cpu_sctlr() & 5u) != 5u) {
        console_write("Enable MMU and D-Cache first.\r\n");
        return -1;
    }
    if (active) {
        console_write("Stop timer first.\r\n");
        return -1;
    }
    if (MMIO32(CLOCK_BASE+0x100u) != 0xa0c80601u ||
        MMIO32(CLOCK_BASE+0x108u) != 0xa29b0c01u ||
        MMIO32(CLOCK_BASE+0x200u) != 0x111u ||
        MMIO32(CLOCK_BASE+0x300u) != 0x14141251u) {
        console_write("Unexpected clock tree; timer not configured.\r\n");
        return -1;
    }
    irq_mask();
    MMIO32(CLOCK_BASE+0x46cu) |= 1u << 23;
    dsb();
    if (!(MMIO32(CLOCK_BASE+0x46cu) & (1u << 23))) {
        console_write("PWM clock gate FAIL\r\n");
        return -1;
    }
    if (PWM(TCON) & ((1u << 8)|(1u << 12)|(1u << 16)|(1u << 20))) {
        console_write("Other PWM timers running; refusing shared prescaler change.\r\n");
        return -1;
    }
    PWM(TCON) &= ~15u;
    /* Low five bits are enables; bits 5..9 are W1C pending. */
    PWM(TINT_CSTAT) = T0_PENDING;
    /* PCLK_PSYS nominal 66.667 MHz /100 /1 /6667 = ~100 Hz. */
    PWM(TCFG0) = (PWM(TCFG0) & ~255u) | 99u;
    PWM(TCFG1) &= ~15u;
    PWM(TCNTB0) = RELOAD;
    PWM(TCMPB0) = 0;
    PWM(TCON) = (PWM(TCON) & ~15u) | 2u;
    PWM(TCON) &= ~15u;
    dsb();
    return 0;
}

static void stop_hw(void)
{
    PWM(TCON) &= ~15u;
    PWM(TINT_CSTAT) = T0_PENDING;
    dsb();
}

int timer_probe(void)
{
    probed = 0;
    if (prepare()) {
        return -1;
    }
    console_reg("TCFG0=", PWM(TCFG0));
    console_reg("TCFG1=", PWM(TCFG1));
    console_reg("TCNTB0=", PWM(TCNTB0));
    PWM(TCON) |= 9u;
    dsb();
    uint32_t first = PWM(TCNTO0);
    console_reg("TCNTO0 initial=", first);
    for (unsigned i = 0; i < 3u; ++i) {
        unsigned timeout = 10000000u;
        while (!(PWM(TINT_CSTAT) & T0_PENDING) && --timeout) {
            __asm__ volatile("nop");
        }
        if (!timeout) {
            console_reg("TCNTO0 timeout=", PWM(TCNTO0));
            console_reg("TINT_CSTAT=", PWM(TINT_CSTAT));
            stop_hw();
            console_write("TIMER POLL FAIL\r\n");
            return -1;
        }
        PWM(TINT_CSTAT) = T0_PENDING;
        dsb();
    }
    stop_hw();
    probed = 1;
    console_write("TIMER POLL PASS: 3 pending events\r\n");
    return 0;
}

void timer_start(void)
{
    if (!probed) {
        console_write("Run timer probe successfully first.\r\n");
        return;
    }
    if (prepare()) {
        return;
    }
    vic_disable_all();
    ticks = 0;
    VIC(0, PRIORITY_MASK) = 0xffffu;
    VIC(0, VECTOR_SLOT + 21u*4u) = 21u;
    VIC(0, VECTOR_PRIORITY + 21u*4u) = 0u;
    PWM(TINT_CSTAT) = T0_ENABLE | T0_PENDING;
    VIC(0, INT_ENABLE) = T0_IRQ;
    dsb();
    if (!(VIC(0, INT_ENABLE) & T0_IRQ)) {
        stop_hw();
        console_write("VIC enable FAIL\r\n");
        return;
    }
    active = 1;
    PWM(TCON) |= 9u;
    dsb();
    console_write("Timer0 IRQ enabled; use ticks to check progress.\r\n");
    __asm__ volatile("cpsie i" ::: "memory");
    isb();
}

void timer_stop(void)
{
    if (!active) {
        console_write("Timer already stopped.\r\n");
        return;
    }
    irq_mask();
    VIC(0, INT_CLEAR) = T0_IRQ;
    stop_hw();
    active = 0;
    dsb();
    console_write("Timer stopped; IRQ masked.\r\n");
    timer_info();
}

int timer_running(void)
{
    return active != 0u;
}

void timer_info(void)
{
    uint32_t snapshot = ticks;
    console_write(active ? "Timer0: RUNNING (~100 Hz nominal)\r\n" :
                           "Timer0: STOPPED\r\n");
    console_write("Ticks: ");
    console_dec(snapshot);
    console_write("\r\n");
}

void irq_dispatch(void)
{
    uint32_t status = VIC(0, IRQ_STATUS);
    if (active && status == T0_IRQ &&
        !(VIC(1, IRQ_STATUS) | VIC(2, IRQ_STATUS) | VIC(3, IRQ_STATUS)) &&
        (PWM(TINT_CSTAT) & T0_PENDING)) {
        /* Read current vector to acknowledge, clear peripheral, then EOI. */
        (void)VIC(0, VECTOR_ADDR);
        PWM(TINT_CSTAT) = T0_ENABLE | T0_PENDING;
        dsb();
        ++ticks;
        VIC(0, VECTOR_ADDR) = 0u;
        dsb();
        return;
    }
    vic_disable_all();
    if (active) {
        stop_hw();
        active = 0;
    }
    console_reg("UNEXPECTED IRQ VIC0=", status);
    runtime_halt();
}
