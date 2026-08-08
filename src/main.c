#include <stdbool.h>
#include <stdint.h>
#include <sys/errno.h>

#include <bluetooth/gatt_dm.h>
#include <helpers/nrfx_reset_reason.h>
#include <hw_id.h>
#include <shell/shell_bt_nus.h>

#include <ant_key_manager.h>

#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <dk_buttons_and_leds.h>

#include "ant_init.h"
#include "ant_parameters.h"
#include "ant_profiles.h"
#include "battery_gauge.h"
#include "bootloader.h"
#include "cac_acm_serial.h"
#include "central_profile.h"
#include "central_t800.h"
#include "gatt_battery.h"
#include "gatt_callbacks.h"
#include "gatt_system_info.h"
#include "leds.h"
#include "poweroff.h"
#include "shell_ant_slave.h"
#include "watchdog.h"

#include "main.h"

#define STACKSIZE 1024
#define PRIORITY 7

#define CONNECTION_ATTEMPT_LIMIT 3

#define LOW_BATTERY_THRESHOLD 20
#define LOW_BATTERY_THRESHOLD_HYSTERESIS 20

#define BT_CENTRAL_KNOWN_PEER_SETTINGS_SUBTREE "bt_central_known_peer"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define CONN_CREATE_PARAMS                                                     \
  BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE,                                 \
                          (2 * BT_GAP_SCAN_FAST_INTERVAL),                     \
                          BT_GAP_SCAN_FAST_WINDOW)

static uint8_t connection_attempt_count = 0;

static uint8_t hwid[MAX(3, HW_ID_LEN)];

struct central_profile_instance {
  const struct central_profile *profile;
  bt_addr_le_t known_peer;
  uint8_t led_idx;
  struct bt_conn *conn;
  const struct bt_uuid *const *next_service_uuid;
  bool is_registered;
};

struct scan_ad_data {
  bt_addr_le_t peer;
  char name[32];
  const struct bt_uuid *uuids[16];
  uint8_t uuids_count;
};

static struct central_profile_instance central_profile_instances[] = {
    {
        .profile = &central_t800_profile,
        .led_idx = CENTRAL1_CON_STATUS_LED_BIT,
        .known_peer = {0},
        .conn = NULL,
        .next_service_uuid = NULL,
        .is_registered = false,
    },
};

static void
bt_conn_foreach_disconnect_connected_peripheral(struct bt_conn *conn,
                                                void *data) {
  struct bt_conn_info conn_info;

  int err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("Failed to get connection info (err %d)", err);
  }
  if (conn_info.type != BT_CONN_TYPE_LE ||
      conn_info.state != BT_CONN_STATE_CONNECTED ||
      conn_info.role != BT_CONN_ROLE_CENTRAL) {
    return;
  }
  bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void bt_conn_foreach_count_connected_peripheral(struct bt_conn *conn,
                                                       void *data) {
  struct bt_conn_info conn_info;
  uint32_t *count = (uint32_t *)data;

  int err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("Failed to get connection info (err %d)", err);
    return;
  }
  if (conn_info.type != BT_CONN_TYPE_LE ||
      conn_info.state != BT_CONN_STATE_CONNECTED ||
      conn_info.role != BT_CONN_ROLE_CENTRAL) {
    return;
  }
  (*count)++;
}

static bool is_central_slot_open(void) {
  uint32_t count = 0;
  bt_conn_foreach(BT_CONN_TYPE_LE, bt_conn_foreach_count_connected_peripheral,
                  &count);
  return count < ARRAY_SIZE(central_profile_instances);
}

