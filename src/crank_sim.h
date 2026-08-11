#pragma once

#include <stdbool.h>
#include <stdint.h>

struct bt_crank_revolution_s {
  uint16_t crank_revolution;
  uint16_t last_crank_time;
};

struct crank_sim_s {
  uint32_t total_revolutions;
  uint32_t completed_revolutions;
  uint32_t last_data_time;
  uint32_t last_rev_time;
};

bool crank_sim_update(struct crank_sim_s *crank_sim, uint16_t rpm,
                      uint32_t current_ticks);
void crank_sim_get_bt_data(struct crank_sim_s *crank_sim,
                           struct bt_crank_revolution_s *bt_data);
