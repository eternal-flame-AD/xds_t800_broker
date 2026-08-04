#pragma once

#include <stdint.h>

void bas_battery_level_set(uint8_t level);
void bas_battery_level_self_set(uint8_t level, uint16_t voltage);