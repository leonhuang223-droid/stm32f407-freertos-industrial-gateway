#include "platform_f407.h"
#include "main.h"

#if defined(FIRMWARE_BOOTLOADER)
#include "f407_boot_platform.h"
#else
#include "app_context.h"
#include "app_rtos.h"
#include "f407_acquisition_port.h"
#include "f407_cli_port.h"
#include "f407_control_storage_port.h"
#include "f407_display_port.h"
#include "f407_fieldbus_port.h"
#include "f407_network_port.h"
#include "f407_reliability_port.h"
#endif

int main(void)
{
#if defined(FIRMWARE_BOOTLOADER)
    return f407_boot_main();
#else
    static app_context_t application;

    __set_BASEPRI(0u);
    __set_FAULTMASK(0u);
    __enable_irq();
    __DSB();
    __ISB();

    if (platform_f407_init() != SYS_OK ||
        app_context_init(&application) != SYS_OK) {
        platform_f407_panic();
    }
    application.acquisition_startup_status =
        f407_acquisition_configure(&application);
    application.fieldbus_startup_status =
        f407_fieldbus_configure(&application);
    (void)f407_control_storage_configure(&application);
    application.network_startup_status =
        f407_network_configure(&application);
    application.ui_startup_status = f407_display_configure(&application);
    application.cli_startup_status = f407_cli_configure(&application);
    if (f407_reliability_configure(&application) != SYS_OK) {
        platform_f407_panic();
    }
    if ((application.initialization_mask &
         (APP_INITIALIZED_ACQUISITION | APP_INITIALIZED_FIELDBUS |
          APP_INITIALIZED_STORAGE | APP_INITIALIZED_CONTROL |
          APP_INITIALIZED_NETWORK | APP_INITIALIZED_CONFIG |
          APP_INITIALIZED_UI | APP_INITIALIZED_CLI |
          APP_INITIALIZED_RELIABILITY)) !=
        (APP_INITIALIZED_ACQUISITION | APP_INITIALIZED_FIELDBUS |
         APP_INITIALIZED_STORAGE | APP_INITIALIZED_CONTROL |
         APP_INITIALIZED_NETWORK | APP_INITIALIZED_CONFIG |
         APP_INITIALIZED_UI | APP_INITIALIZED_CLI |
         APP_INITIALIZED_RELIABILITY)) {
        platform_f407_panic();
    }
    if (app_rtos_start(&application) != SYS_OK) {
        platform_f407_panic();
    }
    platform_f407_panic();
    return 1;
#endif
}
