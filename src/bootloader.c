#include <zephyr/shell/shell.h>

#include "bootloader.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>

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

#if CONFIG_BOOTLOADER_GPREGRET_VAL != 0

LOG_MODULE_REGISTER(shell_bootloader, LOG_LEVEL_INF);

#include <hal/nrf_power.h>
#include <zephyr/sys/reboot.h>

int bootloader_enter(void) {
  nrf_power_gpregret_set(NRF_POWER, 0, CONFIG_BOOTLOADER_GPREGRET_VAL);
  if (nrf_power_gpregret_get(NRF_POWER, 0) != CONFIG_BOOTLOADER_GPREGRET_VAL) {
    return -EIO;
  }
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

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
#endif /* CONFIG_BOOTLOADER_GPREGRET_VAL != 0 */
#endif /* DT_NODE_EXISTS(DT_NODELABEL(selfreset)) */
