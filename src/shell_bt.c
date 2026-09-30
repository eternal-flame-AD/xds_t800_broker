#include "advertiser.h"
#include "central_profile.h"
#include "scanner.h"
#include <sdc_hci_cmd_status_params.h>
#include <sys/errno.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(shell_bt, LOG_LEVEL_INF);

static void print_bond_cb(const struct bt_bond_info *info, void *user_data) {
  const struct shell *shell = user_data;
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(&info->addr, addr, sizeof(addr));
  shell_print(shell, "  Bond: %s", addr);
}

static void find_bond_type_cb(const struct bt_bond_info *info,
                              void *user_data) {
  bt_addr_le_t *addr = user_data;
  if (bt_addr_eq(&info->addr.a, &addr->a)) {
    addr->type = info->addr.type;
    return;
  }
}

static void print_conn_cb(struct bt_conn *conn, void *user_data) {
  const struct shell *shell = user_data;
  char addr[BT_ADDR_LE_STR_LEN];
  int err;

  const bt_addr_le_t *dst = bt_conn_get_dst(conn);
  bt_addr_le_to_str(dst, addr, sizeof(addr));

  struct bt_conn_info conn_info = {0};
  err = bt_conn_get_info(conn, &conn_info);
  if (err != 0) {
    shell_error(shell, "  Failed to get connection info: %d", err);
    return;
  }
  shell_print(shell, "  Connection: %s (%s, %s)", addr,
              conn_info.role == BT_CONN_ROLE_CENTRAL      ? "Central"
              : conn_info.role == BT_CONN_ROLE_PERIPHERAL ? "Peripheral"
                                                          : "Unknown",
              conn_info.state == BT_CONN_STATE_CONNECTED       ? "Connected"
              : conn_info.state == BT_CONN_STATE_DISCONNECTED  ? "Disconnected"
              : conn_info.state == BT_CONN_STATE_DISCONNECTING ? "Disconnecting"
              : conn_info.state == BT_CONN_STATE_CONNECTING    ? "Connecting"
                                                               : "Unknown");
}

static int shell_bt_cmd_handler(const struct shell *shell, size_t argc,
                                char **argv) {
  int err;
  char addr_str[BT_ADDR_LE_STR_LEN];
  bt_addr_le_t addrs[CONFIG_BT_ID_MAX];

  shell_print(shell, "Subcommands:");
  shell_print(shell, "  unpair - Unpair all bonds");
  shell_print(shell, "  private [0|1] - Set private advertising");

  size_t count = CONFIG_BT_ID_MAX;
  bt_id_get(addrs, &count);
  for (size_t i = 0; i < count; i++) {
    bt_addr_le_to_str(&addrs[i], addr_str, sizeof(addr_str));
    shell_print(shell, "Identity %d: %s (type=%d)", i, addr_str, addrs[i].type);
  }
  shell_print(shell, "Bonds:");
  bt_foreach_bond(BT_ID_DEFAULT, print_bond_cb, (void *)shell);
  shell_print(shell, "Connections:");
  bt_conn_foreach(BT_CONN_TYPE_LE, print_conn_cb, (void *)shell);
  shell_print(shell, "Advertiser kind: %s",
              bt_adv_get_kind() == ADVERTISER_KIND_PRIVATE ? "Private"
                                                           : "Public");
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    bt_addr_le_to_str(&instance->known_peer, addr_str, sizeof(addr_str));
    struct bt_conn *conn = bt_conn_ref(instance->conn);

    shell_print(shell, "Profile %s:", instance->profile->name);
    shell_print(shell, "  Peer Address: %s", addr_str);
    if (conn) {
      struct bt_conn_info conn_info = {0};
      bt_conn_get_info(conn, &conn_info);
      shell_print(
          shell, "  %s",
          conn_info.state == BT_CONN_STATE_CONNECTED       ? "Connected"
          : conn_info.state == BT_CONN_STATE_DISCONNECTED  ? "Disconnected"
          : conn_info.state == BT_CONN_STATE_DISCONNECTING ? "Disconnecting"
          : conn_info.state == BT_CONN_STATE_CONNECTING    ? "Connecting"
                                                           : "Unknown");
      shell_print(shell, "  Interval: %d us", conn_info.le.interval_us);
      uint16_t handle;
      err = bt_hci_get_conn_handle(conn, &handle);
      if (0 != err) {
        shell_error(shell, "Failed to get connection handle: %d", err);
        bt_conn_unref(conn);
        continue;
      }
      sdc_hci_cmd_sp_read_rssi_t params = {
          .handle = handle,
      };
      sdc_hci_cmd_sp_read_rssi_return_t return_params = {0};
      err = sdc_hci_cmd_sp_read_rssi(&params, &return_params);
      if (0 != err) {
        shell_error(shell, "Failed to read RSSI: %d", err);
      } else {
        shell_print(shell, "  RSSI: %d dBm", return_params.rssi);
      }
      bt_conn_unref(conn);
    } else {
      shell_print(shell, "  Scanning");
    }
  }

  return -EINVAL;
}

static int shell_bt_cmd_forget(const struct shell *shell, size_t argc,
                               char **argv) {
  uint8_t count = scan_enter_pairing_mode(argc == 2 ? argv[1] : NULL);
  shell_print(shell, "Reset %d profiles", count);
  return count > 0 ? 0 : -ENOENT;
}

static int shell_bt_cmd_unpair(const struct shell *shell, size_t argc,
                               char **argv) {
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

#if IS_ENABLED(CONFIG_BT_PRIVACY)
static int shell_bt_cmd_private(const struct shell *shell, size_t argc,
                                char **argv) {
  enum advertiser_kind kind = bt_adv_get_kind();
  if (argc == 1) {
    shell_print(shell, "Advertiser kind: %s",
                kind == ADVERTISER_KIND_PRIVATE ? "Private" : "Public");
    return 0;
  }

  if (strcmp(argv[1], "1") == 0) {
    shell_print(shell, "Setting advertiser kind to private");
    bt_adv_set_kind(ADVERTISER_KIND_PRIVATE);
  } else if (strcmp(argv[1], "0") == 0) {
    shell_print(shell, "Setting advertiser kind to public");
    bt_adv_set_kind(ADVERTISER_KIND_PUBLIC);
  } else {
    shell_error(shell, "Invalid argument: %s", argv[1]);
    return -EINVAL;
  }

  return 0;
}
#else
static int shell_bt_cmd_private(const struct shell *shell, size_t argc,
                                char **argv) {
  shell_error(shell, "Bluetooth privacy is not enabled at build time");
  return -ENOTSUP;
}
#endif /* IS_ENABLED(CONFIG_BT_PRIVACY) */

SHELL_STATIC_SUBCMD_SET_CREATE(
    bt_sub,
    SHELL_CMD_ARG(forget, NULL,
                  SHELL_HELP("Forget central profile persistence",
                             "[name|MAC]"),
                  shell_bt_cmd_forget, 1, 1),
    SHELL_CMD_ARG(unpair, NULL, SHELL_HELP("Unpair peripheral bonds", "[MAC]"),
                  shell_bt_cmd_unpair, 1, 1),
    SHELL_CMD_ARG(private, NULL,
                  SHELL_HELP("Get/set private advertising", "[0|1]"),
                  shell_bt_cmd_private, 1, 1),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(bt, &bt_sub, "Bluetooth commands", shell_bt_cmd_handler);
