#include "gatt_callbacks.h"
#include "ant_profiles.h"
#include "zephyr/bluetooth/conn.h"
#include "zephyr/bluetooth/hci_types.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gatt_callbacks, CONFIG_BT_GATT_LOG_LEVEL);

void mtu_exchange_func(struct bt_conn *conn, uint8_t att_err,
                       struct bt_gatt_exchange_params *params) {
  LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");
  if (!att_err) {
    uint16_t payload_mtu =
        bt_gatt_get_mtu(conn) - 3; // 3 bytes used for Attribute headers.
    LOG_INF("New MTU: %d bytes", payload_mtu);
  }
}

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

ssize_t sensor_location_read_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr, void *buf,
                                uint16_t len, uint16_t offset) {
  if (offset == 0 && len > 0) {
    *((uint8_t *)buf) = 6;
    return 1;
  }
  return BT_ATT_ERR_INVALID_OFFSET;
}

ssize_t cps_cpf_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset) {
  const uint8_t data[] = {0b1, // Pedal Power Balance Supported
                          0, 0, 0};
  return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}
