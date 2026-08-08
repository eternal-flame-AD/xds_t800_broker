#include "poweroff.h"
#include "ant_profiles.h"
#include "leds.h"
#include "watchdog.h"
#include <ant_interface.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(poweroff, LOG_LEVEL_INF);

static bool poweroff_wakeup_requested = false;

K_SEM_DEFINE(poweroff_wakeup_sem, 0, 1);

void poweroff_request_wakeup(void) {
  if (poweroff_wakeup_requested)
    k_sem_give(&poweroff_wakeup_sem);
}

void enter_poweroff(void) {
  int err = 0;
  bool wakeup_success = false;

  if (IS_ENABLED(CONFIG_ANT_WAKEUP)) {

    uint8_t num_wakeup_channels = 0;
    for (size_t i = 0; i < ANT_WAKEUP_CHANNEL_SLOT_COUNT; i++) {
      if (!(antplus_wakeup_slave_config[i].device_number &&
            antplus_wakeup_slave_config[i].device_type &&
            antplus_wakeup_slave_config[i].channel_period))
        continue;
      if (ant_channel_init(&antplus_wakeup_slave_config[i]) != 0) {
        LOG_ERR("Failed to initialize ANT+ Wakeup Slave");
        continue;
      }
      if (ant_channel_low_priority_rx_search_timeout_set(
              antplus_wakeup_slave_config[i].channel_number, 0) != 0) {
        LOG_ERR("Failed to set search timeout (channel %d)",
                antplus_wakeup_slave_config[i].channel_number);
        continue;
      }
      err = ant_channel_open(antplus_wakeup_slave_config[i].channel_number);
      if (err != 0) {
        LOG_ERR("Failed to open ANT+ Slave (%d)", err);
        continue;
      }
      num_wakeup_channels++;
    }

    if (num_wakeup_channels > 0) {
      LOG_INF("Wakeup ANT+ Slave triggered");
      bt_le_adv_stop();
      bt_le_scan_stop();
      bt_disable();

      poweroff_wakeup_requested = true;
      led_clear_bit(POWER_LED_BIT);
      while (1) {
        watchdog_feed();
        for (size_t i = 0; i < ANT_WAKEUP_CHANNEL_SLOT_COUNT; i++) {
          uint8_t status = 0;
          ant_channel_status_get(antplus_wakeup_slave_config[i].channel_number,
                                 &status);
          if ((status & STATUS_CHANNEL_STATE_MASK) == STATUS_ASSIGNED_CHANNEL) {
            ant_channel_open(antplus_wakeup_slave_config[i].channel_number);
            if (err != 0) {
              LOG_ERR("Failed to open ANT+ Slave (%d)", err);
            }
          }
        }
        if (k_sem_take(&poweroff_wakeup_sem, K_MSEC(1900)) == 0) {
          break;
        }
        led_set_bit(POWER_LED_BIT);
        if (k_sem_take(&poweroff_wakeup_sem, K_MSEC(100)) == 0) {
          break;
        }
        led_clear_bit(POWER_LED_BIT);
      }
      wakeup_success = true;
    }
  }

  if (wakeup_success) {
    sys_reboot(SYS_REBOOT_COLD);
  }

  led_clear_bit(POWER_LED_BIT);
  sys_poweroff();
  sys_reboot(SYS_REBOOT_COLD);
}
