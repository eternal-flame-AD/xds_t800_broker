#include <stdint.h>
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
      {&antplus_generic_slave0_config, "Generic Slave 0"},
      {&antplus_generic_slave1_config, "Generic Slave 1"},
  };

  for (size_t i = 0; i < ARRAY_SIZE(channel_names); i++) {
    uint8_t status;
    shell_print(sh, "Configured Device Number: %d",
                channel_names[i].config->device_number);
    shell_print(sh, "Configured Device Type: %d",
                channel_names[i].config->device_type);
    shell_print(sh, "Configured Transmit Type: %d",
                channel_names[i].config->transmission_type);
    shell_print(sh, "Configured Channel Period: %d",
                channel_names[i].config->channel_period);
    shell_print(sh, "Configured Network Number: %d",
                channel_names[i].config->network_number);
    shell_print(sh, "Configured Channel Number: %d",
                channel_names[i].config->channel_number);
    shell_print(sh, "Configured Channel Type: %d",
                channel_names[i].config->channel_type);
    shell_print(sh, "Configured Ext Assign: %d",
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

static int antplus_generic_slave_cmd_start_handler(const struct shell *sh,
                                                   size_t argc, char **argv,
                                                   void *data) {
  shell_print(sh, "Starting ANT+ Generic Slave");
  uint8_t channel_status;
  ant_channel_config_t *config;
  if (strcmp(argv[1], "0") == 0) {
    config = &antplus_generic_slave0_config;
  } else if (strcmp(argv[1], "1") == 0) {
    config = &antplus_generic_slave1_config;
  } else {
    shell_error(sh, "Invalid channel number: %s", argv[1]);
    return -EINVAL;
  }
  if (ant_channel_status_get(config->channel_number, &channel_status) != 0) {
    shell_error(sh, "Failed to get channel status");
    return -EINVAL;
  }
  if ((channel_status & STATUS_CHANNEL_STATE_MASK) !=
      STATUS_UNASSIGNED_CHANNEL) {
    shell_error(sh, "Channel is not unassigned");
    return -EINVAL;
  }

  config->device_type = strtoul(argv[2], NULL, 10);
  if (errno != 0) {
    shell_error(sh, "Invalid device type: %s", argv[1]);
    return -EINVAL;
  }
  config->channel_period = strtoul(argv[3], NULL, 10);
  if (errno != 0) {
    shell_error(sh, "Invalid channel period: %s", argv[2]);
    return -EINVAL;
  }
  config->device_number = 0;
  if (argc == 5) {
    config->device_number = strtoul(argv[4], NULL, 10);
    if (errno != 0) {
      shell_error(sh, "Invalid device number: %s", argv[3]);
      return -EINVAL;
    }
  }
  if (ant_channel_init(config) != 0) {
    shell_error(sh, "Failed to initialize ANT+ Generic Slave");
    return -EINVAL;
  }
  if (ant_channel_low_priority_rx_search_timeout_set(config->channel_number,
                                                     0) != 0) {
    shell_error(sh, "Failed to set search timeout");
    return -EINVAL;
  }
  if (ant_channel_open(config->channel_number) != 0) {
    shell_error(sh, "Failed to open ANT+ Generic Slave");
    return -EINVAL;
  }
  return 0;
}

static int antplus_generic_slave_cmd_request_handler(const struct shell *sh,

                                                     size_t argc, char **argv,
                                                     void *data) {
  shell_print(sh, "Sending request to ANT+ Generic Slave");
  ant_channel_config_t *config;
  uint8_t request[8];
  uint8_t channel_status;

  if (strcmp(argv[1], "0") == 0) {
    config = &antplus_generic_slave0_config;
  } else if (strcmp(argv[1], "1") == 0) {
    config = &antplus_generic_slave1_config;
  } else {
    shell_error(sh, "Invalid channel number: %s", argv[1]);
    return -EINVAL;
  }

  for (size_t i = 0; i < 8; i++) {
    if (strlen(argv[i + 1]) > 2 && argv[i + 2][0] == '0' &&
        argv[i + 2][1] == 'x') {
      request[i] = strtoul(argv[i + 2] + 2, NULL, 16);
    } else {
      request[i] = strtoul(argv[i + 2], NULL, 10);
    }
    if (errno != 0) {
      shell_error(sh, "Invalid byte: %s", argv[i + 1]);
      continue;
    }
  }
  if (ant_channel_status_get(config->channel_number, &channel_status) != 0) {
    shell_error(sh, "Failed to get channel status");
    return -EINVAL;
  }
  if ((channel_status & STATUS_CHANNEL_STATE_MASK) != STATUS_TRACKING_CHANNEL) {
    shell_error(sh, "Channel is not traking");
    return -EINVAL;
  }
  int err = ant_acknowledge_message_tx(
      antplus_generic_slave0_config.channel_number, sizeof(request), request);
  if (err != 0) {
    shell_error(sh, "Failed to send request: %d", err);
    return -EINVAL;
  }
  return 0;
}

static int antplus_generic_slave_cmd_stop_handler(const struct shell *sh,
                                                  size_t argc, char **argv,
                                                  void *data) {
  int err;
  shell_print(sh, "Stopping ANT+ Generic Slave");
  ant_channel_config_t *config;
  if (strcmp(argv[1], "0") == 0) {
    config = &antplus_generic_slave0_config;
  } else if (strcmp(argv[1], "1") == 0) {
    config = &antplus_generic_slave1_config;
  } else {
    shell_error(sh, "Invalid channel number: %s", argv[1]);
    return -EINVAL;
  }
  err = ant_channel_close(config->channel_number);
  if (err != 0 && err != NRF_ANT_ERROR_CHANNEL_IN_WRONG_STATE) {
    shell_error(sh, "Failed to close ANT+ Generic Slave: %d", err);
    return -EINVAL;
  }
  uint8_t status = 0;
  for (int timeout = 0; timeout < 50; timeout++) {
    err = ant_channel_status_get(config->channel_number, &status);
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
  if (ant_channel_unassign(config->channel_number) != 0) {
    shell_error(sh, "Failed to unassign ANT+ Generic Slave");
    return -EINVAL;
  }
  return 0;
}

void ant_generic_slave_evt_handler(ant_evt_t *p_ant_evt) {
  const char *chan =
      p_ant_evt->channel == antplus_generic_slave0_config.channel_number
          ? "CH0"
          : "CH1";
  switch (p_ant_evt->event) {
  case EVENT_RX:
    LOG_INF("%s RX: %02X %02X %02X %02X %02X %02X %02X %02X", chan,
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
    LOG_WRN("%s Missed RX frame", chan);
    break;
  case EVENT_RX_SEARCH_TIMEOUT:
    LOG_WRN("%s RX search timeout", chan);
    if (ant_channel_open(p_ant_evt->channel) != 0) {
      LOG_ERR("%s Failed to open channel", chan);
    }
    break;
  case EVENT_RX_FAIL_GO_TO_SEARCH:
    LOG_WRN("%s RX failed and going to search", chan);
    break;
  }
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    ant_slave_cmds,
    SHELL_CMD_ARG(
        start, NULL,
        SHELL_HELP("Start ANT+ Slave",
                   "<0|1> <device_type> <channel_period> [<device_number>]"),
        antplus_generic_slave_cmd_start_handler, 4, 1),
    SHELL_CMD_ARG(request, NULL,
                  SHELL_HELP("Send request to ANT+ Slave",
                             "<0|1> <00> <01> <02> <03> <04> <05> <06> <07>"),
                  antplus_generic_slave_cmd_request_handler, 10, 0),
    SHELL_CMD_ARG(stop, NULL, SHELL_HELP("Stop ANT+ Slave", "<0|1>"),
                  antplus_generic_slave_cmd_stop_handler, 2, 0),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ant_slave, &ant_slave_cmds, "ANT+ Slave",
                   antplus_generic_slave_cmd_handler);