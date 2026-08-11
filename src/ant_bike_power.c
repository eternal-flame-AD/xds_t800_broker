#include "ant_bike_power.h"

#include "ant_profiles/common/pages/ant_common_page_80.h"
#include "ant_profiles/common/pages/ant_common_page_81.h"
#include "zephyr/bluetooth/gatt.h"
#include <string.h>
#include <sys/errno.h>
#include <zephyr/kernel.h>

static uint8_t page_ctr = 0;

#define BATT_IDX_REMOTE (0)
#define BATT_IDX_SELF (1)
#define BATT_IDX_LOWEST (2)
#define BATT_IDX_INVALID (0xFFu)

static void calibration_unsupported_stub(struct ant_bike_power_s *profile) {
  ant_bike_power_calib_response(profile, false, -ENOTSUP);
}

void ant_bike_power_set_serial_number(struct ant_bike_power_s *profile,
                                      uint32_t serial_number) {
  ant_common_page81_data_t page_81 = ANT_COMMON_page81(
      CONFIG_BPWR_TX_SW_VERSION, CONFIG_BPWR_TX_SW_VERSION, serial_number);
  ant_common_page_81_encode(profile->cache_page_81 + 1, &page_81);
}

void ant_bike_power_init(struct ant_bike_power_s *profile,
                         ant_bike_calib_request_cb_t calib_request_cb) {
  memset(profile, 0, sizeof(struct ant_bike_power_s));
  k_mutex_init(&profile->update_mutex);

  ant_common_page80_data_t page_80 =
      ANT_COMMON_page80(CONFIG_BPWR_TX_HW_VERSION, CONFIG_BPWR_TX_MFG_ID,
                        CONFIG_BPWR_TX_MODEL_NUM);
  ant_common_page81_data_t page_81 = ANT_COMMON_page81(
      CONFIG_BPWR_TX_SW_VERSION, CONFIG_BPWR_TX_SW_VERSION, 0);

  profile->cache_page_80[0] = 80;
  ant_common_page_80_encode(profile->cache_page_80 + 1, &page_80);
  profile->cache_page_81[0] = 81;
  ant_common_page_81_encode(profile->cache_page_81 + 1, &page_81);

  profile->battery_request_idx = BATT_IDX_INVALID;
  profile->battery_state = ANT_BATTERY_STATUS_INVALID;
  profile->battery_state_self = ANT_BATTERY_STATUS_NEW;
  profile->calib_request_cb =
      calib_request_cb ? calib_request_cb : calibration_unsupported_stub;
}

void ant_bike_power_set_battery_state(struct ant_bike_power_s *profile,
                                      uint8_t percent) {
  if (percent > 100) {
    profile->battery_state = ANT_BATTERY_STATUS_INVALID;
  } else if (percent >= 90) {
    profile->battery_state = ANT_BATTERY_STATUS_NEW;
  } else if (percent >= 50) {
    profile->battery_state = ANT_BATTERY_STATUS_GOOD;
  } else if (percent >= 25) {
    profile->battery_state = ANT_BATTERY_STATUS_OK;
  } else if (percent >= 10) {
    profile->battery_state = ANT_BATTERY_STATUS_LOW;
  } else {
    profile->battery_state = ANT_BATTERY_STATUS_CRITICAL;
  }
}

void ant_bike_power_set_self_battery_state(struct ant_bike_power_s *profile,
                                           uint8_t percent,
                                           uint16_t voltage_mv) {
  if (percent > 100) {
    profile->battery_state_self = ANT_BATTERY_STATUS_INVALID;
  } else if (percent >= 90) {
    profile->battery_state_self = ANT_BATTERY_STATUS_NEW;
  } else if (percent >= 50) {
    profile->battery_state_self = ANT_BATTERY_STATUS_GOOD;
  } else if (percent >= 25) {
    profile->battery_state_self = ANT_BATTERY_STATUS_OK;
  } else if (percent >= 10) {
    profile->battery_state_self = ANT_BATTERY_STATUS_LOW;
  } else {
    profile->battery_state_self = ANT_BATTERY_STATUS_CRITICAL;
  }

  profile->battery_voltage_self = voltage_mv;
}

