#pragma once

#include <ant_channel_config.h>
#include <ant_error.h>
#include <ant_init.h>
#include <ant_interface.h>
#include <ant_parameters.h>

#include <stdint.h>

#include "ant_types.h"
#include <zephyr/kernel.h>

struct ant_bike_power_s;

typedef void (*ant_bike_calib_request_cb_t)(struct ant_bike_power_s *profile);

struct ant_bike_power_s {
  ant_bike_calib_request_cb_t calib_request_cb;

  uint8_t page16_rebroadcast_ctr;
  uint8_t cache_page_80[8];
  uint8_t cache_page_81[8];
  uint8_t cache_page16[8];

  int8_t temperature;
  int16_t force_kgf;
  uint16_t angle;

  int16_t calibration_response;
  uint8_t last_update_event_count;
  uint8_t update_event_count;
  uint16_t instantaneous_power;
  uint16_t accumulated_power;
  uint8_t pwr_distribution_right;
  uint8_t instantaneous_cadence;
  uint8_t battery_request_idx;

  struct k_mutex update_mutex;

  uint16_t battery_voltage_self;
  enum ant_battery_status_t battery_state : 3;
  enum ant_battery_status_t battery_state_self : 3;
  enum {
    ANT_BIKE_POWER_CALIB_STATE_READY = 0,
    ANT_BIKE_POWER_CALIB_STATE_CALIBRATING = 1,
    ANT_BIKE_POWER_CALIB_STATE_SUCCESSFUL = 2,
    ANT_BIKE_POWER_CALIB_STATE_FAILED = 3,
  } calib_state : 2;
};

void ant_bike_power_init(struct ant_bike_power_s *profile,
                         ant_bike_calib_request_cb_t calib_request_cb);

void ant_bike_power_set_serial_number(struct ant_bike_power_s *profile,
                                      uint32_t serial_number);

void ant_bike_power_update(struct ant_bike_power_s *profile, uint16_t power,
                           uint8_t pwr_distribution_right,
                           uint8_t instantaneous_cadence, uint16_t angle);

void ant_bike_power_calib_response(struct ant_bike_power_s *profile,
                                   bool successful, int16_t response);

void ant_bike_power_set_battery_state(struct ant_bike_power_s *profile,
                                      uint8_t percent);

void ant_bike_power_set_self_battery_state(struct ant_bike_power_s *profile,
                                           uint8_t percent,
                                           uint16_t voltage_mv);

void ant_bike_power_evt_handler(ant_evt_t *p_ant_evt,
                                struct ant_bike_power_s *profile);