static int bt_central_known_peer_set(const char *name, size_t len,
                                     settings_read_cb read_cb, void *cb_arg) {
  LOG_INF("bt_central_known_peer_set: %s", name);
  char addr_str[BT_ADDR_LE_STR_LEN] = {0};
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    // skip if not the same profile
    if (strcmp(name, central_profile_instances[i].profile->name) != 0) {
      continue;
    }

    int rc = read_cb(cb_arg, &central_profile_instances[i].known_peer,
                     sizeof(central_profile_instances[i].known_peer));
    if (rc == sizeof(central_profile_instances[i].known_peer)) {
      bt_addr_le_to_str(&central_profile_instances[i].known_peer, addr_str,
                        sizeof(addr_str));
      LOG_INF("Known peer restored for profile: %s (%s)",
              central_profile_instances[i].profile->name, addr_str);
      return 0;
    }
    LOG_WRN("Unexpected retrieved value length: %d. Profile %s will run in "
            "pairing mode.",
            rc, central_profile_instances[i].profile->name);

    memset(&central_profile_instances[i].known_peer, 0,
           sizeof(central_profile_instances[i].known_peer));
    return rc;
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(known_peer,
                               BT_CENTRAL_KNOWN_PEER_SETTINGS_SUBTREE, NULL,
                               bt_central_known_peer_set, NULL, NULL);

static struct central_profile_instance *
central_profile_instance_get(struct bt_conn *conn) {
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    if (central_profile_instances[i].conn == conn) {
      return &central_profile_instances[i];
    }
  }
  return NULL;
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_func,
};

int64_t last_button_press_time = 0;

void start_pairing_mode_work_handler(struct k_work *work) {
  LOG_INF("Long press detected, entering pairing mode");
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    bt_addr_le_copy(&central_profile_instances[i].known_peer, BT_ADDR_LE_ANY);
  }
  bt_le_scan_stop();
  bt_conn_foreach(BT_CONN_TYPE_LE,
                  bt_conn_foreach_disconnect_connected_peripheral, NULL);
  scan_start();
  for (int i = 0; i < 5; i++) {
    led_data_activity();
    k_sleep(K_MSEC(500));
  }
}

K_WORK_DELAYABLE_DEFINE(start_pairing_mode_work,
                        start_pairing_mode_work_handler);

void btn_handler_fn(uint32_t button_state, uint32_t has_changed) {
  button_state &= 1;
  has_changed &= 1;
  LOG_INF("Button state: %d, has_changed: %d", button_state, has_changed);
  if (has_changed) {
    if (button_state) {
      poweroff_request_wakeup();
      last_button_press_time = k_uptime_get();
      k_work_reschedule(&start_pairing_mode_work, K_SECONDS(3));
    } else {
      int64_t now = k_uptime_get();
      uint32_t duration = now - last_button_press_time;
      if (duration < 500) {
        led_brightness_next();
        k_work_cancel_delayable(&start_pairing_mode_work);
      }
    }
  }
}

bool scan_ad_data_callback(struct bt_data *data, void *context) {
  struct scan_ad_data *scan_ad_data = (struct scan_ad_data *)context;
  switch (data->type) {
  case BT_DATA_NAME_COMPLETE:
  case BT_DATA_NAME_SHORTENED:
    if (data->data_len + 1 > sizeof(scan_ad_data->name)) {
      LOG_ERR("Name too long: %d", data->data_len);
      return false;
    }
    memcpy(scan_ad_data->name, data->data, data->data_len);
    scan_ad_data->name[data->data_len] = '\0';
    break;
  case BT_DATA_UUID16_ALL:
  case BT_DATA_UUID16_SOME:
    if (scan_ad_data->uuids_count == ARRAY_SIZE(scan_ad_data->uuids)) {
      return true;
    }
    if (data->data_len % 2 != 0) {
      LOG_ERR("Invalid UUID16 data length: %d", data->data_len);
      return false;
    }
    for (size_t i = 0; i < data->data_len; i += 2) {
      uint16_t uuid = data->data[i] | (data->data[i + 1] << 8);

      for (size_t j = 0; j < ARRAY_SIZE(central_profile_instances); j++) {
        for (const struct bt_uuid *const *target_uuid =
                 central_profile_instances[j].profile->service_uuids;
             *target_uuid != NULL; target_uuid++) {
          if ((*target_uuid)->type != BT_UUID_TYPE_16) {
            continue;
          }
          uint16_t target_uuid_value = BT_UUID_16(*target_uuid)->val;
          if (target_uuid_value == uuid) {
            scan_ad_data->uuids[scan_ad_data->uuids_count++] = *target_uuid;
            continue;
          }
        }
      }
    }
    break;
  case BT_DATA_UUID128_ALL:
  case BT_DATA_UUID128_SOME:
    if (data->data_len % 16 != 0) {
      LOG_ERR("Invalid UUID128 data length: %d", data->data_len);
      return false;
    }
    for (size_t i = 0; i < data->data_len; i += 16) {
      for (size_t j = 0; j < ARRAY_SIZE(central_profile_instances); j++) {
        for (const struct bt_uuid *const *target_uuid =
                 central_profile_instances[j].profile->service_uuids;
             *target_uuid != NULL; target_uuid++) {
          if ((*target_uuid)->type != BT_UUID_TYPE_128) {
            continue;
          }
          const uint8_t *target_uuid_value = BT_UUID_128(*target_uuid)->val;
          if (memcmp(target_uuid_value, data->data + i, 16) == 0) {
            scan_ad_data->uuids[scan_ad_data->uuids_count++] = *target_uuid;
            continue;
          }
        }
      }
    }
    break;
  }
  return true;
}

