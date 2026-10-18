#pragma once

#include "ant_types.h"
#include <ant_channel_config.h>
#include <ant_error.h>
#include <ant_init.h>
#include <ant_interface.h>
#include <ant_parameters.h>

#include <zephyr/kernel.h>

struct ant_environ_s {
  uint32_t epoch_seconds;
  uint8_t cache_page_80[8];
  uint8_t cache_page_81[8];
  uint8_t temp_evt_count;
  int16_t current_temp;
  int16_t max_temp_history[4 * HOUR_PER_DAY];
  int16_t min_temp_history[4 * HOUR_PER_DAY];
  uint16_t last_history_index;
  uint8_t page_request_idx;
  uint8_t page_request_ctr;
  uint16_t battery_voltage_self;
  enum ant_battery_status_t battery_state_self : 3;

  bool temp_valid : 1;
};

void ant_environ_init(struct ant_environ_s *p_environ);

void ant_environ_temp_set(struct ant_environ_s *p_environ,
                          int16_t temp_c_centi);

void ant_environ_set_battery_state(struct ant_environ_s *p_environ,
                                   uint8_t percent, uint16_t voltage_mv);

void ant_environ_evt_handler(ant_evt_t *p_ant_evt,
                             struct ant_environ_s *p_environ);