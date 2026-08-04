#include "ant_profiles.h"
#include "ant_channel_config.h"

struct ant_bike_power_s bike_power;

ant_channel_config_t bpwr_channel_config = {
    .channel_number = 0,
    .channel_type = 0x10,
    .ext_assign = 0,
    .rf_freq = 57,
    .transmission_type = 5,
    .device_type = 11,
    .device_number = 12345, // to be filled by hardware id
    .channel_period = 8182,
    .network_number = ANT_NETWORK_ANTPLUS,
};

ant_channel_config_t bpwr_diag_channel_config = {
    .channel_number = 1,
    .channel_type = 0x10,
    .ext_assign = 0,
    .rf_freq = CONFIG_BPWR_TX_DIAG_FREQUENCY,
    .transmission_type = 5,
    .device_type = 11,
    .device_number = 12345, // to be filled by hardware id
    .channel_period = 8182,
    .network_number = ANT_NETWORK_BPWR_DIAG,
};

ant_channel_config_t antplus_generic_slave_config = {
    .channel_number = 2,
    .channel_type = 0,
    .ext_assign = 0,
    .rf_freq = 57,
    .transmission_type = 0,
    .device_type = 0, // to be filled
    .device_number = 0,
    .channel_period = 0, // to be filled
    .network_number = ANT_NETWORK_ANTPLUS,
};