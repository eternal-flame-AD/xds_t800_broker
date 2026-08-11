#pragma once

#include <zephyr/bluetooth/gatt.h>

void on_subscribed_check_success(struct bt_conn *conn, uint8_t status,
                                 struct bt_gatt_subscribe_params *params);

void bt_ccc_write_cb(const struct bt_gatt_attr *attr, const uint16_t value);

ssize_t gatt_read_u8_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset);
ssize_t gatt_read_u32_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset);
