#pragma once

#include <zephyr/kernel.h>

#define NULL_LED_BIT 0
#define POWER_LED_BIT 1
#define CENTRAL1_CONN_STATUS_LED_BIT 2
#define CENTRAL2_CONN_STATUS_LED_BIT 3
#define DATA_ACTIVITY_LED_BIT 4

void led_brightness_next(void);

void led_data_activity(void);

void led_set_bit(uint8_t bit);
void led_clear_bit(uint8_t bit);

// increment reasons to keep LED in standard brightness
void led_temp_dim_inc(void);

// decrement reasons to keep LED in standard brightness
void led_temp_dim_dec(void);