void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
             struct net_buf_simple *buf) {
  if (adv_type != BT_GAP_ADV_TYPE_ADV_IND &&
      adv_type != BT_GAP_ADV_TYPE_SCAN_RSP) {
    return;
  }

  if (!is_central_slot_open()) {
    LOG_INF("Central slot is full, stopping scan");
    bt_le_scan_stop();
    return;
  }

  bool all_known = true;
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    if (bt_addr_le_eq(&central_profile_instances[i].known_peer, addr)) {
      LOG_INF("Known peer found: %s",
              central_profile_instances[i].profile->name);

      bt_le_scan_stop();
      int err = bt_conn_le_create(
          addr, CONN_CREATE_PARAMS,
          BT_LE_CONN_PARAM(6, 16, 0, BT_GAP_MS_TO_CONN_TIMEOUT(2000)),
          &central_profile_instances[i].conn);

      if (err) {
        LOG_ERR("Failed to create connection (err %d)", err);
      }

      return;
    }
    if (memcmp(&central_profile_instances[i].known_peer, BT_ADDR_LE_ANY,
               sizeof(bt_addr_le_t)) == 0) {
      all_known = false;
    }
  }
  // all profiles have known peers, stop interpreting scan data
  if (all_known) {
    return;
  }

  if (rssi < -70) {
    // ignore too weak signal
    return;
  }
  static struct scan_ad_data scan_ad_data = {0};
  if (!bt_addr_le_eq(&scan_ad_data.peer, addr)) {
    memset(&scan_ad_data, 0, sizeof(scan_ad_data));
    bt_addr_le_copy(&scan_ad_data.peer, addr);
  }

  bt_data_parse(buf, scan_ad_data_callback, &scan_ad_data);

  if (scan_ad_data.uuids_count == 0) {
    return;
  }

  // find matching profile
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    if (central_profile_instances[i].conn != NULL) {
      continue;
    }

    if (central_profile_instances[i].profile->device_name_prefix != NULL) {
      size_t len =
          strlen(central_profile_instances[i].profile->device_name_prefix);
      if (strlen(scan_ad_data.name) < len) {
        continue;
      }
      if (memcmp(scan_ad_data.name,
                 central_profile_instances[i].profile->device_name_prefix,
                 len) != 0) {
        continue;
      }
    }
    bool matches = true;

    for (const struct bt_uuid *const *target_uuid =
             central_profile_instances[i].profile->service_uuids;
         *target_uuid != NULL; target_uuid++) {
      bool found = false;
      for (size_t j = 0; j < scan_ad_data.uuids_count; j++) {
        if (bt_uuid_cmp(*target_uuid, scan_ad_data.uuids[j]) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        matches = false;
        break;
      }
    }
    if (matches) {
      LOG_INF("Matching profile found: %s",
              central_profile_instances[i].profile->name);

      bt_le_scan_stop();
      int err = bt_conn_le_create(
          addr, CONN_CREATE_PARAMS,
          BT_LE_CONN_PARAM(6, 16, 0, BT_GAP_MS_TO_CONN_TIMEOUT(2000)),
          &central_profile_instances[i].conn);

      if (err) {
        LOG_ERR("Failed to create connection (err %d)", err);
      }

      break;
    }
  }
}

