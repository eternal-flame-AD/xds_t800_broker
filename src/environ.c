#include "environ.h"
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

#if CONFIG_ANT_ENVIRON_TX
#include "ant_environ.h"
#include "ant_profiles.h"
#endif

#define TEMP_INVALID (INT16_MIN)

static int16_t current_temp = TEMP_INVALID;

static ssize_t gatt_read_temp_cb(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset) {
  int16_t val = current_temp;
  if (val == TEMP_INVALID) {
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  }
  val = sys_cpu_to_le16(val);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &val, sizeof(val));
}

BT_GATT_SERVICE_DEFINE(ess_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE,
                                              BT_GATT_CHRC_READ,
#if IS_ENABLED(CONFIG_ENVIRON_TX_TEMP_AUTHEN)
                                              BT_GATT_PERM_READ_AUTHEN,
#else
                                              BT_GATT_PERM_READ,
#endif
                                              gatt_read_temp_cb, NULL, NULL));

void environ_set_temp(int16_t temp_c_centi) {
#if CONFIG_ANT_ENVIRON_TX
  ant_environ_temp_set(&environ, temp_c_centi);
#endif
  current_temp = temp_c_centi;
}
