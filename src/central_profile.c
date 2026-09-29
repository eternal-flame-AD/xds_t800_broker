#include "central_profile.h"

static int central_profile_init(void) {
  // assign LED indices to profiles in order
  uint32_t profile_count = 0;
  struct central_profile_instance *instance = NULL;
  STRUCT_SECTION_COUNT(central_profile_instance, &profile_count);
  if (profile_count >= 1) {
    STRUCT_SECTION_GET(central_profile_instance, 0, &instance);
    instance->led_idx = CENTRAL1_CONN_STATUS_LED_BIT;
  }
  if (profile_count >= 2) {
    STRUCT_SECTION_GET(central_profile_instance, 1, &instance);
    instance->led_idx = CENTRAL2_CONN_STATUS_LED_BIT;
  }
  return 0;
}

SYS_INIT(central_profile_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);