static int scan_start(void) {
  if (!is_central_slot_open()) {
    return 0;
  }

  int err;

  bool all_known = true;
  bool fal_configured = true;
  for (size_t i = 0; i < ARRAY_SIZE(central_profile_instances); i++) {
    if (memcmp(&central_profile_instances[i].known_peer, BT_ADDR_LE_ANY,
               sizeof(bt_addr_le_t)) == 0) {
      all_known = false;
      break;
    }
    err =
        bt_le_filter_accept_list_add(&central_profile_instances[i].known_peer);
    if (err) {
      LOG_WRN("Failed to add known peer to filter accept list (err %d)", err);
      fal_configured = false;
    }
  }

  LOG_INF("Configuring scanner (passive: %d, with_fal: %d)", all_known,
          all_known && fal_configured);

  err = bt_le_scan_start(
      BT_LE_SCAN_PARAM(
          all_known ? BT_LE_SCAN_TYPE_PASSIVE : BT_LE_SCAN_TYPE_ACTIVE,
          (all_known && fal_configured) ? BT_LE_SCAN_OPT_FILTER_ACCEPT_LIST
                                        : BT_LE_SCAN_OPT_NONE,
          CONN_CREATE_PARAMS->interval, CONN_CREATE_PARAMS->window),
      scan_cb);

  if (err && err != -EALREADY) {
    LOG_ERR("Scanning failed to start (err %d)", err);
  } else {
    if (err == 0) {
      LOG_INF("Scanning started (passive: %d)", all_known);
    }
  }

  return err;
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_CPS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME)};

static struct k_work adv_work;

uint8_t bt_gatt_discover_cb(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params);

static void adv_work_handler(struct k_work *work) {
  int err = bt_le_adv_start(
      BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_MS_TO_ADV_INTERVAL(400),
                      BT_GAP_MS_TO_ADV_INTERVAL(600), NULL),
      ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

  if (err) {
    LOG_ERR("Advertising failed to start (err %d)", err);
    return;
  }

  LOG_INF("Advertising successfully started");
}

static void advertising_start(void) { k_work_submit(&adv_work); }

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context) {
  int err;

  LOG_INF("The discovery procedure succeeded");

  struct central_profile_instance *instance =
      (struct central_profile_instance *)context;

  bt_gatt_dm_data_print(dm);

  struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

  err = instance->profile->on_discovery(dm);
  if (err) {
    LOG_ERR("Discovery procedure failed (err %d)", err);
    bt_gatt_dm_data_release(dm);
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    return;
  }

  err = bt_gatt_dm_data_release(dm);
  if (err && err != -EALREADY) {
    LOG_ERR("Could not release the discovery data (err %d)", err);
  }

  bt_gatt_dm_continue(dm, context);
}

static void discovery_not_found_cb(struct bt_conn *conn, void *context) {
  struct central_profile_instance *instance =
      central_profile_instance_get(conn);
  if (!instance) {
    LOG_ERR("No instance found for connection");
    return;
  }
  instance->next_service_uuid++;

  if (*instance->next_service_uuid != NULL) {
    LOG_INF("Starting discovery procedure for next service");
    int err = bt_gatt_dm_start(conn, *instance->next_service_uuid,
                               &discovery_cb, context);
    if (err) {
      LOG_ERR("Could not start the discovery procedure (err %d)", err);
    }
  } else {
    LOG_INF("End of discovery procedure");
    connection_attempt_count = 0;
    if (!instance->is_registered) {
      const bt_addr_le_t *dst = bt_conn_get_dst(conn);
      bt_addr_le_copy(&instance->known_peer, dst);
      char name_buf[64];
      if (snprintf(name_buf, sizeof(name_buf), "%s%c%s",
                   BT_CENTRAL_KNOWN_PEER_SETTINGS_SUBTREE,
                   SETTINGS_NAME_SEPARATOR, instance->profile->name) > 0) {
        int err = settings_save_one(name_buf, dst, sizeof(*dst));
        if (err) {
          LOG_ERR("Failed to save known peer (err %d)", err);
        }
      }
      instance->profile->on_connected(conn);
      instance->is_registered = true;
    }
    // restart scanning for other profiles if needed
    scan_start();
  }
}

