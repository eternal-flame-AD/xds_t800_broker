#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(shell_flush, LOG_LEVEL_INF);

static int shell_flush_cmd_handler(const struct shell *shell, size_t argc,
                                   char **argv) {
  LOG_INF("Log buffer flushed");
  log_flush();
  return 0;
}

SHELL_CMD_REGISTER(flush, NULL, "Flush log buffer", shell_flush_cmd_handler);