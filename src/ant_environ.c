#include "ant_environ.h"
#include <stdatomic.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>

#include <ant_profiles/common/pages/ant_common_page_70.h>
#include <ant_profiles/common/pages/ant_common_page_80.h>
#include <ant_profiles/common/pages/ant_common_page_81.h>

#include <zephyr/logging/log.h>

static uint8_t page_ctr = 0;

const static uint32_t seconds_per_period =
    (SEC_PER_DAY / ARRAY_SIZE(((struct ant_environ_s){0}).max_temp_history));

const static uint8_t payload_page_0[] = {0,    0xFF, 0xFF, 0, // 0.5hz
                                         0b11, // page 0 and 1 supported
                                         0,    0,    0};

void ant_environ_init(struct ant_environ_s *p_environ) {
  for (int i = 0; i < ARRAY_SIZE(p_environ->max_temp_history); i++) {
    p_environ->min_temp_history[i] = 0x800;
    p_environ->max_temp_history[i] = -0x800;
  }
  p_environ->temp_valid = false;

  ant_common_page80_data_t page_80 =
      ANT_COMMON_page80(CONFIG_BPWR_TX_HW_VERSION, CONFIG_BPWR_TX_MFG_ID,
                        CONFIG_BPWR_TX_MODEL_NUM);
  ant_common_page81_data_t page_81 = ANT_COMMON_page81(
      CONFIG_BPWR_TX_SW_VERSION, CONFIG_BPWR_TX_SW_VERSION, 0);

  p_environ->cache_page_80[0] = 80;
  ant_common_page_80_encode(p_environ->cache_page_80 + 1, &page_80);
  p_environ->cache_page_81[0] = 81;
  ant_common_page_81_encode(p_environ->cache_page_81 + 1, &page_81);
  p_environ->epoch_seconds = k_uptime_seconds();
}

void ant_environ_temp_set(struct ant_environ_s *p_environ,
                          int16_t temp_c_centi) {
  p_environ->temp_evt_count++;
  int16_t temp_c_deci = temp_c_centi;
  if (temp_c_deci < 0) {
    temp_c_deci -= 5;
  } else {
    temp_c_deci += 5;
  }
  temp_c_deci /= 10;
  uint32_t uptime_seconds = k_uptime_seconds() - p_environ->epoch_seconds;
  uint32_t period_index = (uptime_seconds / seconds_per_period) %
                          ARRAY_SIZE(p_environ->max_temp_history);
  if (period_index != p_environ->last_history_index) {
    p_environ->last_history_index = period_index;
    p_environ->min_temp_history[period_index] = temp_c_deci;
    p_environ->max_temp_history[period_index] = temp_c_deci;
  } else {
    p_environ->max_temp_history[period_index] =
        MAX(p_environ->max_temp_history[period_index], temp_c_deci);
    p_environ->min_temp_history[period_index] =
        MIN(p_environ->min_temp_history[period_index], temp_c_deci);
  }
  p_environ->current_temp = temp_c_centi;
  p_environ->temp_valid = true;
}

static void encode_page82(const struct ant_environ_s *p_environ,
                          uint8_t *output_payload) {
  uint32_t uptime_ticks = k_uptime_seconds() / 16;
  output_payload[0] = 82;
  output_payload[1] = 0xff;
  output_payload[2] = 0xff;
  output_payload[3] = uptime_ticks;
  output_payload[4] = uptime_ticks >> 8;
  output_payload[5] = uptime_ticks >> 16;
  output_payload[6] = 0xff;
  output_payload[7] = 0x0f;
  if (p_environ->battery_voltage_self > 0 &&
      p_environ->battery_voltage_self < 15000) {
    output_payload[6] = (p_environ->battery_voltage_self % 1000) /
                        4; // close enough (1/250V instead of 1/256V)
    output_payload[7] = p_environ->battery_voltage_self / 1000;
  }
  output_payload[7] |= (p_environ->battery_state_self << 4);
}

