#include "watchdog.h"

#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

const struct device *watchdog_dev = DEVICE_DT_GET(DT_NODELABEL(wdt));

const struct wdt_timeout_cfg watchdog_timeout_cfg = {
    .window.max = 5000,
    .flags = WDT_FLAG_RESET_SOC,
};

static int channel = -1;

void watchdog_init(void) {
  channel = wdt_install_timeout(watchdog_dev, &watchdog_timeout_cfg);
  if (channel < 0) {
    LOG_ERR("Failed to get watchdog channel (%d)", channel);
    return;
  }
  wdt_setup(watchdog_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
}

void watchdog_feed(void) {
  if (channel >= 0)
    wdt_feed(watchdog_dev, channel);
}
