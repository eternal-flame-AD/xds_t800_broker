#include <sys/errno.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "ant_profiles.h"
#include <ant_init.h>
#include <ant_interface.h>
#include <ant_parameters.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(shell_ant_slave, LOG_LEVEL_INF);

static int antplus_generic_slave_cmd_handler(const struct shell *sh,
                                             size_t argc, char **argv,
                                             void *data) {
  shell_print(sh, "ANT+ Generic Slave");

  struct channel_name_t {
    const ant_channel_config_t *config;
    const char *name;
  };
  struct channel_name_t channel_names[] = {
      {&antplus_generic_slave_config, "Generic Slave"},
      {&antplus_wakeup_slave_config[0], "Wakeup Slave 0"},
      {&antplus_wakeup_slave_config[1], "Wakeup Slave 1"},
  };

  for (size_t i = 0; i < ARRAY_SIZE(channel_names); i++) {
    uint8_t status;
    shell_print(sh, "Contigured Device Number: %d",
                channel_names[i].config->device_number);
    shell_print(sh, "Contigured Device Type: %d",
                channel_names[i].config->device_type);
    shell_print(sh, "Contigured Transmit Type: %d",
                channel_names[i].config->transmission_type);
    shell_print(sh, "Contigured Channel Period: %d",
                channel_names[i].config->channel_period);
    shell_print(sh, "Contigured Network Number: %d",
                channel_names[i].config->network_number);
    shell_print(sh, "Contigured Channel Number: %d",
                channel_names[i].config->channel_number);
    shell_print(sh, "Contigured Channel Type: %d",
                channel_names[i].config->channel_type);
    shell_print(sh, "Contigured Ext Assign: %d",
                channel_names[i].config->ext_assign);
    if (ant_channel_status_get(channel_names[i].config->channel_number,
                               &status) != 0) {
      shell_error(sh, "Failed to get channel status");
      return -EINVAL;
    }
    status &= STATUS_CHANNEL_STATE_MASK;
    switch (status) {
    case STATUS_UNASSIGNED_CHANNEL:
      shell_print(sh, "Channel is unassigned");
      break;
    case STATUS_ASSIGNED_CHANNEL:
      shell_print(sh, "Channel is assigned");
      break;
    case STATUS_SEARCHING_CHANNEL:
      shell_print(sh, "Channel is searching");
      break;
    case STATUS_TRACKING_CHANNEL:
      shell_print(sh, "Channel is tracking");
      uint16_t device_number;
      uint8_t device_type;
      uint8_t transmit_type;
      ant_channel_id_get(channel_names[i].config->channel_number,
                         &device_number, &device_type, &transmit_type);
      shell_print(sh, "Device number: %d",
                  device_number | ((transmit_type << 12) & 0xFF0000));
      shell_print(sh, "Device type: %d", device_type);
      shell_print(sh, "Transmit type: %d", transmit_type & 0x0F);
      break;
    }
  }
  return 0;
}