void ant_environ_set_battery_state(struct ant_environ_s *p_environ,
                                   uint8_t percent, uint16_t voltage_mv) {
  if (percent > 100) {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_INVALID;
  } else if (percent >= 90) {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_NEW;
  } else if (percent >= 50) {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_GOOD;
  } else if (percent >= 25) {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_OK;
  } else if (percent >= 10) {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_LOW;
  } else {
    p_environ->battery_state_self = ANT_BATTERY_STATUS_CRITICAL;
  }
  p_environ->battery_voltage_self = voltage_mv;
}

void ant_environ_evt_handler(ant_evt_t *p_ant_evt,
                             struct ant_environ_s *p_environ) {
  switch (p_ant_evt->event) {
  case EVENT_TX: {
    uint8_t bcst_id = page_ctr++;
    if (p_environ->page_request_ctr > 0) {
      p_environ->page_request_ctr--;
      switch (p_environ->page_request_idx) {
      case 0:
        bcst_id = 0;
        break;
      case 1:
        bcst_id = 1;
        break;
      case 80:
        bcst_id = 2;
        break;
      case 81:
        bcst_id = 3;
        break;
      case 82:
        bcst_id = 4;
        break;
      default:
        // unsupporteed page, cancel
        p_environ->page_request_ctr = 0;
      }
    }
    switch (bcst_id % 5) {
    case 0:
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(payload_page_0),
                               (uint8_t *)payload_page_0);
      break;
    case 1: {
      if (p_environ->temp_valid) {
        uint16_t current_temp = (uint16_t)p_environ->current_temp;
        int16_t min_temp_signed = p_environ->min_temp_history[0];
        int16_t max_temp_signed = p_environ->max_temp_history[0];
        for (int i = 0; i < ARRAY_SIZE(p_environ->max_temp_history); i++) {
          min_temp_signed =
              MIN(min_temp_signed, p_environ->min_temp_history[i]);
          max_temp_signed =
              MAX(max_temp_signed, p_environ->max_temp_history[i]);
        }
        uint16_t min_temp = (uint16_t)min_temp_signed & 0x07FF;
        if (min_temp_signed < 0) {
          min_temp = 0x800 | min_temp;
        }
        uint16_t max_temp = (uint16_t)max_temp_signed & 0x07FF;
        if (max_temp_signed < 0) {
          max_temp = 0x800 | max_temp;
        }
        uint8_t payload[] = {
            1,
            0xFF,
            p_environ->temp_evt_count,
            min_temp,
            (((min_temp >> 8) & 0x0F) << 4) | (max_temp & 0x0F),
            max_temp >> 4,
            current_temp,
            current_temp >> 8,
        };
        ant_broadcast_message_tx(p_ant_evt->channel, sizeof(payload),
                                 (uint8_t *)payload);
      } else {
        ant_broadcast_message_tx(p_ant_evt->channel, sizeof(payload_page_0),
                                 (uint8_t *)payload_page_0);
      }
    } break;
    case 2: {
      ant_broadcast_message_tx(p_ant_evt->channel,
                               sizeof(p_environ->cache_page_80),
                               (uint8_t *)p_environ->cache_page_80);
    } break;
    case 3: {
      ant_broadcast_message_tx(p_ant_evt->channel,
                               sizeof(p_environ->cache_page_81),
                               (uint8_t *)p_environ->cache_page_81);
    } break;
    case 4: {
      uint8_t payload[8];
      encode_page82(p_environ, payload);
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(payload),
                               (uint8_t *)payload);
    } break;
    }
  } break;
  case EVENT_RX: {
    if (p_ant_evt->message.ANT_MESSAGE_ucMesgID == MESG_ACKNOWLEDGED_DATA_ID) {
      if (p_ant_evt->message.ANT_MESSAGE_aucPayload[0] == 70 &&
          p_ant_evt->message.ANT_MESSAGE_aucPayload[7] == 1) {
        uint8_t ctr = p_ant_evt->message.ANT_MESSAGE_aucPayload[5] & (~0x80);
        if (ctr == 0) {
          // invalid per spec, ignore
          return;
        }
        // 10 seconds max @ 0.5Hz, do not honor requests longer than that
        ctr = MIN(ctr, 5);
        p_environ->page_request_ctr = ctr;
        p_environ->page_request_idx =
            p_ant_evt->message.ANT_MESSAGE_aucPayload[6];
      }
    }
  } break;
  }
}