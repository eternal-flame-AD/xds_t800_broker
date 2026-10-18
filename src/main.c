#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/errno.h>

#include <bluetooth/gatt_dm.h>
#include <shell/shell_bt_nus.h>

#if IS_ENABLED(CONFIG_ANT)
#include "ant_profiles.h"
#include "shell_ant_slave.h"
#include <ant_init.h>
#include <ant_key_manager.h>
#endif

#if IS_ENABLED(CONFIG_SENSOR)
#include <zephyr/drivers/sensor.h>
#endif

#if IS_ENABLED(CONFIG_BT_CHANNEL_SOUNDING)
#include <bluetooth/services/ras.h>
#include <zephyr/bluetooth/cs.h>
#endif

#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/devicetree.h>
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

#include "advertiser.h"
#include "battery_gauge.h"
#include "cdc_acm_serial.h"
#include "central_profile.h"
#include "central_t800.h"
#include "gatt_battery.h"
#include "leds.h"

#if IS_ENABLED(CONFIG_USE_IMU_ST_LSM6DSL)
#include "lsm6dsl.h"
#endif

#include "poweroff.h"
#include "scanner.h"
#include "watchdog.h"

#define CONNECTION_ATTEMPT_LIMIT 3

#define LOW_BATTERY_THRESHOLD 20
#define LOW_BATTERY_THRESHOLD_HYSTERESIS 20

#define BT_CENTRAL_KNOWN_PEER_SETTINGS_SUBTREE "bt_central_known_peer"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(vccpoweroff)) &&                               \
    DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(vccpoweroff))

const struct led_dt_spec vcc_poweroff_pin =
    LED_DT_SPEC_GET(DT_NODELABEL(vccpoweroff));

#define HAS_VCC_POWEROFF_PIN 1
#else
#define HAS_VCC_POWEROFF_PIN 0
#endif

static uint8_t connection_attempt_count = 0;
const static struct bt_gatt_dm_cb discovery_cb;

// some board variants of the XIAO nrf54L15 does not have this.
// so we need to catch and change behavior
#if IS_ENABLED(CONFIG_USE_IMU_ST_LSM6DSL)
static bool lsm6dsl_initialized = false;
#endif

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

static int bt_central_known_peer_set(const char *name, size_t len,
                                     settings_read_cb read_cb, void *cb_arg) {
  LOG_INF("bt_central_known_peer_set: %s", name);
  char addr_str[BT_ADDR_LE_STR_LEN] = {0};
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    // skip if not the same profile
    if (strcmp(name, instance->profile->name) == 0) {
      int rc =
          read_cb(cb_arg, &instance->known_peer, sizeof(instance->known_peer));
      if (rc == sizeof(instance->known_peer)) {
        bt_addr_le_to_str(&instance->known_peer, addr_str, sizeof(addr_str));
        LOG_INF("Known peer restored for profile: %s (%s)",
                instance->profile->name, addr_str);
        return 0;
      }
      LOG_WRN("Unexpected retrieved value length: %d. Profile %s will run in "
              "pairing mode.",
              rc, instance->profile->name);

      memset(&instance->known_peer, 0, sizeof(instance->known_peer));
      return rc;
    }
  }
  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(known_peer,
                               BT_CENTRAL_KNOWN_PEER_SETTINGS_SUBTREE, NULL,
                               bt_central_known_peer_set, NULL, NULL);

static struct central_profile_instance *
central_profile_instance_get(struct bt_conn *conn) {
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    if (instance->conn == conn) {
      return instance;
    }
  }
  return NULL;
}

static void mtu_exchange_func(struct bt_conn *conn, uint8_t att_err,
                              struct bt_gatt_exchange_params *params) {
  if (att_err) {
    LOG_WRN("MTU exchange faled (err %d)", att_err);
  } else {
    uint16_t payload_mtu =
        bt_gatt_get_mtu(conn) - 3; // 3 bytes used for Attribute headers.
    LOG_INF("New MTU: %d bytes", payload_mtu);
  }
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = mtu_exchange_func,
};

int64_t last_button_press_time = 0;

static void start_pairing_mode_work_handler(struct k_work *work) {
  scan_enter_pairing_mode(NULL);
}

K_WORK_DELAYABLE_DEFINE(start_pairing_mode_work,
                        start_pairing_mode_work_handler);

