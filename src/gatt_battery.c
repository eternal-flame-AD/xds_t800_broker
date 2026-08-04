#include "gatt_battery.h"

#include "gatt_callbacks.h"
#include <zephyr/bluetooth/gatt.h>

static uint8_t battery_level = 50;
static uint8_t battery_level_self = 50;
static uint16_t battery_voltage = 0; // 0.01V

#define BAS_BATTERY_LEVEL_STATUS_LEN 3

static ssize_t
bas_battery_energy_status_read_cb(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr, void *buf,
                                  uint16_t len, uint16_t offset) {
  uint8_t data[3] = {0};
  if (battery_voltage > 0) {
    data[0] = 0b10;
    data[1] = battery_voltage;
    data[2] = (battery_voltage >> 8) | 0xe0;
  }
  return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

static ssize_t bas_battery_level_self_read_cb(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr,
                                              void *buf, uint16_t len,
                                              uint16_t offset) {
  uint8_t *data = (uint8_t *)buf;
  if (offset == 0 && len > 0) {
    data[0] = battery_level_self;
    return 1;
  }
  return BT_ATT_ERR_INVALID_OFFSET;
}

static ssize_t bas_battery_level_read_cb(struct bt_conn *conn,
                                         const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len,
                                         uint16_t offset) {
  uint8_t *data = (uint8_t *)buf;
  if (offset == 0 && len > 0) {
    data[0] = battery_level;
    return 1;
  }
  return BT_ATT_ERR_INVALID_OFFSET;
}

static ssize_t bas_battery_level_status_self_read_cb(
    struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
    uint16_t len, uint16_t offset) {
  const uint8_t data[BAS_BATTERY_LEVEL_STATUS_LEN] = {0, 0b01, 0};
  return bt_gatt_attr_read(conn, attr, buf, len, offset, data, sizeof(data));
}

void bas_battery_level_set(uint8_t level) { battery_level = level; }

void bas_battery_level_self_set(uint8_t level, uint16_t voltage) {
  battery_level_self = level;
  uint16_t res = voltage / 10;
  if (voltage % 10 > 5) {
    res++;
  }
  battery_voltage = res;
}

const struct bt_gatt_cpf cpf_batt_internal = {
    .format = 0x04,
    .description = 0x010f,
    .name_space = 0x01,
    .unit = 0x27ad,
};

const struct bt_gatt_cpf cpf_batt_external = {
    .format = 0x04,
    .description = 0x0110,
    .name_space = 0x01,
    .unit = 0x27ad,
};

BT_GATT_SERVICE_DEFINE(
    bas_service_external, BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, bas_battery_level_read_cb, NULL,
                           NULL),
    BT_GATT_CPF(&cpf_batt_external));

BT_GATT_SERVICE_DEFINE(
    bas_service_self, BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, bas_battery_level_self_read_cb,
                           NULL, NULL),
    BT_GATT_CPF(&cpf_batt_internal),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL_STATUS,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           bas_battery_level_status_self_read_cb, NULL, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_ENERGY_STATUS,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, bas_battery_energy_status_read_cb,
                           NULL, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
