#pragma once

#include "ant_bike_power.h"
#include "central_profile.h"

extern const struct central_profile central_t800_profile;

int central_t800_offset_compensation_start(void);
void central_t800_offset_compensation_start_ant(
    struct ant_bike_power_s *profile);