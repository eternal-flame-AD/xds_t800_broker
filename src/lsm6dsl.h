#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

int64_t lsm6dsl_get_time_since_last_activity(void);
int lsmdsl_g_force_service_start(const struct device *dev);
void lsmdsl_g_force_service_stop(void);
bool lsmdsl_wakeup_is_available(void);
