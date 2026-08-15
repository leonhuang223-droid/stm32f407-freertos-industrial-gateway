#ifndef PLATFORM_F407_H
#define PLATFORM_F407_H

#include "error_code.h"

#include <stdint.h>

status_t platform_f407_init(void);
void platform_f407_panic(void);
void platform_f407_pre_sleep(uint32_t *expected_idle_ticks);
void platform_f407_post_sleep(uint32_t expected_idle_ticks);

#if defined(FIRMWARE_USE_FREERTOS)
#include "power_manager.h"
#include "watchdog_device.h"
void platform_f407_bind_reliability(power_manager_t *power,
                                    watchdog_device_t *watchdog);
#endif

#endif
