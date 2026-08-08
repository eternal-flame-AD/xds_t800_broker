#include <zephyr/shell/shell.h>

#include "bootloader.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>

#define RECOVERY_MARKER 0xA5

#if DT_NODE_EXISTS(DT_NODELABEL(selfreset)) &&                                 \
    DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(selfreset))

LOG_MODULE_REGISTER(shell_bootloader, LOG_LEVEL_INF);

const struct led_dt_spec self_reset_pin =
    LED_DT_SPEC_GET(DT_NODELABEL(selfreset));

int bootloader_enter(void) { return led_on_dt(&self_reset_pin); }

static int shell_bootloader_cmd_handler(const struct shell *shell, size_t argc,
                                        char **argv) {
  int error = bootloader_enter();
  if (error) {
    shell_error(shell, "Failed to enter bootloader mode: %d", error);
    return error;
  }
  return 0;
}

SHELL_CMD_REGISTER(bootloader, NULL, "Enter bootloader mode",
                   shell_bootloader_cmd_handler);
#else

int bootloader_enter(void) { return -ENOTSUP; }
#endif
