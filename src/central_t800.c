#include "central_t800.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "ant_bike_power.h"
#include "ant_interface.h"
#include "ant_profiles.h"
#include "crank_sim.h"
#include "gatt_battery.h"
#include "gatt_callbacks.h"
#include "leds.h"
#include "zephyr/bluetooth/gatt.h"
#include "zephyr/kernel.h"

#define STUCK_SENSOR_TIMER_INTERVAL_MS 500
#define STUCK_SENSOR_REBOOT_TIMEOUT_MS 15000

LOG_MODULE_REGISTER(central_t800, LOG_LEVEL_INF);

#define BT_UUID_DATA_PASSTHRU                                                  \
  BT_UUID_DECLARE_128(                                                         \
      BT_UUID_128_ENCODE(0xc78f0000, 0xb6c7, 0x4ceb, 0xbcfe, 0xcba497c197e6))

BT_GATT_SERVICE_DEFINE(
    data_passthru_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_DATA_PASSTHRU),
    BT_GATT_CHARACTERISTIC(BT_UUID_GATT_CPS_CPM, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_LOCATION, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static const uint8_t sensor_location = 6;
static const uint32_t cycling_power_feature =
    BIT(0) | BIT(3) | BIT(9) |
    BIT(20); //  Pedal Power Balance Supported, Cranks Revolution
             //  Supported, Offset Compensation Supported
             //  Not for use in a distributed system

static ssize_t cpcp_write_cb(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags);

BT_GATT_SERVICE_DEFINE(
    cps_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_CPS),
    BT_GATT_CHARACTERISTIC(BT_UUID_GATT_CPS_CPM, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_GATT_CPS_CPF, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_u32_cb, NULL,
                           (void *)&cycling_power_feature),
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_LOCATION, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_u8_cb, NULL,
                           (void *)&sensor_location),
    BT_GATT_CHARACTERISTIC(BT_UUID_GATT_CPS_CPCP,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                           BT_GATT_PERM_WRITE, NULL, cpcp_write_cb, NULL),
    BT_GATT_CCC(bt_ccc_write_cb, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static _Atomic uint8_t calibration_result_pending = 0;

static int64_t last_data_ms = 0;

static const uint8_t calibration_cmd[] = {0x02, 0x16, 0xaa, 0x10};

static uint16_t last_power = UINT16_MAX;

static struct bt_conn *central_conn = NULL;

static struct crank_sim_s crank_sim = {0};

void cps_stuck_sensor_timer_handler(struct k_timer *timer_id) {
  struct bt_conn *conn = (struct bt_conn *)k_timer_user_data_get(timer_id);
  if (conn) {
    int64_t now = k_uptime_get();
    int64_t elapsed = now - last_data_ms;
    LOG_DBG("[Watchdog] data staleness: %lld ms", elapsed);
    if (elapsed > STUCK_SENSOR_REBOOT_TIMEOUT_MS) {
      LOG_ERR("[Watchdog] data staleness exceeded timeout, rebooting");
      sys_reboot(SYS_REBOOT_COLD);
    }
  }
}

K_TIMER_DEFINE(cps_stuck_sensor_timer, cps_stuck_sensor_timer_handler, NULL);

static uint8_t read_internals_cb(struct bt_conn *conn, uint8_t err,
                                 struct bt_gatt_read_params *params,
                                 const void *data, uint16_t length);

static void calibration_write_cb(struct bt_conn *conn, uint8_t err,
                                 struct bt_gatt_write_params *params);

static uint8_t read_dis_serial_number_cb(struct bt_conn *conn, uint8_t err,
                                         struct bt_gatt_read_params *params,
                                         const void *data, uint16_t length) {
  if (err != 0) {
    LOG_WRN("Read DIS serial number failed: %d", err);
    return BT_GATT_ITER_STOP;
  }
  uint8_t *incoming = (uint8_t *)data;
  // walk backwards and collect up to 8 numbers
  char number_buffer[8] = {0};
  uint8_t number_count = 0;
  for (int i = length - 1; i >= 0; i--) {
    if (incoming[i] >= '0' && incoming[i] <= '9') {
      number_buffer[number_count++] = incoming[i];
    }
    if (number_count >= ARRAY_SIZE(number_buffer)) {
      break;
    }
  }

  if (number_count == 0) {
    LOG_WRN("No serial number found");
    return BT_GATT_ITER_STOP;
  }

  // interpret as decimal
  uint32_t serial_number = 0;
  for (int i = number_count - 1; i >= 0; i--) {
    serial_number = serial_number * 10 + (number_buffer[i] - '0');
  }
  LOG_INF("DIS serial number: %d", serial_number);
  ant_bike_power_set_serial_number(&bike_power, serial_number);
  return BT_GATT_ITER_STOP;
}

static struct bt_gatt_read_params dis_read_params = {
    .handle_count = 1,
    .single = {0},
    .func = read_dis_serial_number_cb,
};

static struct bt_gatt_read_params internals_read_params = {
    .handle_count = 1,
    .single = {0},
    .func = read_internals_cb,
};

static struct bt_gatt_write_params calibration_write_params = {
    .data = calibration_cmd,
    .length = sizeof(calibration_cmd),
    .func = calibration_write_cb,
    .handle = 0,
    .offset = 0,
};

static uint8_t cpcp_data_buf[1 + 1 + 1 + 2] = {0};

static struct bt_conn *offset_compensation_conn = NULL;

static void
cpcp_indicate_params_destroy(struct bt_gatt_indicate_params *params) {
  memset(cpcp_data_buf, 0, sizeof(cpcp_data_buf));
}

static struct bt_gatt_indicate_params cpcp_indicate_params = {
    .uuid = BT_UUID_GATT_CPS_CPCP,
    .attr = cps_service.attrs,
    .destroy = cpcp_indicate_params_destroy,
    .data = cpcp_data_buf,
    .len = sizeof(cpcp_data_buf)};

static ssize_t cpcp_write_cb(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags) {
  if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
    return 0;
  }
  if (!len || offset != 0) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }
  uint8_t opcode = *(uint8_t *)buf;
  cpcp_data_buf[0] = 0x20;
  cpcp_data_buf[1] = opcode;

  if (opcode == 0x0c) {
    int err = -EALREADY;
    if (atomic_ptr_cas((void **)&offset_compensation_conn, NULL, (void *)conn))
      err = central_t800_offset_compensation_start();
    if (err < 0) {
      cpcp_data_buf[2] = 0x04;
      cpcp_indicate_params.len = 3;
      if (bt_gatt_indicate(conn, &cpcp_indicate_params) != 0) {
        cpcp_indicate_params_destroy(&cpcp_indicate_params);
        atomic_ptr_clear((void **)&offset_compensation_conn);
      }
    }
  } else {
    cpcp_data_buf[2] = 0x02;
    cpcp_indicate_params.len = 3;
    if (bt_gatt_indicate(conn, &cpcp_indicate_params) != 0) {
      cpcp_indicate_params_destroy(&cpcp_indicate_params);
    }
  }
  return len;
}