static void btn_handler_fn(uint32_t button_state, uint32_t has_changed) {
  button_state &= 1;
  has_changed &= 1;
  LOG_INF("Button state: %d, has_changed: %d", button_state, has_changed);
  if (has_changed) {
    if (button_state) {
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

uint8_t bt_gatt_discover_cb(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params);

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
    scan_start(true);
  }
}

static void discovery_error_found_cb(struct bt_conn *conn, int err,
                                     void *context) {
  LOG_ERR("Discovery procedure failed with %d, disconnecting", err);
  bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  scan_start(true);
}

const static struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed_cb,
    .service_not_found = discovery_not_found_cb,
    .error_found = discovery_error_found_cb};

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
    advertising_notify_bond_changed();
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
  advertising_notify_bond_changed();
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

static enum bt_security_err
auth_pairing_accept(struct bt_conn *conn,
                    const struct bt_conn_pairing_feat *const feat) {
  if (advertising_is_public()) {
    return BT_SECURITY_ERR_SUCCESS;
  }
  return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}

static const struct bt_conn_auth_cb auth_callbacks = {
    .pairing_accept = auth_pairing_accept,
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
    .bond_deleted = bond_deleted,
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed};

static struct bt_conn *nus_conn = NULL;

static void on_connected(struct bt_conn *conn, uint8_t conn_err) {
  int err;
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  struct bt_conn_info conn_info = {0};
  err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("bt_conn_info() returned %d", err);
  }

  struct central_profile_instance *instance =
      central_profile_instance_get(conn);

  if (conn_err) {
    LOG_ERR("Failed to connect to %s, 0x%02x %s", addr, conn_err,
            bt_hci_err_to_str(conn_err));

    // do not count strikes against connection attempts for peripheral
    // connections or remote lack of response
    if (conn_info.role != BT_CONN_ROLE_PERIPHERAL &&
        conn_err != BT_HCI_ERR_PAGE_TIMEOUT &&
        conn_err != BT_HCI_ERR_CONN_TIMEOUT &&
        ++connection_attempt_count > CONNECTION_ATTEMPT_LIMIT) {
      LOG_ERR("Connection attempt limit reached, resetting");
      sys_reboot(SYS_REBOOT_COLD);
    }
    scan_start(true);

    if (instance) {
      struct bt_conn *conn = atomic_ptr_clear((void **)&instance->conn);
      if (conn) {
        led_temp_dim_dec();
      }
    }

    bt_conn_unref(conn);

    return;
  }

  LOG_INF("Connected: %s", addr);

  if (instance) {
    led_set_bit(instance->led_idx);

    instance->next_service_uuid = instance->profile->service_uuids;

    const struct bt_conn_le_phy_param param = {
        .options = BT_CONN_LE_PHY_OPT_CODED_S2,
        .pref_tx_phy = BT_GAP_LE_PHY_CODED,
        .pref_rx_phy = BT_GAP_LE_PHY_CODED,
    };

    err = bt_conn_le_phy_update(conn, &param);
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

    // start scanning for other profiles
    scan_start(true);
  } else {
    // we will only do the MTU exchange on the peripheral side
    // central profiles can do the MTU exchange themselves
    // this it to prevent buggy implementations running out of RAM
    // when we ask for a large MTU
    err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
    if (err) {
      LOG_ERR("bt_gatt_exchange_mtu() returned %d", err);
    }

    advertising_start();
  }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_WRN("Disconnected: %s, reason 0x%02x %s", addr, reason,
          bt_hci_err_to_str(reason));

  struct bt_conn_info conn_info = {0};
  int err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("bt_conn_info() returned %d", err);
  }

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
    if (conn) {
      bt_conn_unref(conn);
      led_temp_dim_dec();
    }
  } else if (conn_info.role == BT_CONN_ROLE_CENTRAL) {
    bt_conn_unref(conn);
  } else {
    if (atomic_ptr_cas((void **)&nus_conn, (void *)conn, NULL)) {
      shell_bt_nus_disable();
    }
  }
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
                                enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  struct bt_conn_info conn_info = {0};
  bt_conn_get_info(conn, &conn_info);

  if (!err) {
    LOG_INF("Security changed: %s level %u", addr, level);
    if (conn_info.role == BT_CONN_ROLE_PERIPHERAL && level == BT_SECURITY_L4) {
      if (atomic_ptr_cas((void **)&nus_conn, NULL, (void *)conn)) {
        shell_bt_nus_enable(conn);
      }
#if IS_ENABLED(CONFIG_BT_CHANNEL_SOUNDING)
      const struct bt_le_cs_set_default_settings_param default_settings = {
          .enable_initiator_role = false,
          .enable_reflector_role = true,
          .cs_sync_antenna_selection =
              BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
          .max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
      };

      err = bt_le_cs_set_default_settings(conn, &default_settings);
      if (err) {
        LOG_ERR("Failed to configure default CS settings (err %d)", err);
      }
#endif
    }
  } else {
    LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
  }
}

