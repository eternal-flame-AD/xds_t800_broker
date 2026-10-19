#include "lsm6dsl.h"
#include "gatt_callbacks.h"
#include "scanner.h"
#include <stdint.h>

#define DT_DRV_COMPAT st_lsm6dsl_wakeup

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lsm6dsl);

struct lsm6dsl_wakeup_config {
  const struct i2c_dt_spec bus;
  const struct gpio_dt_spec irq_gpio;
};

struct lsm6dsl_temp_data {
  struct k_mutex mutex;
  int16_t temperature_raw;
};

static int64_t time_since_last_activity = 0;
static bool wakeup_is_available = false;

bool lsmdsl_wakeup_is_available(void) { return wakeup_is_available; }

int64_t lsm6dsl_get_time_since_last_activity(void) {
  unsigned int key = irq_lock();
  int64_t result = time_since_last_activity;
  irq_unlock(key);
  return result;
}

static int lsm6dsl_sample_fetch(const struct device *dev,
                                enum sensor_channel chan) {
  if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
    return -ENOTSUP;
  }

  const struct lsm6dsl_wakeup_config *config = dev->config;
  struct lsm6dsl_temp_data *data = dev->data;
  int err;
  uint8_t reg = 0x20;
  k_mutex_lock(&data->mutex, K_FOREVER);
  err =
      i2c_write_read_dt(&config->bus, &reg, sizeof(reg), &data->temperature_raw,
                        sizeof(data->temperature_raw));
  k_mutex_unlock(&data->mutex);
  return err;
}

static int lsm6dsl_channel_get(const struct device *dev,
                               enum sensor_channel chan,
                               struct sensor_value *val) {
  const struct lsm6dsl_temp_data *data = dev->data;
  if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
    return -ENOTSUP;
  }
  int32_t temp = (int32_t)data->temperature_raw + 25 * 256;
  if (temp > 85 * 256 || temp < -40 * 256) {
    return -ERANGE;
  }
  val->val1 = temp / 256;
  val->val2 = (temp % 256) * (1000000 / 256);
  return 0;
}

#if IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)

struct gpio_callback lsm6dsl_int1_callback;

static void on_lsm6dsl_wakeup_work(struct k_work *work) { scan_start(true); }

K_WORK_DEFINE(lsm6dsl_wakeup_work, on_lsm6dsl_wakeup_work);

static void on_lsm6dsl_int1_callback(const struct device *dev,
                                     struct gpio_callback *cb,
                                     unsigned int pins) {
  // handle interrupt
  time_since_last_activity = k_uptime_get();
  k_work_submit(&lsm6dsl_wakeup_work);
}

#endif // IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)

static int lsm6dsl_init(const struct device *dev) {
  const struct lsm6dsl_wakeup_config *config = dev->config;
  struct lsm6dsl_temp_data *data = dev->data;
  k_mutex_init(&data->mutex);
  int err;
  uint8_t value = 0;

  err = i2c_reg_read_byte_dt(&config->bus, 0x0F, &value);
  if (err != 0) {
    return err;
  }
  if (value != 0x6A) {
    return -EINVAL;
  }
  // reset the device
  err = i2c_reg_update_byte_dt(&config->bus, 0x12, BIT(0), BIT(0));
  if (err != 0) {
    return err;
  }
  k_busy_wait(50);
  // load default trim
  err = i2c_reg_update_byte_dt(&config->bus, 0x12, BIT(7), BIT(7));
  if (err != 0) {
    return err;
  }
  k_sleep(K_MSEC(15));
  // set BDU bit to 1
  err = i2c_reg_update_byte_dt(&config->bus, 0x12, BIT(6), BIT(6));
  if (err != 0) {
    return err;
  }
  err = i2c_reg_write_byte_dt(&config->bus, 0x01, 0x80);
  if (err != 0) {
    return err;
  }
  // set significant motion detection threshold to 2
  err = i2c_reg_write_byte_dt(&config->bus, 0x13, 2);
  if (err != 0) {
    return err;
  }
  err = i2c_reg_update_byte_dt(&config->bus, 0x14, BIT_MASK(3), 3);
  if (err != 0) {
    return err;
  }
  err = i2c_reg_write_byte_dt(&config->bus, 0x01, 0x00);
  if (err != 0) {
    return err;
  }
  // enable gyroscope and accelerometer FIFO
  err = i2c_reg_write_byte_dt(&config->bus, 0x08, 0b00001001);
  if (err != 0) {
    return err;
  }
  // disable high performance mode
  err = i2c_reg_update_byte_dt(&config->bus, 0x15, BIT(4), BIT(4));
  if (err != 0) {
    return err;
  }
  // setup accelerometer (26Hz if needed for wakeup, off if not needed)
  err = i2c_reg_write_byte_dt(&config->bus, 0x10,
                              CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER ? 0x20
                                                                          : 0);
  if (err != 0) {
    return err;
  }
  // power down gyroscope
  err = i2c_reg_write_byte_dt(&config->bus, 0x11, 0);
  if (err != 0) {
    return err;
  }

  // significant motion detection on INT1
  err = i2c_reg_write_byte_dt(&config->bus, 0x0d, BIT(6));
  if (err != 0) {
    return err;
  }

#if IS_ENABLED(CONFIG_AUTO_POWER_OFF_WAKE_BY_ACCELEROMETER)

  // enable significant motion detection and tilt detection
  err = i2c_reg_write_byte_dt(&config->bus, 0x19, BIT(0) | BIT(2) | BIT(3));
  if (err != 0) {
    return err;
  }

  // configure INT1
  err = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
  if (err != 0) {
    return err;
  }

  gpio_init_callback(&lsm6dsl_int1_callback, on_lsm6dsl_int1_callback,
                     BIT(config->irq_gpio.pin));
  if (gpio_add_callback(config->irq_gpio.port, &lsm6dsl_int1_callback) < 0) {
    return err;
  }

  // start listening for interrupts on INT1
  err = gpio_pin_interrupt_configure_dt(
      &config->irq_gpio, GPIO_INT_LEVEL_ACTIVE | GPIO_INT_WAKEUP);
  if (err != 0) {
    return err;
  }

  // turn on interrupts
  err = i2c_reg_write_byte_dt(&config->bus, 0x58, 0b11100000);
  if (err != 0) {
    return err;
  }
  err = i2c_reg_write_byte_dt(&config->bus, 0x5e, BIT(1));
  if (err != 0) {
    return err;
  }
#endif
  wakeup_is_available = true;

  return 0;
}

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#warning "LSM6DSL wakeup driver enabled without any devices"
#endif

static DEVICE_API(sensor, lsm6dsl_driver_api) = {
    .sample_fetch = lsm6dsl_sample_fetch,
    .channel_get = lsm6dsl_channel_get,
};

#define LSM6DSL_WAKEUP_DEFINE(inst)                                            \
  static const struct lsm6dsl_wakeup_config lsm6dsl_wakeup_config_##inst = {   \
      .bus = I2C_DT_SPEC_INST_GET(inst),                                       \
      .irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                      \
  };                                                                           \
  static struct lsm6dsl_temp_data lsm6dsl_temp_data_##inst = {0};              \
  SENSOR_DEVICE_DT_INST_DEFINE(                                                \
      inst, lsm6dsl_init, NULL, &lsm6dsl_temp_data_##inst,                     \
      &lsm6dsl_wakeup_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
      &lsm6dsl_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LSM6DSL_WAKEUP_DEFINE);
