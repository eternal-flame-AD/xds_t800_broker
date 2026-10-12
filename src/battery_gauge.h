#pragma once

#include <stdint.h>
#include <zephyr/sys/clock.h>

int battery_gauge_setup(void);
void battery_gauge_teardown(void);
int16_t battery_gauge_get_mv(void);
uint8_t battery_gauge_get_level(uint16_t voltage_mv);