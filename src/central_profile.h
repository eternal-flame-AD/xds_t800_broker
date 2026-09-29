#pragma once

#include "leds.h"
#include <bluetooth/gatt_dm.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/iterable_sections.h>

struct central_profile {
  const char *name;
  const char *device_name_prefix;

  /* Called when a service is found during discovery. */
  int (*on_discovery)(struct bt_gatt_dm *dm);

  /* Called after the whole connection is up. */
  void (*on_connected)(struct bt_conn *conn);

  /* Called when the central disconnects. */
  void (*on_disconnected)(struct bt_conn *conn, uint8_t reason);

  const struct bt_uuid *service_uuids[];
};

struct central_profile_instance {
  const struct central_profile *profile;
  bt_addr_le_t known_peer;
  uint8_t led_idx;
  struct bt_conn *conn;
  const struct bt_uuid *const *next_service_uuid;
  bool is_registered;
};

// LED will be assigned in central_profile_init
#define REGISTER_CENTRAL_PROFILE(name, val)                                    \
  STRUCT_SECTION_ITERABLE(central_profile_instance, name) = {                  \
      .profile = val,                                                          \
      .known_peer = {0},                                                       \
      .led_idx = NULL_LED_BIT,                                                 \
      .conn = NULL,                                                            \
      .next_service_uuid = NULL,                                               \
      .is_registered = false,                                                  \
  };
