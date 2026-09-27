#include "advertiser.h"
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(advertiser, LOG_LEVEL_INF);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_CPS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

#define BT_DEVICE_NAME_LEN (sizeof(CONFIG_BT_DEVICE_NAME) - 1)
#define MAX_LEGACY_ADV_NAME_LEN 29

// this assignment is asserted in the SDK to fit
static uint8_t sd_device_name[CONFIG_BT_DEVICE_NAME_MAX + 1] =
    CONFIG_BT_DEVICE_NAME;

// we will leave this requirement for now because otherwise we might need to
// do UTF-8 parsing and it's too annoying
BUILD_ASSERT(BT_DEVICE_NAME_LEN <= MAX_LEGACY_ADV_NAME_LEN,
             "Device name is too long for legacy advertising");

static struct bt_data sd[] = {{.type = BT_DATA_NAME_COMPLETE,
                               .data = sd_device_name,
                               .data_len = BT_DEVICE_NAME_LEN}};

static void bt_conn_foreach_count_central(struct bt_conn *conn, void *data) {
  struct bt_conn_info conn_info;
  uint32_t *count = (uint32_t *)data;

  int err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("Failed to get connection info (err %d)", err);
    return;
  }
  if (conn_info.type != BT_CONN_TYPE_LE ||
      conn_info.state != BT_CONN_STATE_CONNECTED ||
      conn_info.role != BT_CONN_ROLE_PERIPHERAL) {
    return;
  }
  (*count)++;
}

static void adv_work_handler(struct k_work *work) {
  int err = bt_le_adv_start(
      BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_MS_TO_ADV_INTERVAL(800),
                      BT_GAP_MS_TO_ADV_INTERVAL(1200), NULL),
      ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

  if (err && err != -EALREADY) {
    LOG_ERR("Advertising failed to start (err %d)", err);
    return;
  }

  LOG_INF("Advertising successfully started");
}

K_WORK_DEFINE(adv_work, adv_work_handler);

void advertising_start(void) {
  uint32_t count = 0;
  bt_conn_foreach(BT_CONN_TYPE_LE, bt_conn_foreach_count_central, &count);
  if (count < CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT) {
    k_work_submit(&adv_work);
  }
}

int bt_adv_set_device_number(uint32_t device_number) {
  char new_device_name[ARRAY_SIZE(sd_device_name)] = {0};
  int len = snprintf(new_device_name, ARRAY_SIZE(sd_device_name), "%s-%" PRIu32,
                     CONFIG_BT_DEVICE_NAME, device_number);
  if (len < 0) {
    LOG_ERR("Failed to format new name (err %d)", len);
    return -EINVAL;
  }
  new_device_name[CONFIG_BT_DEVICE_NAME_MAX] =
      '\0'; // force truncate to maximum size
  if (0 == strcmp(new_device_name, sd_device_name)) {
    return 0;
  }
  int err = bt_set_name(new_device_name);
  if (err == 0) {
    bt_le_adv_stop();
    memcpy(sd_device_name, new_device_name, sizeof(sd_device_name));
    if (len > MAX_LEGACY_ADV_NAME_LEN) {
      sd[0].data_len = MAX_LEGACY_ADV_NAME_LEN;
      sd[0].type = BT_DATA_NAME_SHORTENED;
    } else {
      sd[0].data_len = len;
      sd[0].type = BT_DATA_NAME_COMPLETE;
    }
    advertising_start();
  }
  if (err) {
    LOG_ERR("Failed to set BT name (err %d)", err);
    return err;
  }
  return 0;
}
