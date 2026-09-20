#pragma once

#include <stdbool.h>

bool is_usb_enabled(void);
bool is_usb_connected(void);
int usb_set_enabled(bool enabled);