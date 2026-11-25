#include "openboot/platform.h"
void watchdog_disable(void)
{
    mmio_write32(S5PV210_WATCHDOG_BASE, 0);
}
