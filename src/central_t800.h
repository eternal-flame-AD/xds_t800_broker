#pragma once

#include <zephyr/sys/util_macro.h>

#if IS_ENABLED(CONFIG_ANT)
#include "ant_bike_power.h"
#endif

int central_t800_offset_compensation_start(void);
#if IS_ENABLED(CONFIG_ANT)
void central_t800_offset_compensation_start_ant(
    struct ant_bike_power_s *profile);
#endif