#include "ant_profiles.h"
#include "ant_bike_power.h"
#include <ant_channel_config.h>
#include <stdlib.h>

#undef RESET_POR
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ant_profiles, LOG_LEVEL_INF);

struct ant_bike_power_s bike_power;
struct ant_environ_s environ;

ant_channel_config_t bpwr_channel_config = {
    .channel_number = 0,
    .channel_type = 0x10,
    .ext_assign = 0,
    .rf_freq = 57,
    .transmission_type = 5,
    .device_type = 11,
    .device_number =
        CONFIG_BPWR_TX_FALLBACK_DEVICE_NUMBER, // to be filled by hardware id
    .channel_period = 8182,
    .network_number = ANT_NETWORK_ANTPLUS,
};

ant_channel_config_t bpwr_environ_channel_config = {
    .channel_number = 1,
    .channel_type = 0x10,
    .ext_assign = 0,
    .rf_freq = 57,
    .transmission_type = 5,
    .device_type = 25,
    .device_number =
        CONFIG_BPWR_TX_FALLBACK_DEVICE_NUMBER, // to be filled by hardware id
    .channel_period = 8192,
    .network_number = ANT_NETWORK_ANTPLUS,
};

ant_channel_config_t antplus_wakeup_slave_config[2] = {
    {
        .channel_number = 2,
        .channel_type = 0,
        .ext_assign = 0,
        .rf_freq = 57,
        .transmission_type = 0,
        .device_type = 0, // to be filled
        .device_number = 0,
        .channel_period = 0, // to be filled
        .network_number = ANT_NETWORK_ANTPLUS,
    },
    {
        .channel_number = 3,
        .channel_type = 0,
        .ext_assign = 0,
        .rf_freq = 57,
        .transmission_type = 0,
        .device_type = 0, // to be filled
        .device_number = 0,
        .channel_period = 0, // to be filled
        .network_number = ANT_NETWORK_ANTPLUS,
    },
};

ant_channel_config_t antplus_generic_slave_config = {
    .channel_number = 4,
    .channel_type = 0,
    .ext_assign = 0,
    .rf_freq = 57,
    .transmission_type = 0,
    .device_type = 0, // to be filled
    .device_number = 0,
    .channel_period = 0, // to be filled
    .network_number = ANT_NETWORK_ANTPLUS,
};

uint32_t ant_profiles_create_device_number(uint32_t seed) {
  uint32_t device_number = seed % 65535 + 1;
  seed /= 65535;
  device_number |= (seed & 0x0F) << 16;
  return device_number;
}

static int ant_profiles_init_device_number(void) {
  uint32_t hwid_int = 0;
  if (hwinfo_get_device_id((uint8_t *)(&hwid_int), sizeof(hwid_int)) >= 0) {
    hwid_int = sys_be32_to_cpu(hwid_int);
    uint32_t device_number = ant_profiles_create_device_number(hwid_int);
    ant_profiles_set_device_number(device_number, false);
  }
  return 0;
}

int ant_profiles_set_device_number(uint32_t device_number, bool persist) {
  int err;
  if (device_number == 0 && persist) {
    ant_profiles_init_device_number();
    err = settings_delete(SETTINGS_ANT_SUBTREE
                          "/" SETTINGS_ANT_DEVICE_NUMBER_SEGMENT);
    if (err != 0) {
      return err;
    }
    return 0;
  }

  uint16_t device_number_low = device_number;
  if (!device_number_low)
    return -EINVAL;
  uint16_t device_number_high = device_number >> 16;
  if (device_number_high & (~0x0f))
    return -EINVAL;

  if (persist) {
    uint32_t device_number_be = sys_cpu_to_be32(device_number);
    err = settings_save_one(SETTINGS_ANT_SUBTREE
                            "/" SETTINGS_ANT_DEVICE_NUMBER_SEGMENT,
                            &device_number_be, sizeof(device_number_be));
    if (err != 0) {
      return err;
    }
  }

  ant_channel_config_t *configs[] = {&bpwr_channel_config,
                                     &bpwr_environ_channel_config};
  for (size_t i = 0; i < ARRAY_SIZE(configs); i++) {
    configs[i]->transmission_type &= 0x0F;
    configs[i]->transmission_type |= device_number_high << 4;
    configs[i]->device_number = device_number_low;
  }
  return 0;
}

static int ant_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  int rc;

  if (settings_name_steq(name, SETTINGS_ANT_WAKEUP_SEGMENT, &next) && next) {
    unsigned long slot = strtoul(next, NULL, 10);
    if (errno != 0 || slot >= ANT_WAKEUP_CHANNEL_SLOT_COUNT) {
      return -ENOENT;
    }
    ant_channel_config_t tmp;
    if (len != sizeof(tmp)) {
      return -EINVAL;
    }

    rc = read_cb(cb_arg, &tmp, sizeof(tmp));
    if (rc >= 0) {
      tmp.channel_number = antplus_wakeup_slave_config[slot].channel_number;
      memcpy(&antplus_wakeup_slave_config[slot], &tmp, sizeof(tmp));
      LOG_INF("Wakeup ANT+ Slave slot %lu config set to %d %d %d", slot,
              antplus_wakeup_slave_config[slot].device_number,
              antplus_wakeup_slave_config[slot].device_type,
              antplus_wakeup_slave_config[slot].channel_period);
      return 0;
    }

    return rc;
  }

  if (settings_name_steq(name, SETTINGS_ANT_DEVICE_NUMBER_SEGMENT, &next) &&
      !next) {
    uint32_t device_number;
    if (len != sizeof(device_number)) {
      return -EINVAL;
    }
    rc = read_cb(cb_arg, &device_number, sizeof(device_number));
    if (rc >= 0) {
      device_number = sys_be32_to_cpu(device_number);
      if (0 == ant_profiles_set_device_number(device_number, false)) {
        LOG_INF("Device number set to %d from settings", device_number);
      };
      return 0;
    }
    return rc;
  }

  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(ant, SETTINGS_ANT_SUBTREE, NULL,
                               ant_settings_set, NULL, NULL);

uint32_t ant_profiles_get_device_number(void) {
  uint32_t device_number = bpwr_channel_config.device_number;
  device_number |= (bpwr_channel_config.transmission_type >> 4) << 16;
  return device_number;
}

SYS_INIT(ant_profiles_init_device_number, APPLICATION,
         CONFIG_APPLICATION_INIT_PRIORITY);