static void discovery_error_found_cb(struct bt_conn *conn, int err,
                                     void *context) {
  LOG_ERR("Discovery procedure failed with %d, disconnecting", err);
  bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  scan_start();
}

const struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed_cb,
    .service_not_found = discovery_not_found_cb,
    .error_found = discovery_error_found_cb};

const struct bt_uuid *discovery_services[] = {BT_UUID_MESH_PROXY, BT_UUID_BAS,
                                              NULL};

static void auth_cancel(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_WRN("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
  if (bonded) {
    int err = bt_le_filter_accept_list_add(bt_conn_get_dst(conn));
    if (err) {
      LOG_ERR("Failed to add peer to filter accept list (err %d)", err);
    }
  }
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_ERR("Pairing failed conn: %s, reason %d %s", addr, reason,
          bt_security_err_to_str(reason));
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(peer, addr, sizeof(addr));
  LOG_INF("Bond deleted: %s", addr);
  int err = bt_le_filter_accept_list_remove(peer);
  if (err) {
    LOG_ERR("Failed to remove peer from filter accept list (err %d)", err);
  }
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

static const struct bt_conn_auth_cb auth_callbacks = {
    .passkey_display = auth_passkey_display, .cancel = auth_cancel};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
    .bond_deleted = bond_deleted,
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed};

static void on_connected(struct bt_conn *conn, uint8_t conn_err) {
  int err;
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  struct central_profile_instance *instance =
      central_profile_instance_get(conn);

  if (conn_err) {
    LOG_ERR("Failed to connect to %s, 0x%02x %s", addr, conn_err,
            bt_hci_err_to_str(conn_err));

    if (++connection_attempt_count > CONNECTION_ATTEMPT_LIMIT) {
      LOG_ERR("Connection attempt limit reached, resetting");
      sys_reboot(SYS_REBOOT_COLD);
    }
    scan_start();

    if (instance) {
      instance->conn = NULL;
    }

    bt_conn_unref(conn);

    return;
  }

  struct bt_conn_info conn_info = {0};
  err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("bt_conn_info() returned %d", err);
  }

  LOG_INF("Connected: %s", addr);

  if (instance) {
    led_set_bit(instance->led_idx);

    instance->next_service_uuid = instance->profile->service_uuids;

    const struct bt_conn_le_phy_param preferred_phy = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_rx_phy = BT_GAP_LE_PHY_CODED,
        .pref_tx_phy = BT_GAP_LE_PHY_CODED,
    };
    err = bt_conn_le_phy_update(conn, &preferred_phy);
    if (err) {
      LOG_ERR("bt_conn_le_phy_update() returned %d", err);
    }

    err = bt_gatt_dm_start(conn, *instance->next_service_uuid, &discovery_cb,
                           (void *)instance);
    if (err) {
      LOG_ERR("Could not start the discovery procedure, disconnecting (err %d)",
              err);
      bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return;
    }
  } else {
    advertising_start();
    shell_bt_nus_enable(conn);
    bt_conn_set_security(conn, BT_SECURITY_L4);
    bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
  }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_WRN("Disconnected: %s, reason 0x%02x %s", addr, reason,
          bt_hci_err_to_str(reason));

  struct central_profile_instance *instance =
      central_profile_instance_get(conn);

  if (instance) {

    if (++connection_attempt_count > CONNECTION_ATTEMPT_LIMIT) {
      LOG_ERR("Connection attempt limit reached, resetting");
      sys_reboot(SYS_REBOOT_COLD);
    }

    if (instance->is_registered) {
      instance->profile->on_disconnected(conn, reason);
      instance->is_registered = false;
    }

    struct bt_conn *conn = atomic_ptr_clear((void **)&instance->conn);
    led_clear_bit(instance->led_idx);
    if (conn)
      bt_conn_unref(conn);
  } else {
    shell_bt_nus_disable();
  }
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
                                enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  if (!err) {
    LOG_INF("Security changed: %s level %u", addr, level);
  } else {
    LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
  }
}