void ant_bike_power_update(struct ant_bike_power_s *profile, uint16_t power,
                           uint8_t pwr_distribution_right,
                           uint8_t instantaneous_cadence, uint16_t angle) {
  k_mutex_lock(&profile->update_mutex, K_FOREVER);
  profile->angle = angle;
  profile->instantaneous_power = power;
  profile->accumulated_power += power;
  profile->pwr_distribution_right = pwr_distribution_right;
  profile->instantaneous_cadence = instantaneous_cadence;
  profile->last_update_event_count = profile->update_event_count;
  profile->update_event_count++;
  k_mutex_unlock(&profile->update_mutex);
}

static void encode_page16_cache(struct ant_bike_power_s *profile) {
  profile->cache_page16[0] = 0x10;
  profile->cache_page16[1] = profile->update_event_count;
  profile->cache_page16[2] = 0x80 | profile->pwr_distribution_right;
  profile->cache_page16[3] = profile->instantaneous_cadence;
  profile->cache_page16[4] = profile->accumulated_power & 0xFF;
  profile->cache_page16[5] = profile->accumulated_power >> 8;
  profile->cache_page16[6] = profile->instantaneous_power & 0xFF;
  profile->cache_page16[7] = profile->instantaneous_power >> 8;
}

void ant_bike_power_calib_response(struct ant_bike_power_s *profile,
                                   bool successful, int16_t response) {
  if (profile->calib_state == ANT_BIKE_POWER_CALIB_STATE_CALIBRATING) {
    profile->calibration_response = response;
    profile->calib_state = successful ? ANT_BIKE_POWER_CALIB_STATE_SUCCESSFUL
                                      : ANT_BIKE_POWER_CALIB_STATE_FAILED;
  }
}

static void encode_page82(const struct ant_bike_power_s *profile,
                          uint8_t batt_idx, uint8_t *output_payload) {
  if (batt_idx >= 2) {
    batt_idx = (profile->battery_state_self > profile->battery_state)
                   ? BATT_IDX_SELF
                   : BATT_IDX_REMOTE;
  }
  uint32_t uptime_ticks = k_uptime_seconds() / 16;
  output_payload[0] = 82;
  output_payload[1] = 0xff;
  output_payload[2] = 2; // two batteries
  output_payload[3] = uptime_ticks;
  output_payload[4] = uptime_ticks >> 8;
  output_payload[5] = uptime_ticks >> 16;
  output_payload[6] = 0xff;
  output_payload[7] = 0x0f;
  if (batt_idx == BATT_IDX_SELF) {
    output_payload[2] |= (BATT_IDX_SELF << 4);
    if (profile->battery_voltage_self > 0 &&
        profile->battery_voltage_self < 15000) {
      output_payload[6] = (profile->battery_voltage_self % 1000) /
                          4; // close enough (1/250V instead of 1/256V)
      output_payload[7] = profile->battery_voltage_self / 1000;
    }
    output_payload[7] |= (profile->battery_state_self << 4);
  } else {
    output_payload[2] |= (BATT_IDX_REMOTE << 4);
    output_payload[7] |= (profile->battery_state << 4);
  }
}

