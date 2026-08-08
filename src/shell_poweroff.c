#include <zephyr/shell/shell.h>

#include <zephyr/sys/poweroff.h>

#include "poweroff.h"

int shell_poweroff_cmd_handler(const struct shell *shell, size_t argc,
                               char **argv) {
  enter_poweroff();
  return 0;
}

SHELL_CMD_REGISTER(poweroff, NULL, "Power off the device",
                   shell_poweroff_cmd_handler);