static void on_conn_recycled(void) {
  advertising_start();

  scan_start();
}

void on_le_phy_updated(struct bt_conn *conn,
                       struct bt_conn_le_phy_info *param) {
  LOG_INF("PHY updated. New PHY: %s",
          (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M)         ? "1M"
          : (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M)       ? "2M"
          : (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S2) ? "Coded(S2)"
          : (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8) ? "Coded(S8)"
                                                                : "Unknown");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = on_security_changed,
    .recycled = on_conn_recycled,
    .le_phy_updated = on_le_phy_updated,
};

static void ant_evt_handler(ant_evt_t *p_ant_evt) {

  if (p_ant_evt->channel == bpwr_channel_config.channel_number) {
    ant_bike_power_evt_handler(p_ant_evt, &bike_power);
    return;
  } else if (p_ant_evt->channel == bpwr_diag_channel_config.channel_number) {
    ant_bike_power_diag_evt_handler(p_ant_evt, &bike_power);
    return;
  } else if (p_ant_evt->channel ==
             antplus_generic_slave_config.channel_number) {
    ant_generic_slave_evt_handler(p_ant_evt);
    return;
  } else {
    for (size_t i = 0; i < ANT_WAKEUP_CHANNEL_SLOT_COUNT; i++) {
      if (p_ant_evt->channel == antplus_wakeup_slave_config[i].channel_number) {
        if (p_ant_evt->event == EVENT_RX)
          poweroff_request_wakeup();
        if (p_ant_evt->event == EVENT_RX_SEARCH_TIMEOUT)
          ant_channel_close(antplus_wakeup_slave_config[i].channel_number);

        return;
      }
    }
  }
  LOG_ERR("Unknown channel: %d", p_ant_evt->channel);
}

static int ant_stack_setup(void) {
  int err = 0;

  err = ant_init();

  if (err) {
    LOG_ERR("ant_init failed: %d", err);
    return err;
  }

  LOG_INF("ANT version %s", ANT_VERSION_STRING);

  err = ant_cb_register(&ant_evt_handler);
  if (err) {
    LOG_ERR("ant_cb_register failed: %d", err);
    return err;
  }

  err = ant_plus_key_set(ANT_NETWORK_ANTPLUS);
  if (err) {
    LOG_ERR("ant_plus_key_set failed: %d", err);
    return err;
  }

  err = ant_bike_power_diag_key_set(ANT_NETWORK_BPWR_DIAG);
  if (err) {
    LOG_ERR("ant_bike_power_diag_key_set failed: %d", err);
    return err;
  }

  return 0;
}

static int ant_profile_setup(void) {
  int err;

  ant_bike_power_init(&bike_power, central_t800_offset_compensation_start_ant);

  err = ant_channel_init(&bpwr_channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: %d", err);
    return err;
  }
  err = ant_channel_init(&bpwr_diag_channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: %d", err);
    return err;
  }

  return 0;
}

static void setup_accept_list_cb(const struct bt_bond_info *info,
                                 void *user_data) {
  int *bond_cnt = user_data;
  if ((*bond_cnt) < 0) {
    return;
  }
  int err = bt_le_filter_accept_list_add(&info->addr);
  if (err) {
    LOG_INF("Cannot add peer to Filter Accept List (err: %d)\n", err);
    (*bond_cnt) = -EIO;
  } else {
    (*bond_cnt)++;
  }
}

int bt_setup(void) {
  int err;
  err = bt_enable(NULL);
  if (err) {
    if (err == -EALREADY) {
      return 0;
    }
    return err;
  }

  if (IS_ENABLED(CONFIG_SETTINGS)) {
    settings_load();
  }

  err = bt_le_filter_accept_list_clear();
  if (err) {
    LOG_ERR("bt_le_filter_accept_list_clear failed: %d", err);
    return err;
  }
  int bond_cnt = 0;
  bt_foreach_bond(BT_ID_DEFAULT, setup_accept_list_cb, &bond_cnt);
  if (bond_cnt < 0) {
    LOG_ERR("Failed to add bonds to accept list");
    bt_disable();
    return -EIO;
  }

  led_data_activity();

  err = scan_start();

  if (err) {
    bt_disable();
    return err;
  }

  advertising_start();

  return 0;
}

