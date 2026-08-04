#include "gatt_cpu_load.h"

#include <bluetooth/services/nsms.h>
#include <debug/cpu_load.h>

BT_NSMS_DEF(nsms_cpuload, "cpu_load", BT_NSMS_SECURITY_LEVEL_NONE, "", 32);

void cpu_load_timer_handler(struct k_timer *timer_id) {
  char cpu_load_str[32];
  int load = cpu_load_get();
  if (load < 0) {
    return;
  }
  int len = snprintf(cpu_load_str, sizeof(cpu_load_str), "5s=%d.%03d%%",
                     load / 1000, load % 1000);
  if (len > 0) {
    bt_nsms_set_status(&nsms_cpuload, cpu_load_str);
  }
  cpu_load_reset();
}

K_TIMER_DEFINE(cpu_load_timer, cpu_load_timer_handler, NULL);

void gatt_cpu_load_init(void) {
  cpu_load_reset();
  k_timer_start(&cpu_load_timer, K_MSEC(5000), K_MSEC(5000));
}