static int antplus_wakeup_slave_cmd_set_handler(const struct shell *sh,
                                                size_t argc, char **argv,
                                                void *data) {
  uint8_t status;
  uint16_t device_number;
  uint8_t device_type;
  uint8_t transmit_type;
  uint8_t slot = 0;
  if (argc > 2) {
    shell_error(sh, "Usage: %s <slot>", argv[0]);
    return -ENOEXEC;
  }
  if (argc == 2) {
    slot = strtoul(argv[1], NULL, 10);
    if (errno != 0 || slot >= ANT_WAKEUP_CHANNEL_SLOT_COUNT) {
      shell_error(sh, "Invalid slot: %s", argv[1]);
      return -EINVAL;
    }
  }
  ant_channel_id_get(antplus_generic_slave_config.channel_number,
                     &device_number, &device_type, &transmit_type);
  if (ant_channel_status_get(antplus_generic_slave_config.channel_number,
                             &status) != 0) {
    shell_error(sh, "Failed to get channel status");
    return -EINVAL;
  }
  if ((status & STATUS_CHANNEL_STATE_MASK) != STATUS_TRACKING_CHANNEL) {
    shell_error(sh, "Channel is not tracking, connect to sensor first");
    return -EINVAL;
  }
  if (!device_number || !device_type || !transmit_type) {
    shell_error(sh, "Device metadata is not correct");
    return -EINVAL;
  }

  antplus_wakeup_slave_config[slot].transmission_type = transmit_type;
  antplus_wakeup_slave_config[slot].device_type = device_type;
  antplus_wakeup_slave_config[slot].device_number = device_number;
  antplus_wakeup_slave_config[slot].channel_period =
      antplus_generic_slave_config.channel_period;
  char settings_name[] =
      SETTINGS_ANT_SUBTREE "/" SETTINGS_ANT_WAKEUP_SEGMENT "/0";
  settings_name[strlen(settings_name) - 1] = '0' + slot;

  if (settings_save_one(settings_name, &antplus_wakeup_slave_config[slot],
                        sizeof(antplus_wakeup_slave_config[slot])) != 0) {
    shell_error(sh, "Failed to save wakeup ANT+ Slave");
    return -EINVAL;
  }
  shell_print(sh, "Wakeup ANT+ Slave config slot %d set to %d %d %d", slot,
              device_number, device_type, transmit_type);

  return 0;
}

static int antplus_wakeup_slave_cmd_clear_handler(const struct shell *sh,
                                                  size_t argc, char **argv,
                                                  void *data) {
  int err;
  uint8_t slot_begin = 0;
  uint8_t slot_end = ANT_WAKEUP_CHANNEL_SLOT_COUNT;
  if (argc > 2) {
    shell_error(sh, "Usage: %s <slot>", argv[0]);
    return -ENOEXEC;
  }
  if (argc == 2) {
    slot_begin = strtoul(argv[1], NULL, 10);
    if (errno != 0 || slot_begin >= ANT_WAKEUP_CHANNEL_SLOT_COUNT) {
      shell_error(sh, "Invalid slot: %s", argv[1]);
      return -EINVAL;
    }
    slot_end = slot_begin + 1;
  }
  char settings_name[] =
      SETTINGS_ANT_SUBTREE "/" SETTINGS_ANT_WAKEUP_SEGMENT "/0";
  for (size_t i = slot_begin; i < slot_end; i++) {
    settings_name[strlen(settings_name) - 1] = '0' + i;
    err = settings_delete(settings_name);
    if (err != 0) {
      shell_error(sh, "Failed to clear wakeup ANT+ Slave config: %d", err);
      continue;
    }
    antplus_wakeup_slave_config[i].device_number = 0;
    antplus_wakeup_slave_config[i].device_type = 0;
    antplus_wakeup_slave_config[i].channel_period = 0;
  }
  shell_print(sh, "Wakeup ANT+ Slave config cleared");
  return 0;
}

static int antplus_generic_slave_cmd_start_handler(const struct shell *sh,
                                                   size_t argc, char **argv,
                                                   void *data) {
  shell_print(sh, "Starting ANT+ Generic Slave");
  if (argc < 3 || argc > 4) {
    shell_error(sh,
                "Usage: %s <device_type> <channel_period> [<device_number>]",
                argv[0]);
    return -ENOEXEC;
  }
  uint8_t channel_status;
  if (ant_channel_status_get(antplus_generic_slave_config.channel_number,
                             &channel_status) != 0) {
    shell_error(sh, "Failed to get channel status");
    return -EINVAL;
  }
  if ((channel_status & STATUS_CHANNEL_STATE_MASK) !=
      STATUS_UNASSIGNED_CHANNEL) {
    shell_error(sh, "Channel is not unassigned");
    return -EINVAL;
  }

  antplus_generic_slave_config.device_type = strtoul(argv[1], NULL, 10);
  if (errno != 0) {
    shell_error(sh, "Invalid device type: %s", argv[1]);
    return -EINVAL;
  }
  antplus_generic_slave_config.channel_period = strtoul(argv[2], NULL, 10);
  if (errno != 0) {
    shell_error(sh, "Invalid channel period: %s", argv[2]);
    return -EINVAL;
  }
  antplus_generic_slave_config.device_number = 0;
  if (argc == 4) {
    antplus_generic_slave_config.device_number = strtoul(argv[3], NULL, 10);
    if (errno != 0) {
      shell_error(sh, "Invalid device number: %s", argv[3]);
      return -EINVAL;
    }
  }
  if (ant_channel_init(&antplus_generic_slave_config) != 0) {
    shell_error(sh, "Failed to initialize ANT+ Generic Slave");
    return -EINVAL;
  }
  if (ant_channel_low_priority_rx_search_timeout_set(
          antplus_generic_slave_config.channel_number, 0) != 0) {
    shell_error(sh, "Failed to set search timeout");
    return -EINVAL;
  }
  if (ant_channel_open(antplus_generic_slave_config.channel_number) != 0) {
    shell_error(sh, "Failed to open ANT+ Generic Slave");
    return -EINVAL;
  }
  return 0;
}

