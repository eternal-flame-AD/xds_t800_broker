#include "scanner.h"
#include "advertiser.h"
#include "central_profile.h"
#include "leds.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(scanner, CONFIG_LOG_DEFAULT_LEVEL);

struct scan_ad_data {
  bt_addr_le_t peer;
  char name[32];
  const struct bt_uuid *uuids[16];
  uint8_t uuids_count;
};

static _Atomic bool low_power = true;

static bool is_central_slot_open(void) {
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    if (atomic_ptr_get((void **)&instance->conn) == NULL) {
      return true;
    }
  }
  return false;
}

static void scan_enter_led_flash_work_handler(struct k_work *work) {
  for (int i = 0; i < 5; i++) {
    led_data_activity();
    k_sleep(K_MSEC(500));
  }
}

K_WORK_DEFINE(scan_enter_led_flash_work, scan_enter_led_flash_work_handler);

uint8_t scan_enter_pairing_mode(char *key) {
  LOG_INF("Entering pairing mode");
  uint8_t count = central_profile_reset_pairing(key);
  bt_le_scan_stop();
  k_work_submit(&scan_enter_led_flash_work);
  scan_start(true);
  return count;
}

static void scan_go_low_power_work_handler(struct k_work *work) {
  if (!atomic_exchange(&low_power, true)) {
    led_temp_dim_dec();
    low_power = true;
    scan_start(false);
  }
}

K_WORK_DELAYABLE_DEFINE(scan_go_low_power_work, scan_go_low_power_work_handler);

static bool scan_ad_data_callback(struct bt_data *data, void *context) {
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

      STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
        for (const struct bt_uuid *const *target_uuid =
                 instance->profile->service_uuids;
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
      STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
        for (const struct bt_uuid *const *target_uuid =
                 instance->profile->service_uuids;
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

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
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
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    if (bt_addr_le_eq(&instance->known_peer, addr)) {
      LOG_INF("Known peer found: %s", instance->profile->name);

      bt_le_scan_stop();
      int err = bt_conn_le_create(
          addr, CONN_CREATE_PARAMS,
          BT_LE_CONN_PARAM(60, 100, 0, BT_GAP_MS_TO_CONN_TIMEOUT(2500)),
          &instance->conn);

      if (err) {
        LOG_ERR("Failed to create connection (err %d)", err);
      } else {
        led_temp_dim_inc();
      }

      return;
    }
    if (memcmp(&instance->known_peer, BT_ADDR_LE_ANY, sizeof(bt_addr_le_t)) ==
        0) {
      all_known = false;
    }
  }
  // all profiles have known peers, stop interpreting scan data
  if (all_known) {
    return;
  }

  if (rssi < CONFIG_PROXIMITY_PAIR_RSSI) {
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
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    if (instance->conn != NULL) {
      continue;
    }

    if (instance->profile->device_name_prefix != NULL) {
      size_t len = strlen(instance->profile->device_name_prefix);
      if (strlen(scan_ad_data.name) < len) {
        continue;
      }
      if (memcmp(scan_ad_data.name, instance->profile->device_name_prefix,
                 len) != 0) {
        continue;
      }
    }
    bool matches = true;

    for (const struct bt_uuid *const *target_uuid =
             instance->profile->service_uuids;
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
      LOG_INF("Matching profile found: %s", instance->profile->name);

      bt_le_scan_stop();
      int err = bt_conn_le_create(
          addr, CONN_CREATE_PARAMS,
          BT_LE_CONN_PARAM(80, 120, 0, BT_GAP_MS_TO_CONN_TIMEOUT(2000)),
          &instance->conn);

      if (err) {
        LOG_ERR("Failed to create connection (err %d)", err);
      } else {
        led_temp_dim_inc();
      }

      break;
    }
  }
}

static void scan_work_handler(struct k_work *work) {
  int err;

  bool all_known = true;
  bool use_low_power = low_power;

  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    if (memcmp(&instance->known_peer, BT_ADDR_LE_ANY, sizeof(bt_addr_le_t)) ==
        0) {
      all_known = false;
    }
  }

  err = bt_le_scan_start(
      BT_LE_SCAN_PARAM(all_known ? BT_LE_SCAN_TYPE_PASSIVE
                                 : BT_LE_SCAN_TYPE_ACTIVE,
                       BT_LE_SCAN_OPT_NONE,
                       CONN_CREATE_PARAMS->interval * (use_low_power ? 8 : 1),
                       CONN_CREATE_PARAMS->window),
      scan_cb);

  if (err && err != -EALREADY) {
    LOG_ERR("Scanning failed to start (err %d)", err);
  } else {
    if (err == 0) {
      LOG_INF("Scanning started (passive: %d, low_power: %d)", all_known,
              use_low_power);
    }
  }
}

K_WORK_DEFINE(scan_work, scan_work_handler);

void scan_start(bool high_power) {
  if (!is_central_slot_open()) {
    // technically we should release the LED brightness here
    // but when centrals are connected they will hold the LED anyways
    return;
  }

  bt_le_scan_stop();

  if (high_power) {
    if (atomic_exchange(&low_power, false)) {
      led_temp_dim_inc();
      if (CONFIG_LOW_POWER_TIMEOUT > 0) {
        k_work_reschedule(&scan_go_low_power_work,
                          K_SECONDS(CONFIG_LOW_POWER_TIMEOUT));
      }
    }
  }

  k_work_submit(&scan_work);
}
