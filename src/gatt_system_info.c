#include "gatt_system_info.h"

#include "gatt_callbacks.h"
#include "zephyr/bluetooth/uuid.h"
#include "zephyr/devicetree.h"
#include "zephyr/sys/byteorder.h"

#include <debug/cpu_load.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#define BT_UUID_DATA_SYSTEM_INFO                                               \
  BT_UUID_DECLARE_128(                                                         \
      BT_UUID_128_ENCODE(0x4ba20000, 0x8120, 0x462e, 0xb169, 0xcdb66f60500b))

#define BT_UUID_DATA_SYSTEM_INFO_CPU_LOAD                                      \
  BT_UUID_DECLARE_128(                                                         \
      BT_UUID_128_ENCODE(0x4ba20001, 0x8120, 0x462e, 0xb169, 0xcdb66f60500b))

static int32_t temperature = 0;
static uint32_t cpu_load = 0;

const struct device *die_temp_dev = DEVICE_DT_GET(DT_NODELABEL(temp));

BT_GATT_SERVICE_DEFINE(
    system_info_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_DATA_SYSTEM_INFO),
    BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, gatt_read_u32_cb, NULL,
                           &temperature),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_DATA_SYSTEM_INFO_CPU_LOAD,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, gatt_read_u32_cb, NULL,
                           &cpu_load),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

void cpu_load_timer_handler(struct k_timer *timer_id) {
  int load = cpu_load_get();

  if (0 == sensor_sample_fetch(die_temp_dev)) {
    struct sensor_value value;
    sensor_channel_get(die_temp_dev, SENSOR_CHAN_DIE_TEMP, &value);
    temperature = sensor_value_to_centi(&value);
    uint32_t temp_le = sys_cpu_to_le32(temperature);
    bt_gatt_notify_uuid(NULL, BT_UUID_TEMPERATURE, system_info_service.attrs,
                        &temp_le, sizeof(temp_le));
  }

  if (load >= 0) {
    cpu_load = load;
    uint32_t cpu_load_le = sys_cpu_to_le32(cpu_load);
    bt_gatt_notify_uuid(NULL, BT_UUID_DATA_SYSTEM_INFO_CPU_LOAD,
                        system_info_service.attrs, &cpu_load_le,
                        sizeof(cpu_load_le));
  }

  cpu_load_reset();
}

K_TIMER_DEFINE(cpu_load_timer, cpu_load_timer_handler, NULL);

void gatt_sys_info_init(void) {
  cpu_load_reset();
  k_timer_start(&cpu_load_timer, K_MSEC(5000), K_MSEC(5000));
}