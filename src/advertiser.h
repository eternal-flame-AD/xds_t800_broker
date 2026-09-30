#pragma once

#include <stdbool.h>
#include <zephyr/types.h>

enum advertiser_kind {
  ADVERTISER_KIND_PUBLIC = 0u,
  ADVERTISER_KIND_PRIVATE = 1u,
  ADVERTISER_KIND_COUNT = 2u,
};

void advertising_stop(void);
void advertising_start(void);

/**
 * @brief Check if the advertiser is public
 *
 * @return true if the advertiser is public, false otherwise
 */
bool advertising_is_public(void);

/**
 * @brief Set the device number
 *
 * @param device_number the device number to set
 * @return 0 on success, negative error code on failure
 */
int bt_adv_set_device_number(uint32_t device_number);

/**
 * @brief Set the advertiser kind
 *
 * @param kind the advertiser kind to set
 */
void bt_adv_set_kind(enum advertiser_kind kind);

/**
 * @brief Set the advertiser kind
 *
 * @param kind the advertiser kind to set
 */
enum advertiser_kind bt_adv_get_kind(void);