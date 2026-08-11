#pragma once

#include "ant_bike_power.h"
#include "ant_environ.h"
#include <ant_channel_config.h>

#define SETTINGS_ANT_SUBTREE "ant"
#define SETTINGS_ANT_WAKEUP_SEGMENT "wakeup"
#define SETTINGS_ANT_DEVICE_NUMBER_SEGMENT "device_number"

#define ANT_NETWORK_ANTPLUS 0

#define ANT_WAKEUP_CHANNEL_SLOT_COUNT 2

extern struct ant_bike_power_s bike_power;
extern struct ant_environ_s environ;

extern ant_channel_config_t bpwr_channel_config;
extern ant_channel_config_t bpwr_environ_channel_config;
extern ant_channel_config_t antplus_wakeup_slave_config[2];
extern ant_channel_config_t antplus_generic_slave_config;

uint32_t ant_profiles_create_device_number(uint32_t seed);

uint32_t ant_profiles_get_device_number(void);

int ant_profiles_set_device_number(uint32_t device_number, bool persist);