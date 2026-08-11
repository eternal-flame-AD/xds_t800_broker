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

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME)};

static void bt_conn_foreach_count_central(struct bt_conn *conn, void *data) {
  struct bt_conn_info conn_info;
  uint32_t *count = (uint32_t *)data;

  int err = bt_conn_get_info(conn, &conn_info);
  if (err) {
    LOG_ERR("Failed to get connection info (err %d)", err);
    return;
  }
  if (conn_info.type != BT_CONN_TYPE_LE ||
      conn_info.state != BT_CONN_STATE_DISCONNECTED ||
      conn_info.role != BT_CONN_ROLE_PERIPHERAL) {
    return;
  }
  (*count)++;
}

static void adv_work_handler(struct k_work *work) {
  int err = bt_le_adv_start(
      BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_MS_TO_ADV_INTERVAL(400),
                      BT_GAP_MS_TO_ADV_INTERVAL(600), NULL),
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