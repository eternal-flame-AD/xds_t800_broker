#pragma once

#include <bluetooth/gatt_dm.h>
#include <zephyr/bluetooth/gatt.h>

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