#include "crank_sim.h"
#include "zephyr/toolchain.h"

#define ONE_ROTATION (4096)
#define ONE_MINUTE_IN_TICKS (CONFIG_SYS_CLOCK_TICKS_PER_SEC * 60)

#define ROUND_DIV(x, y) (((x) + (y) / 2) / (y))

BUILD_ASSERT(ONE_MINUTE_IN_TICKS % ONE_ROTATION == 0,
             "one_minute_in_ticks must be divisible by one_rotation");
BUILD_ASSERT(CONFIG_SYS_CLOCK_TICKS_PER_SEC > 0 &&
                 CONFIG_SYS_CLOCK_TICKS_PER_SEC % 1024 == 0,
             "SYS_CLOCK_TICKS_PER_SEC must be divisible by 1024");

bool crank_sim_update(struct crank_sim_s *crank_sim, uint16_t rpm,
                      uint32_t current_ticks) {

  bool new_rev = false;
  uint32_t time_delta = current_ticks - crank_sim->last_data_time;

  // lost data for too long, reset
  if (!crank_sim->last_data_time ||
      time_delta > 5 * CONFIG_SYS_CLOCK_TICKS_PER_SEC) {
    crank_sim->last_data_time = current_ticks;
    crank_sim->last_rev_time = current_ticks;
    return false;
  }

  uint32_t d = rpm * time_delta;
  uint32_t d_crank_position = ROUND_DIV(d, ONE_MINUTE_IN_TICKS / ONE_ROTATION);
  crank_sim->total_revolutions += d_crank_position;

  if (crank_sim->total_revolutions / ONE_ROTATION !=
      crank_sim->completed_revolutions) {
    new_rev = true;

    uint32_t fractional_position = crank_sim->total_revolutions % ONE_ROTATION;
    time_delta = current_ticks - crank_sim->last_rev_time;
    crank_sim->last_rev_time =
        current_ticks -
        ROUND_DIV(time_delta * fractional_position,
                  crank_sim->total_revolutions -
                      crank_sim->completed_revolutions * ONE_ROTATION);
    crank_sim->completed_revolutions =
        crank_sim->total_revolutions / ONE_ROTATION;
  }
  crank_sim->last_data_time = current_ticks;

  return new_rev;
}

void crank_sim_get_bt_data(struct crank_sim_s *crank_sim,
                           struct bt_crank_revolution_s *bt_data) {
  bt_data->crank_revolution = crank_sim->completed_revolutions;
  bt_data->last_crank_time =
      crank_sim->last_rev_time / (CONFIG_SYS_CLOCK_TICKS_PER_SEC / 1024);
}