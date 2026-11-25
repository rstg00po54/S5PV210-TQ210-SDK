#include "memory.h"
#include "timebase.h"
#include "selftest.h"

static uintptr_t held;
static unsigned held_pages;
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static unsigned parse_count(const char *s)
{
    unsigned n = 0;
    if (!*s) { return 0; }
    while (*s >= '0' && *s <= '9') {
        n = n * 10u + (unsigned)(*s++ - '0');
        if (n > 4096u) { return 0; }
    }
    return *s ? 0u : n;
}
static void help(void)
{
    console_write("help       Show commands\r\n"
                  "cpuinfo    CPU and clock registers\r\n"
                  "meminfo    Physical memory and free pages\r\n"
                  "mmu        Show MMU/cache status\r\n"
                  "mmu on     Enable identity mappings\r\n"
                  "cache on   Enable D-cache (requires MMU)\r\n"
                  "alloc N    Allocate N contiguous 4 KiB pages (1..4096)\r\n"
                  "free       Release the shell allocation\r\n"
                  "memtest    Test allocated scratch pages\r\n"
                  "benchmark  PMU memory test (1 MiB)\r\n"
                  "timer probe Test PWM pending events\r\n"
                  "timer on   Start Timer1 IRQ after probe\r\n"
                  "timer off  Stop Timer1 IRQ\r\n"
                  "ticks      Show timer tick count\r\n"
                  "irqtest    Run bounded Timer1 IRQ self-test\r\n"
                  "autotest   Run automatic MMU/cache/DDR/timer tests\r\n"
                  "halt       Stop; cold power-cycle to restart\r\n");
}
static void execute(char *line)
{
    if (!*line) {
        return;
    }
    if (equal(line, "help")) {
        help();
    } else if (equal(line, "cpuinfo")) {
        cpu_info();
    } else if (equal(line, "meminfo")) {
        memory_info();
    } else if (equal(line, "mmu")) {
        mmu_info();
    } else if (equal(line, "mmu on")) {
        console_write("Enabling MMU...\r\n");
        console_write(mmu_enable() ? "MMU FAIL\r\n" : "MMU ON\r\n");
        mmu_info();
    } else if (equal(line, "cache on")) {
        console_write("Enabling D-Cache...\r\n");
        console_write(cache_enable() ? "CACHE FAIL (MMU required)\r\n" : "D-Cache ON\r\n");
        mmu_info();
    } else if (equal(line, "memtest")) {
        console_write(memory_selftest() ? "MEMTEST FAIL\r\n" : "MEMTEST PASS\r\n");
    } else if (line[0]=='a' && line[1]=='l' && line[2]=='l' &&
               line[3]=='o' && line[4]=='c' && line[5]==' ') {
        unsigned n = parse_count(line + 6);
        if (!n || held) {
            console_write("Usage: alloc 1..4096; free existing allocation first\r\n");
        } else {
            held = page_alloc(n);
            if (held) {
                held_pages = n;
                console_reg("ALLOC=", (uint32_t)held);
                console_write("Pages: ");
                console_dec(n);
                console_write("\r\n");
            } else {
                console_write("ALLOC FAIL\r\n");
            }
        }
    } else if (equal(line, "free")) {
        if (held && !page_free(held, held_pages)) {
            held = 0;
            held_pages = 0;
            console_write("FREE PASS\r\n");
        } else {
            console_write("No shell allocation or free failed\r\n");
        }
    } else if (equal(line, "benchmark")) {
        benchmark_run();
    } else if (equal(line, "timer probe")) {
        timer_probe();
    } else if (equal(line, "irqtest")) {
        timer_irq_selftest();
    } else if (equal(line, "timer on")) {
        timer_start();
    } else if (equal(line, "timer off")) {
        timer_stop();
    } else if (equal(line, "ticks")) {
        timer_info();
    } else if (equal(line, "autotest")) {
        runtime_autotest();
    } else if (equal(line, "halt")) {
        console_write("Halted. Cold power-cycle to restart.\r\n");
        runtime_halt();
    } else {
        console_write("Unknown command; type help\r\n");
    }
}
void shell_run(void)
{
    char line[80];
    unsigned used = 0;
    int skip_lf = 0, overflow = 0;
    console_write("Type help for commands.\r\ncc@TQ210:~$ ");
    for (;;) {
        int ch = uart_getc();
        if (ch == '\n' && skip_lf) {
            skip_lf = 0;
            continue;
        }
        skip_lf = 0;
        if (ch == '\r' || ch == '\n') {
            skip_lf = ch == '\r';
            console_write("\r\n");
            line[used] = 0;
            if (overflow) {
                console_write("Line too long; command ignored\r\n");
            } else {
                execute(line);
            }
            used = 0;
            overflow = 0;
            console_write("cc@TQ210:~$ ");
        } else if (ch == 8 || ch == 127) {
            if (used && !overflow) {
                --used;
                console_write("\b \b");
            }
        } else if (ch >= 32 && ch <= 126) {
            if (used + 1u < sizeof(line) && !overflow) {
                line[used++] = (char)ch;
                uart_putc((char)ch);
            } else {
                overflow = 1;
            }
        }
    }
}
