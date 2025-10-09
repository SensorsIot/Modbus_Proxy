#pragma once

#include <stdbool.h>

bool wifi_init_sta(void);
bool wifi_is_connected(void);
void wifi_wait_connected(void);
