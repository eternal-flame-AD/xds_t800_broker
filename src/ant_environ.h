#pragma once

#include <ant_channel_config.h>
#include <ant_error.h>
#include <ant_init.h>
#include <ant_interface.h>
#include <ant_parameters.h>

#include <zephyr/kernel.h>

struct ant_environ_s {
  uint8_t cache_page_80[8];
  uint8_t cache_page_81[8];
  uint8_t temp_evt_count;
  int16_t current_temp;
  int16_t max_temp;
  int16_t min_temp;
  bool temp_valid : 1;
};

void ant_environ_init(struct ant_environ_s *p_environ);

void ant_environ_temp_set(struct ant_environ_s *p_environ, int8_t temp_c);

void ant_environ_evt_handler(ant_evt_t *p_ant_evt,
                             struct ant_environ_s *p_environ);