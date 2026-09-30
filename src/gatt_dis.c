#include "gatt_callbacks.h"
#include "zephyr/bluetooth/uuid.h"
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>

#define GATT_DIS_BUF_SIZE 32

#define SETTINGS_GATT_DIS_SUBTREE "gatt/dis"
#define SETTINGS_GATT_DIS_SERIAL_NUMBER_SEGMENT "serial_number"
#define SETTINGS_GATT_DIS_SOFTWARE_REVISION_SEGMENT "software_revision"

static char serial_number[GATT_DIS_BUF_SIZE + 1] = "(PAIRING)";
static char software_revision[GATT_DIS_BUF_SIZE + 1] = "(PAIRING)";

BT_GATT_SERVICE_DEFINE(
    dis_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_DIS),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_MANUFACTURER_NAME, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_cstr_cb, NULL,
                           CONFIG_BOARD),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_HARDWARE_REVISION, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_cstr_cb, NULL,
                           CONFIG_BOARD),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_FIRMWARE_REVISION, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_cstr_cb, NULL,
                           GIT_COMMIT),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_SOFTWARE_REVISION, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_cstr_cb, NULL,
                           software_revision),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_SERIAL_NUMBER, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, gatt_read_cstr_cb, NULL,
                           serial_number));

void dis_set_serial_number(uint8_t *data, uint16_t length) {
  length = MIN(length, GATT_DIS_BUF_SIZE);
  serial_number[length] = '\0';
  if (0 == memcmp(serial_number, data, length)) {
    return;
  }
  memcpy(serial_number, data, length);
  settings_save_one(SETTINGS_GATT_DIS_SUBTREE
                    "/" SETTINGS_GATT_DIS_SERIAL_NUMBER_SEGMENT,
                    serial_number, strlen(serial_number));
}

void dis_set_software_revision(uint8_t *data, uint16_t length) {
  length = MIN(length, GATT_DIS_BUF_SIZE);
  software_revision[length] = '\0';
  if (0 == memcmp(software_revision, data, length)) {
    return;
  }
  memcpy(software_revision, data, length);
  settings_save_one(SETTINGS_GATT_DIS_SUBTREE
                    "/" SETTINGS_GATT_DIS_SOFTWARE_REVISION_SEGMENT,
                    software_revision, strlen(software_revision));
}

static int gatt_dis_settings_set(const char *name, size_t len,
                                 settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  int rc;

  if (settings_name_steq(name, SETTINGS_GATT_DIS_SERIAL_NUMBER_SEGMENT,
                         &next) &&
      !next) {
    if (len > GATT_DIS_BUF_SIZE) {
      len = GATT_DIS_BUF_SIZE;
    }
    memset(serial_number, 0, sizeof(serial_number));
    rc = read_cb(cb_arg, serial_number, sizeof(serial_number));
    if (rc >= 0) {
      return 0;
    }
    return rc;
  }

  if (settings_name_steq(name, SETTINGS_GATT_DIS_SOFTWARE_REVISION_SEGMENT,
                         &next) &&
      !next) {
    if (len > GATT_DIS_BUF_SIZE) {
      len = GATT_DIS_BUF_SIZE;
    }
    memset(software_revision, 0, sizeof(software_revision));
    rc = read_cb(cb_arg, software_revision, sizeof(software_revision));
    if (rc >= 0) {
      return 0;
    }
    return rc;
  }

  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(dis, SETTINGS_GATT_DIS_SUBTREE, NULL,
                               gatt_dis_settings_set, NULL, NULL);