static void calibration_write_cb(struct bt_conn *conn, uint8_t err,
                                 struct bt_gatt_write_params *params) {
  if (err != 0) {
    LOG_ERR("Calibration write failed: %d", err);
    ant_bike_power_calib_response(&bike_power, false, -3);
    if (cpcp_data_buf[0] == 0x20) {
      cpcp_data_buf[2] = 0x04;
      cpcp_indicate_params.len = 3;
      struct bt_conn *peripheral_conn =
          atomic_ptr_clear((void **)&offset_compensation_conn);
      if (peripheral_conn &&
          bt_gatt_indicate(peripheral_conn, &cpcp_indicate_params) != 0) {
        cpcp_indicate_params_destroy(&cpcp_indicate_params);
      }
    }
  }
  LOG_INF("Calibration write successful");
}

static struct bt_gatt_subscribe_params cpm_sub_params = {0};
static struct bt_gatt_subscribe_params sc_cp_sub_params = {0};
static struct bt_gatt_subscribe_params battery_level_sub_params = {0};

int central_t800_offset_compensation_start(void) {
  if (!central_conn || !calibration_write_params.handle) {
    LOG_ERR("No central connection or calibration SC handle");
    return -EINVAL;
  }
  if (bt_gatt_write(central_conn, &calibration_write_params) != 0) {
    LOG_ERR("Calibration write failed");
    return -EINVAL;
  }
  return 0;
}

