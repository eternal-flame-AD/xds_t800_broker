#include "poweroff.h"
#include "battery_gauge.h"
#include "leds.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#if IS_ENABLED(CONFIG_ANT)
#include "ant_interface.h"
#endif

LOG_MODULE_REGISTER(poweroff, LOG_LEVEL_ERR);

static bool insomnia_mode = false;

bool poweroff_get_insomnia(void) { return insomnia_mode; }

void enter_poweroff(void) {
  if (insomnia_mode) {
    return;
  }
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

int shell_poweroff_cmd_handler(const struct shell *shell, size_t argc,
                               char **argv) {
  enter_poweroff();
  return 0;
}

int shell_insomnia_cmd_handler(const struct shell *shell, size_t argc,
                               char **argv) {
  if (argc != 1 && argc != 2) {
    shell_help(shell);
    return -EINVAL;
  }
  if (argc == 2) {
    if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0) {
      insomnia_mode = true;
    } else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
      insomnia_mode = false;
    } else {
      shell_help(shell);
      return -EINVAL;
    }
  }
  shell_print(shell, "Insomnia mode: %s",
              poweroff_get_insomnia() ? "true" : "false");
  return 0;
}

SHELL_CMD_REGISTER(insomnia, NULL, "Get the insomnia state",
                   shell_insomnia_cmd_handler);

SHELL_CMD_REGISTER(poweroff, NULL, "Power off the device",
                   shell_poweroff_cmd_handler);