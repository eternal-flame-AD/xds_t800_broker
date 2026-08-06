#include "battery_gauge.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(battery_gauge, LOG_LEVEL_ERR);

typedef struct {
  uint16_t voltage;
  uint8_t level;
} battery_gauge_table_t;

const battery_gauge_table_t battery_gauge_table[] = {
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

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));

int16_t battery_gauge_samples[4] = {0};
static int16_t next_sample = 0;
int64_t last_calibrated_at_ms = 0;

static const struct adc_channel_cfg vddh_channel_cfg = {
    .gain = ADC_GAIN_1_4,
    .reference = ADC_REF_INTERNAL, // 0.6V
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 20),
    .channel_id = 0,
    .input_positive = NRF_SAADC_VDDHDIV5,
};

int16_t battery_gauge_get_mv(void) {
  int16_t max = INT16_MIN;
  for (int i = 0; i < ARRAY_SIZE(battery_gauge_samples); i++) {
    int16_t sample = battery_gauge_samples[i];
    if (sample > max) {
      max = sample;
    }
  }
  return max;
}

static int32_t gauge_read() {
  int16_t sample_buffer;
  int64_t now_ms = k_uptime_get();
  struct adc_sequence sequence = {
      .options = NULL,
      .channels = BIT(0),
      .buffer = &sample_buffer,
      .buffer_size = sizeof(sample_buffer),
      .resolution = 14,
      .oversampling = 2,
      .calibrate = !last_calibrated_at_ms ||
                   (now_ms - last_calibrated_at_ms) > 180 * 1000,
  };

  int err = adc_read(adc_dev, &sequence);
  if (err < 0) {
    LOG_ERR("Failed to read ADC: %d", err);
    return err;
  }
  int32_t tmp = sample_buffer;
  err = adc_raw_to_millivolts(5 * CONFIG_BATTERY_GAUGE_ADC_REFERENCE_VOLTAGE,
                              vddh_channel_cfg.gain, sequence.resolution, &tmp);
  if (err < 0) {
    LOG_ERR("Failed to convert raw to mV: %d", err);
    return err;
  }

  if (sequence.calibrate) {
    last_calibrated_at_ms = now_ms;
  }

  return tmp;
}

static void battery_gauge_upkeep_handler(struct k_timer *timer) {

  int32_t res = gauge_read();
  if (res < 0) {
    LOG_ERR("Failed to read ADC: %d", res);
    return;
  }

  battery_gauge_samples[(next_sample++) % ARRAY_SIZE(battery_gauge_samples)] =
      res;
}

K_TIMER_DEFINE(battery_gauge_upkeep_timer, battery_gauge_upkeep_handler, NULL);

int battery_gauge_setup(k_timeout_t interval) {
  if (!device_is_ready(adc_dev)) {
    return -EINVAL;
  }

  int err = adc_channel_setup(adc_dev, &vddh_channel_cfg);
  if (err < 0) {
    LOG_ERR("Failed to setup ADC channel: %d", err);
    return err;
  }

  int32_t first_sample = gauge_read();
  if (first_sample < 0) {
    LOG_ERR("Failed to read ADC: %d", first_sample);
    return first_sample;
  }
  battery_gauge_samples[0] = first_sample;
  next_sample = 1;

  k_timer_start(&battery_gauge_upkeep_timer, interval, interval);

  return 0;
}

int16_t battery_gauge_read(void) {
  int16_t sample_buffer;
  int64_t now_ms = k_uptime_get();
  struct adc_sequence sequence = {
      .options = NULL,
      .channels = BIT(0),
      .buffer = &sample_buffer,
      .buffer_size = sizeof(sample_buffer),
      .resolution = 14,
      .oversampling = 2,
      .calibrate = !last_calibrated_at_ms ||
                   (now_ms - last_calibrated_at_ms) > 180 * 1000,
  };

  int err = adc_read(adc_dev, &sequence);
  if (err < 0) {
    return err;
  }
  int32_t tmp = sample_buffer;
  err = adc_raw_to_millivolts(5 * CONFIG_BATTERY_GAUGE_ADC_REFERENCE_VOLTAGE,
                              vddh_channel_cfg.gain, sequence.resolution, &tmp);
  if (err < 0) {
    return err;
  }
  if (sequence.calibrate) {
    last_calibrated_at_ms = now_ms;
  }

  return tmp;
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