void central_t800_offset_compensation_start_ant(
    struct ant_bike_power_s *profile) {
  int res = central_t800_offset_compensation_start();
  if (res < 0) {
    ant_bike_power_calib_response(profile, false, res);
  }
}

static uint8_t read_internals_cb(struct bt_conn *conn, uint8_t err,
                                 struct bt_gatt_read_params *params,
                                 const void *data, uint16_t length) {
  if (err != 0) {
    LOG_ERR("Read internals after calib failed: %d", err);
    return BT_GATT_ITER_STOP;
  } else {
    bt_gatt_notify_uuid(NULL, BT_UUID_SENSOR_LOCATION,
                        data_passthru_service.attrs, data, length);
  }

  int16_t internal_calibration_data = 0;
  int8_t temp = INT8_MAX;
  uint16_t weight = UINT16_MAX;

  if (err == 0 && length >= 5) {
    // report zero offset
    uint8_t *incoming = (uint8_t *)data;
    temp = incoming[0];
    internal_calibration_data = incoming[1] | (incoming[2] << 8);
    weight = incoming[3] | (incoming[4] << 8);
    char sign = '+';
    if (weight & 0x8000) {
      sign = '-';
      weight = ~weight + 1;
    }
    ant_environ_temp_set(&environ, temp);

    LOG_INF("temp=%d, adj=%d, force=%c%d.%dkgF", temp,
            internal_calibration_data, sign, weight / 10, weight % 10);
  }
  if (atomic_fetch_and(&calibration_result_pending, 0) == 1) {
    ant_bike_power_calib_response(&bike_power, true, internal_calibration_data);
    if (cpcp_data_buf[0] == 0x20) {
      cpcp_data_buf[2] = 0x01;
      cpcp_data_buf[3] = internal_calibration_data & 0xFF;
      cpcp_data_buf[4] = internal_calibration_data >> 8;
      cpcp_indicate_params.len = 5;
      struct bt_conn *peripheral_conn =
          atomic_ptr_clear((void **)&offset_compensation_conn);
      if (peripheral_conn) {
        int err = bt_gatt_indicate(peripheral_conn, &cpcp_indicate_params);
        if (err != 0) {
          LOG_ERR("Failed to indicate calibration result: %d", err);
          cpcp_indicate_params_destroy(&cpcp_indicate_params);
        }
      }
    }
  }

  return BT_GATT_ITER_STOP;
}

static uint8_t
bt_gatt_notify_func_bas_battery_level(struct bt_conn *conn,
                                      struct bt_gatt_subscribe_params *params,
                                      const void *data, uint16_t length) {
  if (!data) {
    LOG_ERR("BAS battery level notify: ended");
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    return BT_GATT_ITER_STOP;
  }
  LOG_DBG("BAS battery level notify: (%d bytes)", length);

  if (length >= 1) {
    uint8_t *incoming = (uint8_t *)data;
    uint8_t new_battery_level = incoming[0];
    if (new_battery_level > 0 && new_battery_level <= 100) {
      ant_bike_power_set_battery_state(&bike_power, new_battery_level);
      bas_battery_level_set(new_battery_level);
      LOG_INF("Battery level: %d", new_battery_level);
    }
  }
  return BT_GATT_ITER_CONTINUE;
}

static uint8_t
bt_gatt_notify_func_sc_cp(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length) {
  if (!data) {
    LOG_ERR("SC CP notify: ended");
    return BT_GATT_ITER_STOP;
  }

  uint8_t *incoming = (uint8_t *)data;

  if (length >= 4 && incoming[0] == 0x02 && incoming[1] == 0x16 &&
      incoming[2] == 0xab) {
    uint8_t status = incoming[3];

    if (status == 0x00) {
      LOG_INF("SC CP calibration successful");
      if (internals_read_params.single.handle) {
        atomic_store(&calibration_result_pending, 1);
        if (0 == bt_gatt_read(conn, &internals_read_params)) {
          // successfully read internals, defer result notification in read
          // callback
          return BT_GATT_ITER_CONTINUE;
        }
      }
      ant_bike_power_calib_response(&bike_power, true, 0);
      if (cpcp_data_buf[0] == 0x20) {
        cpcp_data_buf[2] = 0x01;
        cpcp_data_buf[3] = 0xff;
        cpcp_data_buf[4] = 0xff;
        cpcp_indicate_params.len = 5;
        struct bt_conn *peripheral_conn =
            atomic_ptr_clear((void **)&offset_compensation_conn);
        if (peripheral_conn &&
            bt_gatt_indicate(peripheral_conn, &cpcp_indicate_params) != 0) {
          cpcp_indicate_params_destroy(&cpcp_indicate_params);
        }
      }
    } else {
      LOG_ERR("SC CP calibration failed (%d)", status);
      ant_bike_power_calib_response(&bike_power, false, status);
      if (cpcp_data_buf[0] == 0x20) {
        cpcp_data_buf[2] = 0x04;
        cpcp_indicate_params.len = 3;
        struct bt_conn *peripheral_conn =
            atomic_ptr_clear((void **)&offset_compensation_conn);
        if (peripheral_conn &&
            bt_gatt_indicate(peripheral_conn, &cpcp_indicate_params) != 0) {
          cpcp_indicate_params_destroy(&cpcp_indicate_params);
        }
      }
    }
  }

  return BT_GATT_ITER_CONTINUE;
}

