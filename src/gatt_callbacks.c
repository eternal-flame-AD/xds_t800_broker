#include "gatt_callbacks.h"
#include "zephyr/bluetooth/conn.h"
#include "zephyr/bluetooth/hci_types.h"
#include "zephyr/bluetooth/uuid.h"
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
  char uuid_str[BT_UUID_STR_LEN];
  bt_uuid_to_str((attr - 1)->uuid, uuid_str, sizeof(uuid_str));
  LOG_INF("notify CCC write: %d %s", value, uuid_str);
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