static void on_conn_recycled(void) {
  LOG_INF("Connection recycled");
  advertising_start();

  scan_start(false);
}

static const char *bt_le_phy_to_str(enum bt_gap_le_phy phy) {
  switch (phy) {
  case BT_GAP_LE_PHY_NONE:
    return "none";
  case BT_GAP_LE_PHY_CODED:
    return "coded";
  case BT_GAP_LE_PHY_CODED_S2:
    return "coded_s2";
  case BT_GAP_LE_PHY_CODED_S8:
    return "coded_s8";
  case BT_GAP_LE_PHY_1M:
    return "1M";
  case BT_GAP_LE_PHY_2M:
    return "2M";
  }
  return "unknown";
}

static void on_le_phy_updated(struct bt_conn *conn,
                              struct bt_conn_le_phy_info *info) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("LE PHY updated: %s tx_phy %s rx_phy %s", addr,
          bt_le_phy_to_str(info->tx_phy), bt_le_phy_to_str(info->rx_phy));
}

#if IS_ENABLED(CONFIG_BT_CHANNEL_SOUNDING)
static void on_le_cs_config_complete(struct bt_conn *conn, uint8_t status,
                                     struct bt_conn_le_cs_config *config) {

  const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
      .config_id = 0,
      .max_procedure_len = 2000,
      .min_procedure_interval = 1,
      .max_procedure_interval = 200,
      .max_procedure_count = 0,
      .min_subevent_len = 10000,
      .max_subevent_len = 75000,
      .tone_antenna_config_selection =
          BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
      .phy = BT_LE_CS_PROCEDURE_PHY_1M,
      .tx_power_delta = 0x80,
      .preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
      .snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
      .snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
  };

  int err = bt_le_cs_set_procedure_parameters(conn, &procedure_params);
  if (err) {
    LOG_ERR("Failed to set procedure parameters (err %d)", err);
  }
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = on_security_changed,
    .recycled = on_conn_recycled,
    .le_phy_updated = on_le_phy_updated,
#if IS_ENABLED(CONFIG_BT_CHANNEL_SOUNDING)
    .le_cs_config_complete = on_le_cs_config_complete,
#endif
};

#if IS_ENABLED(CONFIG_ANT)

