#include "poweroff.h"
#include "battery_gauge.h"
#include "leds.h"
#include "zephyr/sys/clock.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#if IS_ENABLED(CONFIG_ANT)
#include "ant_interface.h"
#endif

LOG_MODULE_REGISTER(poweroff, LOG_LEVEL_ERR);

void enter_poweroff(void) {
  led_clear_bit(POWER_LED_BIT);

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(rfsw_pwr))
  const struct device *rfsw_pwr = DEVICE_DT_GET(DT_NODELABEL(rfsw_pwr));
  int err = regulator_disable(rfsw_pwr);
  if (err < 0) {
    LOG_ERR("Failed to disable rfsw_pwr regulator: %d", err);
  }
#endif

  battery_gauge_teardown();
  bt_disable();
#if IS_ENABLED(CONFIG_ANT)
  ant_stack_disable();
#endif
  log_flush();
  // wait for 0.1sec for everything to settle
  k_busy_wait(USEC_PER_MSEC * 100);
  sys_poweroff();
  sys_reboot(SYS_REBOOT_COLD);
}
