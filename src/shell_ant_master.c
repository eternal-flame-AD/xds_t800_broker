#include "ant_profiles.h"
#include <stdlib.h>
#include <zephyr/shell/shell.h>

static int shell_ant_master_cmd_set_device_number(const struct shell *shell,
                                                  size_t argc, char **argv) {
  if (argc != 2) {
    shell_error(shell, "Usage: ant_master set_device_number <device_number>");
    shell_error(
        shell,
        "Device number must be between 1 and 65535 (0=reset to default)");
    return -EINVAL;
  }
  uint32_t device_number = strtoul(argv[1], NULL, 10);
  if (errno != 0) {
    shell_error(shell, "Invalid device number: %s", argv[1]);
    return -EINVAL;
  }
  int err = ant_profiles_set_device_number(
      device_number, device_number != ant_profiles_get_device_number());
  if (err != 0) {
    shell_error(shell,
                "Failed to set device number: device number %d is not valid",
                device_number);
    return err;
  }
  shell_print(shell, "Device number set to %d", device_number);
  return 0;
}

static int shell_ant_master_cmd_handler(const struct shell *shell, size_t argc,
                                        char **argv) {
  shell_print(shell, "Device number: %d", ant_profiles_get_device_number());
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    ant_master_sub, SHELL_CMD(set_device_number, NULL, "Set the device number",
                              shell_ant_master_cmd_set_device_number));

SHELL_CMD_REGISTER(ant_master, &ant_master_sub, "ANT+ Master commands",
                   shell_ant_master_cmd_handler);