int main_loop(void) {
  int err;
  bool low_batt = false;
  uint32_t reset_reason = nrfx_reset_reason_get();
  if (reset_reason & NRFX_RESET_REASON_DOG_MASK) {
    bootloader_enter();
  }

  err = hw_id_get(hwid, sizeof(hwid));
  if (err) {
    return 0;
  }
  uint32_t hwid_int = 0;
  memcpy(&hwid_int, hwid, MIN(sizeof(hwid), sizeof(hwid_int)));
  // write extended device id
  bpwr_channel_config.transmission_type ^= (hwid_int << 4);
  bpwr_channel_config.device_number =
      1 + (hwid_int >> 4) % 65535; // device number cannot be 0
  bpwr_diag_channel_config.transmission_type ^= (hwid_int << 4);
  bpwr_diag_channel_config.device_number =
      1 + (hwid_int >> 4) % 65535; // device number cannot be 0

  LOG_INIT();

  err = battery_gauge_setup();
  if (err) {
    LOG_ERR("Failed to setup battery gauge: %d", err);
    return err;
  }

  err = dk_buttons_init(btn_handler_fn);
  if (err) {
    LOG_ERR("Buttons init failed (err %d)", err);
    return err;
  }

  err = ant_stack_setup();
  if (err) {
    LOG_ERR("ANT stack setup failed (err %d)", err);
    return err;
  }

  err = ant_profile_setup();
  if (err) {
    LOG_ERR("ANT profile setup failed (err %d)", err);
    return err;
  }

  err = shell_bt_nus_init();
  if (err) {
    LOG_ERR("Shell BT NUS init failed (err %d)", err);
    return err;
  }

  err = bt_conn_auth_cb_register(&auth_callbacks);
  if (err) {
    LOG_ERR("Failed to register authorization callbacks.");
    return err;
  }

  err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
  if (err) {
    LOG_ERR("Failed to register authorization info callbacks.");
    return err;
  }

  k_work_init(&adv_work, adv_work_handler);

  err = bt_setup();
  if (err) {
    LOG_ERR("BT setup failed (err %d)", err);
    return err;
  }

  gatt_sys_info_init();

  led_set_bit(POWER_LED_BIT);

  int64_t no_activity_since_ms = k_uptime_get();

  for (int i = 0;; i++) {
    err = battery_gauge_upkeep();
    if (err) {
      LOG_ERR("Failed to upkeep battery gauge: %d", err);
    }
    uint16_t battery_mv = battery_gauge_get_mv();
    uint8_t battery_level = battery_gauge_get_level(battery_mv);
    watchdog_feed();
    low_batt |= battery_level < LOW_BATTERY_THRESHOLD;
    low_batt &= battery_level <
                LOW_BATTERY_THRESHOLD + LOW_BATTERY_THRESHOLD_HYSTERESIS;

    bas_battery_level_self_set(battery_level, battery_mv);
    ant_bike_power_set_self_battery_state(&bike_power, battery_level,
                                          battery_mv);
    if (low_batt) {
      ((i % 2) ? led_set_bit : led_clear_bit)(POWER_LED_BIT);
    }

    uint32_t is_active = is_usb_enabled();
    bt_conn_foreach(BT_CONN_TYPE_LE, bt_conn_foreach_count_connected_peripheral,
                    &is_active);
    if (is_active) {
      no_activity_since_ms = k_uptime_get();
    }

#if CONFIG_POWEROFF
    if (CONFIG_AUTO_POWER_OFF_TIMEOUT > 0 &&
        (k_uptime_get() - no_activity_since_ms) >
            CONFIG_AUTO_POWER_OFF_TIMEOUT * 1000) {
      LOG_INF("Auto power off triggered");
      enter_poweroff();
    }
#endif

    k_sleep(K_MSEC(500));
  }

  LOG_WRN("Main loop exited");
  return 0;
}

int main(void) {
  int err;
  err = main_loop();
  if (err) {
    printk("Main loop failed (err %d)", err);
    bootloader_enter();
    return err;
  }
  return 0;
}