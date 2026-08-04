#include "battery_gauge.h"
#include "zephyr/sys/util.h"

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