static int antplus_generic_slave_cmd_stop_handler(const struct shell *sh,
                                                  size_t argc, char **argv,
                                                  void *data) {
  int err;
  shell_print(sh, "Stopping ANT+ Generic Slave");
  err = ant_channel_close(antplus_generic_slave_config.channel_number);
  if (err != 0 && err != NRF_ANT_ERROR_CHANNEL_IN_WRONG_STATE) {
    shell_error(sh, "Failed to close ANT+ Generic Slave: %d", err);
    return -EINVAL;
  }
  uint8_t status = 0;
  for (int timeout = 0; timeout < 50; timeout++) {
    err = ant_channel_status_get(antplus_generic_slave_config.channel_number,
                                 &status);
    if (err != 0) {
      shell_error(sh, "Failed to get channel status: %d", err);
      return -EINVAL;
    }
    if ((status & STATUS_CHANNEL_STATE_MASK) == STATUS_ASSIGNED_CHANNEL) {
      break;
    }
    k_sleep(K_MSEC(100));
  }
  if ((status & STATUS_CHANNEL_STATE_MASK) != STATUS_ASSIGNED_CHANNEL) {
    shell_error(sh, "Failed to wait for channel to be disconnected");
    return -EINVAL;
  }
  if (ant_channel_unassign(antplus_generic_slave_config.channel_number) != 0) {
    shell_error(sh, "Failed to unassign ANT+ Generic Slave");
    return -EINVAL;
  }
  return 0;
}

void ant_generic_slave_evt_handler(ant_evt_t *p_ant_evt) {
  switch (p_ant_evt->event) {
  case EVENT_RX:
    LOG_INF("RX: %02X %02X %02X %02X %02X %02X %02X %02X",
            p_ant_evt->message.ANT_MESSAGE_aucPayload[0],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[1],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[2],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[3],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[4],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[5],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[6],
            p_ant_evt->message.ANT_MESSAGE_aucPayload[7]);
    break;
  case EVENT_RX_FAIL:
    LOG_WRN("Missed RX frame");
    break;
  case EVENT_RX_SEARCH_TIMEOUT:
  case EVENT_RX_FAIL_GO_TO_SEARCH:
    LOG_WRN("RX search timeout");
    break;
  }
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    ant_slave_cmds,
    SHELL_CMD(start, NULL, "Start ANT+ Slave",
              antplus_generic_slave_cmd_start_handler),
    SHELL_CMD(stop, NULL, "Stop ANT+ Slave",
              antplus_generic_slave_cmd_stop_handler),
    SHELL_CMD(set_wakeup, NULL, "Set wakeup ANT+ Slave",
              antplus_wakeup_slave_cmd_set_handler),
    SHELL_CMD(clear_wakeup, NULL, "Clear wakeup ANT+ Slave",
              antplus_wakeup_slave_cmd_clear_handler),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ant_slave, &ant_slave_cmds, "ANT+ Slave",
                   antplus_generic_slave_cmd_handler);