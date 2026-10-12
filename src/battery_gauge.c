#include "battery_gauge.h"
#include "zephyr/kernel.h"
#include <stdatomic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(battery_gauge, LOG_LEVEL_ERR);

#define BATTERY_GAUGE_REGULATOR_EXISTS                                         \
  DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(vbat_pwr))

#if BATTERY_GAUGE_REGULATOR_EXISTS
static const struct device *const vbat_reg =
    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(vbat_pwr)),
                DEVICE_DT_GET(DT_NODELABEL(vbat_pwr)), NULL);
#endif

typedef struct {
  uint16_t voltage;
  uint8_t level;
} battery_gauge_table_t;

static const battery_gauge_table_t battery_gauge_table[] = {
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_10, 10},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_20, 20},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_30, 30},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_40, 40},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_50, 50},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_60, 60},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_70, 70},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_80, 80},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_90, 90},
    {CONFIG_BATTERY_GAUGE_VOLTAGE_TABLE_100, 100},
};

static _Atomic int16_t battery_gauge_samples[4] = {0};
static int16_t next_sample = 0;
static int64_t last_calibrated_at_ms = 0;

static const struct adc_dt_spec vbat_spec =
    ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat);

int16_t battery_gauge_get_mv(void) {
  int16_t max = INT16_MIN;
  for (int i = 0; i < ARRAY_SIZE(battery_gauge_samples); i++) {
    int16_t sample = atomic_load(&battery_gauge_samples[i]);
    if (sample > max) {
      max = sample;
    }
  }
  return max;
}

static int32_t gauge_read() {
  int err;
  int16_t sample_buffer;
#if BATTERY_GAUGE_REGULATOR_EXISTS
  err = regulator_enable(vbat_reg);
  if (err < 0) {
    LOG_ERR("Failed to enable regulator: %d", err);
    return err;
  }
#endif

  int64_t now_ms = k_uptime_get();
  struct adc_sequence sequence = {
      .buffer = &sample_buffer,
      .buffer_size = sizeof(sample_buffer),
      .calibrate = !last_calibrated_at_ms ||
                   (now_ms - last_calibrated_at_ms) > 180 * 1000,
  };

  err = adc_sequence_init_dt(&vbat_spec, &sequence);
  if (err < 0) {
    LOG_ERR("Failed to initialize ADC sequence: %d", err);
    return err;
  }

#if BATTERY_GAUGE_REGULATOR_EXISTS
  k_sleep(K_MSEC(5));
#endif

  err = adc_read_dt(&vbat_spec, &sequence);
  if (err < 0) {
    LOG_ERR("Failed to read ADC: %d", err);
    return err;
  }
#if BATTERY_GAUGE_REGULATOR_EXISTS
  err = regulator_disable(vbat_reg);
  if (err < 0) {
    LOG_WRN("Failed to disable regulator: %d", err);
  }
#endif

  int32_t tmp = sample_buffer
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), vbat_divider_ratio)
                * DT_PROP(DT_PATH(zephyr_user), vbat_divider_ratio)
#endif
      ;
  err = adc_raw_to_millivolts_dt(&vbat_spec, &tmp);
  if (err < 0) {
    LOG_ERR("Failed to convert raw to mV: %d", err);
    return err;
  }

  if (sequence.calibrate) {
    last_calibrated_at_ms = now_ms;
  }

  return tmp;
}

struct k_work_delayable battery_gauge_upkeep_work;

static void battery_gauge_upkeep(struct k_work *work) {
  (void)work;
  int32_t res = gauge_read();
  if (res < 0) {
    LOG_ERR("Failed to read ADC: %d", res);
    return;
  }

  atomic_store(&battery_gauge_samples[(next_sample++) %
                                      ARRAY_SIZE(battery_gauge_samples)],
               res);
  k_work_schedule(&battery_gauge_upkeep_work, K_SECONDS(2));
}

K_WORK_DELAYABLE_DEFINE(battery_gauge_upkeep_work, battery_gauge_upkeep);

int battery_gauge_setup(void) {
  int err;

#if BATTERY_GAUGE_REGULATOR_EXISTS
  err = regulator_disable(vbat_reg);
  if (err < 0) {
    LOG_ERR("Failed to disable regulator: %d", err);
    return err;
  }
#endif

  if (!adc_is_ready_dt(&vbat_spec)) {
    LOG_ERR("ADC not ready");
    return -ENOTSUP;
  }

  err = adc_channel_setup_dt(&vbat_spec);
  if (err < 0) {
    LOG_ERR("Failed to setup ADC channel: %d", err);
    return err;
  }

  int32_t first_sample = gauge_read();
  if (first_sample < 0) {
    LOG_ERR("Failed to read ADC: %d", first_sample);
    return first_sample;
  }
  atomic_store(&battery_gauge_samples[0], first_sample);
  next_sample = 1;

  k_work_schedule(&battery_gauge_upkeep_work, K_SECONDS(1));
  return 0;
}

void battery_gauge_teardown(void) {
  struct k_work_sync sync;
  k_work_cancel_delayable_sync(&battery_gauge_upkeep_work, &sync);
}

uint8_t battery_gauge_get_level(uint16_t voltage_mv) {
  if (voltage_mv < 2900) {
    return 5; // vddh cutoff at 2700
  }

  voltage_mv += CONFIG_BATTERY_CELL_COUNT / 2;
  voltage_mv /= CONFIG_BATTERY_CELL_COUNT;

  uint8_t level = 5;

  for (int i = 0; i < ARRAY_SIZE(battery_gauge_table); i++) {
    if (voltage_mv >= battery_gauge_table[i].voltage) {
      level = battery_gauge_table[i].level;
    }
  }

  return level;
}