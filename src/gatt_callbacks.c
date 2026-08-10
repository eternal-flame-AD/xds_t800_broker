#include "gatt_callbacks.h"
#include "ant_bike_power.h"
#include "ant_profiles.h"
#include "zephyr/bluetooth/conn.h"
#include "zephyr/bluetooth/hci_types.h"
#include "zephyr/sys/byteorder.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gatt_callbacks, CONFIG_BT_GATT_LOG_LEVEL);

void on_subscribed_check_success(struct bt_conn *conn, uint8_t status,
                                 struct bt_gatt_subscribe_params *params) {
  if (status != 0) {
    LOG_ERR("Subscribed check failed: %d", status);
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }
}

void bt_ccc_write_cb(const struct bt_gatt_attr *attr, const uint16_t value) {
  LOG_DBG("notify CCC write: %d", value);
}

void calibration_write_cb(struct bt_conn *conn, uint8_t err,
                          struct bt_gatt_write_params *params) {
  if (err != 0) {
    LOG_ERR("Calibration write failed: %d", err);
    ant_bike_power_calib_response(&bike_power, false, -3);
  }
}

ssize_t gatt_read_u8_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset) {
  uint8_t val = *(uint8_t *)attr->user_data;
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

ssize_t gatt_read_u32_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset) {
  uint32_t val = sys_cpu_to_le32(*(uint32_t *)attr->user_data);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}
