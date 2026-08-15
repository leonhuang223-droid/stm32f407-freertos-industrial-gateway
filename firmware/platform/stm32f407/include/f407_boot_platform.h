#ifndef F407_BOOT_PLATFORM_H
#define F407_BOOT_PLATFORM_H

#include "bootloader_core.h"

status_t f407_boot_platform_make_port(bootloader_port_t *out_port);
int f407_boot_platform_key_pressed(void);
void f407_boot_platform_fatal(status_t reason);
int f407_boot_main(void);

#endif
