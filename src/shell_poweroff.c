#include <zephyr/shell/shell.h>

#include <zephyr/sys/poweroff.h>

#include "leds.h"

int shell_poweroff_cmd_handler(const struct shell *shell, size_t argc,
                               char **argv) {
  led_clear_bit(POWER_LED_BIT);
  sys_poweroff();
  led_set_bit(POWER_LED_BIT);
  return 0;
}

SHELL_CMD_REGISTER(poweroff, NULL, "Power off the device",
                   shell_poweroff_cmd_handler);