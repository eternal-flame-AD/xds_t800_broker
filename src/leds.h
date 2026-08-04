#pragma once

#include <zephyr/kernel.h>

#define POWER_LED_BIT 0
#define CENTRAL1_CON_STATUS_LED_BIT 1
#define CENTRAL2_CONN_STATUS_LED_BIT 2
#define DATA_ACTIVITY_LED_BIT 3

void led_brightness_next(void);

void led_data_activity(void);

void led_set_bit(uint8_t bit);
void led_clear_bit(uint8_t bit);