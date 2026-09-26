#include "poweroff.h"
#include "leds.h"
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

void enter_poweroff(void) {
  led_clear_bit(POWER_LED_BIT);
  sys_poweroff();
  sys_reboot(SYS_REBOOT_COLD);
}
