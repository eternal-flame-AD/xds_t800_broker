#pragma once

#include <stdbool.h>

/**
 * @brief Enter poweroff mode
 * This function will turn off the device and enter poweroff mode.
 *
 * Will return if the device is in insomnia mode.
 */
void enter_poweroff(void);

/**
 * @brief Get the insomnia state
 *
 * @return true if the device is in insomnia mode
 * @return false otherwise
 */
bool poweroff_get_insomnia(void);