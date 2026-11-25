#ifndef RUNTIME_TIMEBASE_H
#define RUNTIME_TIMEBASE_H
#include "platform.h"
int timer_probe(void);
void timer_start(void);
void timer_stop(void);
void timer_info(void);
int timer_running(void);
void irq_dispatch(void);
int benchmark_run(void);
#endif