static uint8_t
bt_gatt_notify_func_cps_cpm(struct bt_conn *conn,
                            struct bt_gatt_subscribe_params *params,
                            const void *data, uint16_t length) {
  if (!data) {
    LOG_ERR("CPS CPM notify: ended");
    return BT_GATT_ITER_STOP;
  } else {
    bt_gatt_notify_uuid(NULL, BT_UUID_GATT_CPS_CPM, data_passthru_service.attrs,
                        data, length);
  }

  uint8_t *incoming = (uint8_t *)data;

  if (length >= 11) {
    int64_t now_ticks = k_uptime_ticks();
    last_data_ms = k_uptime_get();
    uint8_t out[9] = {0};
    out[0] = BIT(0) | BIT(1) | BIT(5); // power balance present, reference left,
                                       // cranks revolution supported
    out[1] = 0;
    memcpy(out + 2, incoming, 2);
    uint16_t totPower = incoming[0] | (incoming[1] << 8); // total power (watts)
    uint16_t leftPower = incoming[2] | (incoming[3] << 8); // left power (watts)
    uint16_t rightPower =
        incoming[4] | (incoming[5] << 8);               // right power (watts)
    int16_t cadence = incoming[6] | (incoming[7] << 8); // cadence (rpm)
    uint16_t angle =
        incoming[8] |
        (incoming[9]
         << 8); // crank angle (degrees, 0 = ready position right forward)
    uint8_t error = incoming[10];
    if (error != 0) {
      LOG_ERR("CPS CPM notify: packet contains error code: %d", error);
      return BT_GATT_ITER_CONTINUE;
    }
    LOG_INF("%dW(%d+%d), %drpm, angle: %ddeg", totPower, leftPower, rightPower,
            cadence, angle);
    if (totPower > 0) {
      uint32_t balance = ((uint32_t)leftPower * 200 + totPower / 2) / totPower;
      out[4] = CLAMP(balance, 0, 200);
    } else {
      out[4] = 100;
    }

    if (CONFIG_BPWR_CLIP_RISING_DELTA > 0 && totPower > last_power) {
      uint16_t delta = totPower - last_power;
      if (delta > CONFIG_BPWR_CLIP_RISING_DELTA) {
        LOG_WRN("Clipping rising delta: %d -> %d", totPower,
                last_power + CONFIG_BPWR_CLIP_RISING_DELTA);
        totPower = last_power + CONFIG_BPWR_CLIP_RISING_DELTA;
      }
    }
    last_power = totPower;

    out[2] = totPower & 0xFF;
    out[3] = (totPower >> 8) & 0xFF;

    ant_bike_power_update(&bike_power, totPower, (200 - out[4]) / 2,
                          cadence < 0 ? 0 : cadence, angle);

    if (cadence > 0) {
      crank_sim_update(&crank_sim, cadence, now_ticks);
    }

    struct bt_crank_revolution_s bt_data;
    crank_sim_get_bt_data(&crank_sim, &bt_data);
    out[5] = bt_data.crank_revolution & 0xFF;
    out[6] = (bt_data.crank_revolution >> 8) & 0xFF;
    out[7] = bt_data.last_crank_time & 0xFF;
    out[8] = (bt_data.last_crank_time >> 8) & 0xFF;

    bt_gatt_notify_uuid(NULL, BT_UUID_GATT_CPS_CPM, cps_service.attrs, out,
                        sizeof(out));
    led_data_activity();

    if (internals_read_params.single.handle) {
      bt_gatt_read(conn, &internals_read_params);
    }
  }
  return BT_GATT_ITER_CONTINUE;
}

