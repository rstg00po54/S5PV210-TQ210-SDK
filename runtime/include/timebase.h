#ifndef RUNTIME_TIMEBASE_H
#define RUNTIME_TIMEBASE_H
#include "platform.h"
int timer_probe(void);
int timer_start(void);
int timer_start_bounded(unsigned limit);
uint32_t timer_ticks(void);
int timer_test_failed(void);
int timer_irq_selftest(void);
void timer_stop(void);
void timer_info(void);
int timer_running(void);
void irq_dispatch(void);
int benchmark_run(void);
#endif