void ant_bike_power_evt_handler(ant_evt_t *p_ant_evt,
                                struct ant_bike_power_s *profile) {
  switch (p_ant_evt->event) {
  case EVENT_TX: {

    uint8_t output_payload[8];

    // if we have calibration response, encode and send without delay
    if (profile->calib_state == ANT_BIKE_POWER_CALIB_STATE_SUCCESSFUL ||
        profile->calib_state == ANT_BIKE_POWER_CALIB_STATE_FAILED) {
      output_payload[0] = 0x01;
      output_payload[1] =
          profile->calib_state == ANT_BIKE_POWER_CALIB_STATE_SUCCESSFUL ? 0xac
                                                                        : 0xaf;
      output_payload[2] = 0xff;
      output_payload[3] = 0xff;
      output_payload[4] = 0xff;
      output_payload[5] = 0xff;
      output_payload[6] = profile->calibration_response & 0xff;
      output_payload[7] = profile->calibration_response >> 8;
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(output_payload),
                               (uint8_t *)output_payload);
      profile->calib_state = ANT_BIKE_POWER_CALIB_STATE_READY;
      return;
    }

    // if we have new power data, encode and send without delay

    if (profile->update_event_count != profile->last_update_event_count) {
      if (0 == k_mutex_lock(&profile->update_mutex, K_NO_WAIT)) {
        encode_page16_cache(profile);
        ant_broadcast_message_tx(p_ant_evt->channel,
                                 sizeof(profile->cache_page16),
                                 (uint8_t *)profile->cache_page16);
        profile->last_update_event_count = profile->update_event_count;
        profile->page16_rebroadcast_ctr = 1;
        k_mutex_unlock(&profile->update_mutex);
        return;
      }
    }

    // if we have a battery request
    if (profile->battery_request_idx != BATT_IDX_INVALID) {
      encode_page82(profile, profile->battery_request_idx, output_payload);
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(output_payload),
                               (uint8_t *)output_payload);
      profile->battery_request_idx = BATT_IDX_INVALID;
      return;
    }

    // rebroadcast one frame to ensure reception
    if (profile->page16_rebroadcast_ctr > 0) {
      if (0 == k_mutex_lock(&profile->update_mutex, K_NO_WAIT)) {
        profile->page16_rebroadcast_ctr--;
        ant_broadcast_message_tx(p_ant_evt->channel,
                                 sizeof(profile->cache_page16),
                                 (uint8_t *)profile->cache_page16);
        k_mutex_unlock(&profile->update_mutex);
        return;
      }
    }

    // otherwise, send one of the background pages

    // if battery voltage is valid, send it with 50 probability
    if (profile->battery_state != ANT_BATTERY_STATUS_INVALID && page_ctr % 2) {
      encode_page82(profile, BATT_IDX_LOWEST, output_payload);
      ant_broadcast_message_tx(p_ant_evt->channel, sizeof(output_payload),
                               (uint8_t *)output_payload);
    } else if (page_ctr & 0b10) {
      ant_broadcast_message_tx(p_ant_evt->channel,
                               sizeof(profile->cache_page_80),
                               (uint8_t *)profile->cache_page_80);
    } else {
      ant_broadcast_message_tx(p_ant_evt->channel,
                               sizeof(profile->cache_page_81),
                               (uint8_t *)profile->cache_page_81);
    }
    page_ctr++;

    break;
  case EVENT_RX: {
    if (p_ant_evt->message.ANT_MESSAGE_ucMesgID == MESG_ACKNOWLEDGED_DATA_ID) {
      if (p_ant_evt->message.ANT_MESSAGE_aucPayload[0] == 0x01 &&
          p_ant_evt->message.ANT_MESSAGE_aucPayload[1] == 0xAA &&
          profile->calib_state == ANT_BIKE_POWER_CALIB_STATE_READY) {
        profile->calib_state = ANT_BIKE_POWER_CALIB_STATE_CALIBRATING;
        profile->calib_request_cb(profile);
      }
      if (p_ant_evt->message.ANT_MESSAGE_aucPayload[0] == 70 &&
          p_ant_evt->message.ANT_MESSAGE_aucPayload[6] == 82) {
        uint8_t batt_idx = p_ant_evt->message.ANT_MESSAGE_aucPayload[3];
        if (batt_idx == BATT_IDX_LOWEST) {
          profile->battery_request_idx = BATT_IDX_LOWEST;
        } else if (batt_idx == BATT_IDX_SELF) {
          profile->battery_request_idx = BATT_IDX_SELF;
        } else {
          profile->battery_request_idx = BATT_IDX_INVALID;
        }
      }
    }
  } break;
  default:
    break;
  }
  }
}