static int on_discovery(struct bt_gatt_dm *dm) {
  LOG_INF("on_discovery");
  int err;

  struct bt_gatt_service_val *service_val =
      bt_gatt_dm_attr_service_val(bt_gatt_dm_service_get(dm));

  struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

  if (0 == bt_uuid_cmp(service_val->uuid, BT_UUID_DIS)) {
    LOG_INF("DIS service found");

    const struct bt_gatt_dm_attr *attr_dis_serial_number =
        bt_gatt_dm_char_by_uuid(dm, BT_UUID_DIS_SERIAL_NUMBER);
    if (!attr_dis_serial_number) {
      LOG_WRN("DIS serial number characteristic not found");
    } else {
      dis_read_params.single.handle =
          bt_gatt_dm_attr_chrc_val(attr_dis_serial_number)->value_handle;
      err = bt_gatt_read(conn, &dis_read_params);
      if (err) {
        LOG_WRN("Failed to read DIS serial number: %d", err);
      }
    }

  } else if (0 == bt_uuid_cmp(service_val->uuid, BT_UUID_MESH_PROXY)) {

    const struct bt_gatt_dm_attr *attr_cpm =
        bt_gatt_dm_char_by_uuid(dm, BT_UUID_GATT_CPS_CPM);
    if (!attr_cpm) {
      LOG_ERR("CPM characteristic not found");
      bt_gatt_dm_data_release(dm);
      bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return -EINVAL;
    }

    const struct bt_gatt_dm_attr *attr_com_ccc =
        bt_gatt_dm_desc_by_uuid(dm, attr_cpm, BT_UUID_GATT_CCC);
    if (!attr_com_ccc) {
      LOG_ERR("COM CCC descriptor not found");
      bt_gatt_dm_data_release(dm);
      bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return -EINVAL;
    }

    LOG_INF("Subscribing to CPM characteristic: %d, COM CCC descriptor: %d",
            attr_cpm->handle, attr_com_ccc->handle);

    last_power = UINT16_MAX; // first sample is not clipped
    cpm_sub_params.ccc_handle = attr_com_ccc->handle;
    cpm_sub_params.value_handle =
        bt_gatt_dm_attr_chrc_val(attr_cpm)->value_handle;
    cpm_sub_params.notify = bt_gatt_notify_func_cps_cpm;
    cpm_sub_params.subscribe = on_subscribed_check_success;
    cpm_sub_params.value = BT_GATT_CCC_NOTIFY;
    err = bt_gatt_subscribe(conn, &cpm_sub_params);
    if (err && err != -EALREADY) {
      LOG_ERR("Failed to subscribe to characteristic (err %d)", err);
      bt_gatt_dm_data_release(dm);
      bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return -EINVAL;
    }

    const struct bt_gatt_dm_attr *attr_internals =
        bt_gatt_dm_char_by_uuid(dm, BT_UUID_SENSOR_LOCATION);
    if (attr_internals) {
      internals_read_params.single.handle =
          bt_gatt_dm_attr_chrc_val(attr_internals)->value_handle;
    }

    const struct bt_gatt_dm_attr *attr_com_sc_cp =
        bt_gatt_dm_char_by_uuid(dm, BT_UUID_SC_CONTROL_POINT);

    if (attr_com_sc_cp) {
      const struct bt_gatt_dm_attr *attr_com_sc_cp_ccc =
          bt_gatt_dm_desc_by_uuid(dm, attr_com_sc_cp, BT_UUID_GATT_CCC);

      if (attr_com_sc_cp_ccc) {
        sc_cp_sub_params.ccc_handle = attr_com_sc_cp_ccc->handle;
        sc_cp_sub_params.value_handle =
            bt_gatt_dm_attr_chrc_val(attr_com_sc_cp)->value_handle;
        sc_cp_sub_params.notify = bt_gatt_notify_func_sc_cp;
        sc_cp_sub_params.subscribe = on_subscribed_check_success;
        sc_cp_sub_params.value = BT_GATT_CCC_INDICATE;
        err = bt_gatt_subscribe(conn, &sc_cp_sub_params);
        if (err && err != -EALREADY) {
          LOG_ERR("Failed to subscribe to characteristic (err %d)", err);
        } else {
          calibration_write_params.handle =
              bt_gatt_dm_attr_chrc_val(attr_com_sc_cp)->value_handle;
        }
      }
    }
  } else if (0 == bt_uuid_cmp(service_val->uuid, BT_UUID_BAS)) {
    LOG_INF("BAS service found");

    // the read implementation on the T800 is broken, only notify is reliable
    // unfortunately even the notify uses nonstandard values, so we can't use
    // the BAS client library
    const struct bt_gatt_dm_attr *attr_battery_level =
        bt_gatt_dm_char_by_uuid(dm, BT_UUID_BAS_BATTERY_LEVEL);
    if (!attr_battery_level) {
      LOG_ERR("Battery level characteristic not found");
      bt_gatt_dm_data_release(dm);
      return -EINVAL;
    }

    const struct bt_gatt_dm_attr *attr_battery_level_ccc =
        bt_gatt_dm_desc_by_uuid(dm, attr_battery_level, BT_UUID_GATT_CCC);
    if (!attr_battery_level_ccc) {
      LOG_ERR("Battery level CCC descriptor not found");
      bt_gatt_dm_data_release(dm);
      return -EINVAL;
    }

    battery_level_sub_params.ccc_handle = attr_battery_level_ccc->handle;
    battery_level_sub_params.value_handle =
        bt_gatt_dm_attr_chrc_val(attr_battery_level)->value_handle;
    battery_level_sub_params.notify = bt_gatt_notify_func_bas_battery_level;
    battery_level_sub_params.value = BT_GATT_CCC_NOTIFY;

    err = bt_gatt_subscribe(conn, &battery_level_sub_params);
    if (err && err != -EALREADY) {
      LOG_ERR("Failed to subscribe to characteristic (err %d)", err);
    }
  }

  return 0;
}

