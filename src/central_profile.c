#include "central_profile.h"
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(central_profile, CONFIG_LOG_DEFAULT_LEVEL);

uint8_t central_profile_reset_pairing(const char *key) {
  uint8_t done = 0;
  STRUCT_SECTION_FOREACH(central_profile_instance, instance) {
    bool matched = key == NULL;
    if (key) {
      if (strcmp(instance->profile->name, key) == 0) {
        matched = true;
      }
      bt_addr_t addr = bt_addr_any;
      if (0 == bt_addr_from_str(key, &addr) &&
          0 == bt_addr_cmp(&addr, &instance->known_peer.a)) {
        matched = true;
      }
    }
    if (matched) {
      done++;
      struct bt_conn *conn = bt_conn_ref(instance->conn);
      if (conn) {
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(conn);
      }
      bt_addr_le_copy(&instance->known_peer, BT_ADDR_LE_ANY);
    }
  }
  return done;
}

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