static void ant_evt_handler(ant_evt_t *p_ant_evt) {

  if (p_ant_evt->channel == bpwr_channel_config.channel_number) {
    ant_bike_power_evt_handler(p_ant_evt, &bike_power);
    return;
  } else if (p_ant_evt->channel == environ_channel_config.channel_number) {
    ant_environ_evt_handler(p_ant_evt, &environ);
    return;
  } else if (p_ant_evt->channel ==
                 antplus_generic_slave0_config.channel_number ||
             p_ant_evt->channel ==
                 antplus_generic_slave1_config.channel_number) {
    ant_generic_slave_evt_handler(p_ant_evt);
    return;
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

  return 0;
}

static int ant_profile_setup(void) {
  int err;

  ant_bike_power_init(&bike_power, central_t800_offset_compensation_start_ant);

  ant_environ_init(&environ);

  err = ant_channel_init(&environ_channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: %d", err);
    return err;
  }

  err = ant_channel_init(&bpwr_channel_config);
  if (err) {
    LOG_ERR("ant_channel_init failed: %d", err);
    return err;
  }

  return 0;
}

#endif

int bt_setup(void) {
  int err;
  err = bt_enable(NULL);
  if (err) {
    if (err == -EALREADY) {
      return 0;
    }
    return err;
  }

  settings_load();

  led_data_activity();

  scan_start(true);

#if IS_ENABLED(CONFIG_ANT)
  err = bt_adv_set_device_number(ant_profiles_get_device_number());
  if (err) {
    LOG_ERR("Failed to set device number (err %d)", err);
    return err;
  }
#endif

  advertising_start();

  return 0;
}

int main_loop(void) {
  int err;
  bool low_batt = false;

#if HAS_VCC_POWEROFF_PIN
  led_on_dt(&vcc_poweroff_pin);
#endif

  LOG_INIT();
  LOG_INF("Reset reason: %d", watchdog_get_reset_reason());

#if IS_ENABLED(CONFIG_USE_IMU_ST_LSM6DSL)
  lsm6dsl_initialized = lsmdsl_wakeup_is_available();
  LOG_INF("LSM6DSL init success: %d", lsm6dsl_initialized);
#endif

  err = battery_gauge_setup();
  if (err) {
    LOG_ERR("Failed to setup battery gauge: %d", err);
  }

  err = dk_buttons_init(btn_handler_fn);
  if (err) {
    LOG_ERR("Buttons init failed (err %d)", err);
    return err;
  }

#if IS_ENABLED(CONFIG_ANT)
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
#endif

#if IS_ENABLED(CONFIG_ENVIRON_TX_TEMP_ST_LSM6DSL)
  if (lsm6dsl_initialized) {
    err = ant_channel_open(environ_channel_config.channel_number);
    if (err) {
      LOG_ERR("Failed to open environ channel: %d", err);
      return err;
    }
  }
#endif // IS_ENABLED(CONFIG_ENVIRON_TX_TEMP_ST_LSM6DSL)

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

  err = bt_setup();
  if (err) {
    LOG_ERR("BT setup failed (err %d)", err);
    return err;
  }

  int64_t no_activity_since_ms = k_uptime_get();

  for (int i = 0;; i++) {
    watchdog_feed();
    uint16_t battery_mv = battery_gauge_get_mv();
    uint8_t battery_level = battery_gauge_get_level(battery_mv);
    low_batt |= battery_level <= LOW_BATTERY_THRESHOLD;
    low_batt &= battery_level <
                LOW_BATTERY_THRESHOLD + LOW_BATTERY_THRESHOLD_HYSTERESIS;

    bas_battery_level_self_set(battery_level, battery_mv);
#if IS_ENABLED(CONFIG_ANT)
    ant_bike_power_set_self_battery_state(&bike_power, battery_level,
                                          battery_mv);
    ant_environ_set_battery_state(&environ, battery_level, battery_mv);
#endif

    led_set_bit(POWER_LED_BIT);
    k_sleep(K_MSEC(75));
    if (low_batt) {
      led_clear_bit(POWER_LED_BIT);
    }
    k_sleep(K_MSEC(75));
    if (low_batt) {
      led_set_bit(POWER_LED_BIT);
    }
    k_sleep(K_MSEC(75));
    led_clear_bit(POWER_LED_BIT);

#if defined(CONFIG_AUTO_POWER_OFF_TIMEOUT) && CONFIG_AUTO_POWER_OFF_TIMEOUT > 0
    uint32_t is_active = is_usb_connected();
    bt_conn_foreach(BT_CONN_TYPE_LE, bt_conn_foreach_count_connected_peripheral,
                    &is_active);
    if (is_active) {
      no_activity_since_ms = k_uptime_get();
    } else {
#if IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)
      if (lsm6dsl_initialized) {
        no_activity_since_ms =
            MAX(no_activity_since_ms, lsm6dsl_get_time_since_last_activity());
      }
#endif // IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)
    }

    if (
#if IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)
        lsm6dsl_initialized &&
#endif // IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)
        (k_uptime_get() - no_activity_since_ms) >
            CONFIG_AUTO_POWER_OFF_TIMEOUT * 1000) {
      LOG_INF("Auto power off triggered");
      enter_poweroff();
      no_activity_since_ms = k_uptime_get();
    }
#endif // defined(CONFIG_AUTO_POWER_OFF_TIMEOUT) &&
       // CONFIG_AUTO_POWER_OFF_TIMEOUT > 0

    k_sleep(K_MSEC(2000 - 75 * 3));

#if IS_ENABLED(CONFIG_ENVIRON_TX_TEMP_ST_LSM6DSL)
    if (lsm6dsl_initialized) {
      const struct device *lsm6dsl_device =
          DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(st_lsm6dsl_wakeup));
      struct sensor_value val;
      err = sensor_sample_fetch(lsm6dsl_device);
      if (err) {
        LOG_ERR("Failed to fetch temperature: %d", err);
      } else {
        err =
            sensor_channel_get(lsm6dsl_device, SENSOR_CHAN_AMBIENT_TEMP, &val);
        if (err) {
          LOG_ERR("Failed to get temperature: %d", err);
        } else {
          int16_t temperature = sensor_value_to_centi(&val);
          ant_environ_temp_set(&environ, temperature);
        }
      }
    }
#endif // IS_ENABLED(CONFIG_ENVIRON_TX_TEMP_ST_LSM6DSL)
  }

  LOG_WRN("Main loop exited");
  return 0;
}

int main(void) {
  int err;
  err = main_loop();
  if (err) {
    printk("Main loop failed (err %d)", err);
    k_panic();
    return err;
  }
  return 0;
}