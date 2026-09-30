#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/gap.h>

#define CONN_CREATE_PARAMS                                                     \
  BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE,                                 \
                          (2 * BT_GAP_SCAN_FAST_INTERVAL),                     \
                          BT_GAP_SCAN_FAST_WINDOW)

void scan_start(bool high_power);
uint8_t scan_enter_pairing_mode(char *key);