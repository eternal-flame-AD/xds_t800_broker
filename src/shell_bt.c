#include "zephyr/bluetooth/addr.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(shell_bt, LOG_LEVEL_INF);

static void print_bond_cb(const struct bt_bond_info *info, void *user_data) {
  const struct shell *shell = user_data;
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(&info->addr, addr, sizeof(addr));
  shell_print(shell, "Bond: %s", addr);
}

static void find_bond_type_cb(const struct bt_bond_info *info,
                              void *user_data) {
  bt_addr_le_t *addr = user_data;
  if (bt_addr_eq(&info->addr.a, &addr->a)) {
    addr->type = info->addr.type;
    return;
  }
}

static int shell_bt_cmd_handler(const struct shell *shell, size_t argc,
                                char **argv) {
  char addr_str[BT_ADDR_LE_STR_LEN];
  bt_addr_le_t addrs[CONFIG_BT_ID_MAX];

  shell_print(shell, "Subcommands:");
  shell_print(shell, "  unpair - Unpair all bonds");

  size_t count = CONFIG_BT_ID_MAX;
  bt_id_get(addrs, &count);
  for (size_t i = 0; i < count; i++) {
    bt_addr_le_to_str(&addrs[i], addr_str, sizeof(addr_str));
    shell_print(shell, "Identity %d: %s (type=%d)", i, addr_str, addrs[i].type);
  }
  shell_print(shell, "Bonds:");
  bt_foreach_bond(BT_ID_DEFAULT, print_bond_cb, (void *)shell);

  return -EINVAL;
}

static int shell_bt_cmd_unpair(const struct shell *shell, size_t argc,
                               char **argv) {
  if (argc != 1 && argc != 2) {
    return -EINVAL;
  }
  bt_addr_le_t addr = *BT_ADDR_LE_ANY;
  if (argc == 2) {
    addr.type = BT_ADDR_LE_ANONYMOUS;
    if (bt_addr_from_str(argv[1], &addr.a) != 0) {
      shell_error(shell, "Invalid address: %s", argv[1]);
      return -EINVAL;
    }
    bt_foreach_bond(BT_ID_DEFAULT, find_bond_type_cb, &addr);
    if (addr.type == BT_ADDR_LE_ANONYMOUS) {
      shell_error(shell, "Address not found in bonds");
      return -EINVAL;
    }
  }
  int err = bt_unpair(BT_ID_DEFAULT, &addr);
  if (err != 0) {
    shell_error(shell, "Failed to unpair: %d", err);
    return err;
  }
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bt_sub, SHELL_CMD(unpair, NULL, "Unpair bonds",
                                                 shell_bt_cmd_unpair));

SHELL_CMD_REGISTER(bt, &bt_sub, "Bluetooth commands", shell_bt_cmd_handler);
