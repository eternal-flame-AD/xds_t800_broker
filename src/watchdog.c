#include "watchdog.h"
#include "bootloader.h"
#include "leds.h"
#include "zephyr/toolchain.h"
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

const struct device *watchdog_dev = DEVICE_DT_GET(DT_NODELABEL(wdt));

const struct wdt_timeout_cfg watchdog_timeout_cfg = {
    .window.max = 5000,
    .flags = WDT_FLAG_RESET_SOC,
};

static int channel = -1;

static uint32_t reset_reason = 0;

#if !IS_ENABLED(CONFIG_RESET_ON_FATAL_ERROR)
void k_sys_fatal_error_handler(unsigned int reason,
                               const struct arch_esf *esf) {
  if (reason != K_ERR_KERNEL_PANIC) {
    LOG_PANIC();
    for (int i = 0; i < 10; i++) {
      led_set_bit(POWER_LED_BIT);
      k_sleep(K_MSEC(100));
      led_clear_bit(POWER_LED_BIT);
      k_sleep(K_MSEC(100));
    }
  }

  bootloader_enter();
  k_fatal_halt(reason);
  CODE_UNREACHABLE;
}
#endif

int watchdog_init(void) {
  int err = hwinfo_get_reset_cause(&reset_reason);
  if (err == 0) {
    if (reset_reason & (RESET_WATCHDOG | RESET_CPU_LOCKUP)) {
      bootloader_enter();
    }
    hwinfo_clear_reset_cause();
  }

  channel = wdt_install_timeout(watchdog_dev, &watchdog_timeout_cfg);
  if (channel < 0) {
    return 0;
  }
  wdt_setup(watchdog_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

  return 0;
}

SYS_INIT(watchdog_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

void watchdog_feed(void) {
  if (channel >= 0)
    wdt_feed(watchdog_dev, channel);
}

uint32_t watchdog_get_reset_reason(void) { return reset_reason; }
