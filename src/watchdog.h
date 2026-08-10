#pragma once

#include <stdint.h>

void watchdog_feed(void);
uint32_t watchdog_get_reset_reason(void);