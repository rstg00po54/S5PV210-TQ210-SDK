#ifndef RUNTIME_TIMEBASE_H
#define RUNTIME_TIMEBASE_H
#include "platform.h"
void timer_probe(void);
void timer_start(void);
void timer_stop(void);
void timer_info(void);
int timer_running(void);
void irq_dispatch(void);
void benchmark_run(void);
#endif
