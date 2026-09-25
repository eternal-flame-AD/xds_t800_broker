#include "poweroff.h"
#include "leds.h"
#include <ant_interface.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(poweroff, LOG_LEVEL_INF);

void enter_poweroff(void) {
  led_clear_bit(POWER_LED_BIT);
  sys_poweroff();
  sys_reboot(SYS_REBOOT_COLD);
}
