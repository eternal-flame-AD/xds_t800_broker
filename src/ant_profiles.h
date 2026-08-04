#pragma once

#include "ant_bike_power.h"

#define ANT_NETWORK_ANTPLUS 0
#define ANT_NETWORK_BPWR_DIAG 1

extern struct ant_bike_power_s bike_power;

extern ant_channel_config_t bpwr_channel_config;
extern ant_channel_config_t bpwr_diag_channel_config;
extern ant_channel_config_t antplus_generic_slave_config;