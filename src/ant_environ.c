#include "ant_environ.h"

#include <ant_profiles/common/pages/ant_common_page_70.h>
#include <ant_profiles/common/pages/ant_common_page_80.h>
#include <ant_profiles/common/pages/ant_common_page_81.h>

static uint8_t page_ctr = 0;

const uint8_t payload_page_0[] = {0,    0xFF, 0xFF, 1, // 4hz
                                  0b11,                // page 0 and 1 supported
                                  0,    0,    0};

void ant_environ_init(struct ant_environ_s *p_environ) {
  p_environ->min_temp = 0x800;
  p_environ->max_temp = -0x800;
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
}

void ant_environ_temp_set(struct ant_environ_s *p_environ, int8_t temp_c) {
  p_environ->temp_evt_count++;
  p_environ->min_temp = MIN(p_environ->min_temp, temp_c * 10);
  p_environ->max_temp = MAX(p_environ->max_temp, temp_c * 10);
  p_environ->current_temp = temp_c * 100;
  p_environ->temp_valid = true;
}

void ant_environ_evt_handler(ant_evt_t *p_ant_evt,
                             struct ant_environ_s *p_environ) {
  switch (p_ant_evt->event) {
  case EVENT_TX: {
    switch (page_ctr++ % 4) {
    case 0:
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(payload_page_0),
                               (uint8_t *)payload_page_0);
      break;
    case 1: {
      if (p_environ->temp_valid) {
        uint8_t payload[] = {
            1,
            0xFF,
            p_environ->temp_evt_count,
            p_environ->min_temp,
            ((p_environ->min_temp >> 8) & 0x0F) | (p_environ->max_temp << 8),
            p_environ->max_temp >> 8,
            p_environ->current_temp,
            p_environ->current_temp >> 8,
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
    }
  } break;
  }
}