static void on_connected(struct bt_conn *conn) {
  int err;
  LOG_INF("on_connected");
  err = ant_channel_open(bpwr_channel_config.channel_number);
  if (err) {
    LOG_ERR("Failed to open main channel: %d", err);
  }
  err = ant_channel_open(bpwr_environ_channel_config.channel_number);
  if (err) {
    LOG_ERR("Failed to open environ channel: %d", err);
  }
  k_timer_user_data_set(&cps_stuck_sensor_timer, conn);
  last_data_ms = k_uptime_get();
  central_conn = conn;
  k_timer_start(&cps_stuck_sensor_timer, K_MSEC(STUCK_SENSOR_TIMER_INTERVAL_MS),
                K_MSEC(STUCK_SENSOR_TIMER_INTERVAL_MS));
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
  central_conn = NULL;
  LOG_INF("on_disconnected, reason: %d", reason);
  ant_channel_close(bpwr_channel_config.channel_number);
  ant_channel_close(bpwr_environ_channel_config.channel_number);
  k_timer_stop(&cps_stuck_sensor_timer);
  k_timer_user_data_set(&cps_stuck_sensor_timer, NULL);
  if (atomic_fetch_and(&calibration_result_pending, 0) == 1) {
    ant_bike_power_calib_response(&bike_power, true, 0);
    if (cpcp_data_buf[0] == 0x20) {
      cpcp_data_buf[2] = 0x01;
      cpcp_data_buf[3] = 0xff;
      cpcp_data_buf[4] = 0xff;
      cpcp_indicate_params.len = 5;
      struct bt_conn *peripheral_conn =
          atomic_ptr_clear((void **)&offset_compensation_conn);
      if (peripheral_conn &&
          bt_gatt_indicate(peripheral_conn, &cpcp_indicate_params) != 0) {
        cpcp_indicate_params_destroy(&cpcp_indicate_params);
      }
    }
  }
}

const struct central_profile central_t800_profile = {
    .name = "T800",
    .device_name_prefix = "XDS-A001-",

    .on_discovery = on_discovery,
    .on_connected = on_connected,
    .on_disconnected = on_disconnected,

    .service_uuids = {BT_UUID_DIS, BT_UUID_MESH_PROXY, BT_UUID_BAS, NULL},
};