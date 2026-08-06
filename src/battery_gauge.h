#pragma once

#include <stdint.h>
#include <zephyr/sys/clock.h>

int battery_gauge_setup(k_timeout_t interval);
int16_t battery_gauge_get_mv(void);
uint8_t battery_gauge_get_level(uint16_t voltage_mv);