#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(shell_rfsw, CONFIG_SHELL_LOG_LEVEL);

#define SETTINGS_RF_SW_SUBTREE "rfsw"

#define SETTINGS_RF_SW_EXTERNAL_SEGMENT "external"

static const struct device *const rfsw_ctl_reg =
    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(rfsw_ctl)),
                DEVICE_DT_GET(DT_NODELABEL(rfsw_ctl)), NULL);

static int shell_rfsw_cmd_handler(const struct shell *shell, size_t argc,
                                  char **argv) {
  if (argc != 1 && argc != 2) {
    shell_help(shell);
    return -EINVAL;
  }
  if (!rfsw_ctl_reg) {
    shell_error(shell, "RF switch control not available");
    return -ENOTSUP;
  }
  int err = 0;
  if (argc == 2) {
    if (strcmp(argv[1], "ext") == 0 || strcmp(argv[1], "1") == 0) {
      err = regulator_enable(rfsw_ctl_reg);
      if (err == 0) {
        uint8_t is_external = 1;
        int store_err = settings_save_one(SETTINGS_RF_SW_EXTERNAL_SEGMENT,
                                          &is_external, sizeof(is_external));
        if (store_err != 0) {
          LOG_ERR("Failed to save RF switch settings: %d", err);
        }
      }
    }
    if (strcmp(argv[1], "int") == 0 || strcmp(argv[1], "0") == 0) {
      err = regulator_disable(rfsw_ctl_reg);
      if (err == 0) {
        int store_err = settings_delete(SETTINGS_RF_SW_EXTERNAL_SEGMENT);
        if (store_err != 0) {
          LOG_ERR("Failed to save RF switch settings: %d", err);
        }
      }
    }
  }
  if (err != 0) {
    shell_error(shell, "Failed to control RF switch: %d", err);
  }
  if (regulator_is_enabled(rfsw_ctl_reg)) {
    shell_print(shell, "RF switch is enabled (external antenna)");
  } else {
    shell_print(shell, "RF switch is disabled (internal antenna)");
  }
  return err;
}

static int rfsw_settings_set(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  int rc;

  if (settings_name_steq(name, SETTINGS_RF_SW_EXTERNAL_SEGMENT, &next) &&
      !next) {
    uint8_t is_external;
    if (len != sizeof(is_external)) {
      return -EINVAL;
    }
    rc = read_cb(cb_arg, &is_external, sizeof(is_external));
    if (rc >= 0) {
      if (!rfsw_ctl_reg) {
        LOG_WRN("RF switch control not available");
        return -ENOTSUP;
      }
      if (is_external) {
        int err = regulator_enable(rfsw_ctl_reg);
        if (err != 0) {
          LOG_ERR("Failed to enable RF switch: %d", err);
          return err;
        }
      } else {
        int err = regulator_disable(rfsw_ctl_reg);
        if (err != 0) {
          LOG_ERR("Failed to disable RF switch: %d", err);
          return err;
        }
      }
      return 0;
    }
    return rc;
  }

  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(rfsw, SETTINGS_RF_SW_SUBTREE, NULL,
                               rfsw_settings_set, NULL, NULL);

SHELL_CMD_REGISTER(rfsw, NULL, SHELL_HELP("RF switch control", "[ext|int]"),
                   shell_rfsw_cmd_handler);