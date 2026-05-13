#pragma once

#include <stdint.h>
#include <stdbool.h>

bool power_init(void);
uint8_t power_battery_level(void);
bool power_is_low_battery(void);
