if(BUILD_STM32F407_FIRMWARE)
    add_subdirectory(platform/stm32f407)
    return()
endif()

add_library(common STATIC
    common/src/boot_metadata.c
    common/src/crc32.c
    common/src/error_code.c
    common/src/external_flash_layout.c
    common/src/image_descriptor.c
    common/src/image_header.c
    common/src/image_verify.c
    common/src/manifest.c
    common/src/partition_table.c
    common/src/sha256.c
    common/src/version.c
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)
target_compile_definitions(common PUBLIC STM32F407xx)

add_library(sensor_stack STATIC
    app/devices/src/device.c
    app/devices/src/i2c_bus.c
    app/devices/src/spi_bus.c
    app/devices/src/ads1115.c
    app/devices/src/max31865.c
    app/devices/src/sht30.c
    app/subsystems/src/acquisition_subsystem.c
    app/subsystems/src/periodic_timing_monitor.c
)

target_include_directories(sensor_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
)

target_link_libraries(sensor_stack PUBLIC common m)
target_compile_options(sensor_stack PRIVATE -Wall -Wextra -Werror)

add_library(display_touch_stack STATIC
    app/devices/src/gt911.c
    app/devices/src/lcd_bus.c
    app/devices/src/lcd_controller.c
    app/devices/src/lcd_ili9806g.c
    app/devices/src/lcd_nt35510.c
)

target_include_directories(display_touch_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
)

target_link_libraries(display_touch_stack PUBLIC sensor_stack common)
target_compile_options(display_touch_stack PRIVATE -Wall -Wextra -Werror)

add_library(fieldbus_stack STATIC
    app/devices/src/can_bus.c
    app/devices/src/rs485_bus.c
    app/protocols/src/can_protocol.c
    app/protocols/src/modbus_rtu_master.c
    app/subsystems/src/fieldbus_subsystem.c
)

target_include_directories(fieldbus_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/protocols/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
)

target_link_libraries(fieldbus_stack PUBLIC common)
target_compile_options(fieldbus_stack PRIVATE -Wall -Wextra -Werror)

add_library(control_storage_stack STATIC
    app/devices/src/fault_recorder.c
    app/devices/src/relay.c
    app/devices/src/storage_media.c
    app/devices/src/w25q128.c
    app/subsystems/src/alarm_subsystem.c
    app/subsystems/src/config_subsystem.c
    app/subsystems/src/storage_subsystem.c
)

target_include_directories(control_storage_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
)

target_link_libraries(control_storage_stack PUBLIC sensor_stack common)
target_compile_options(control_storage_stack PRIVATE -Wall -Wextra -Werror)

add_library(network_stack STATIC
    app/devices/src/network_transport.c
    app/devices/src/esp8266.c
    app/protocols/src/mqtt_codec.c
    app/subsystems/src/network_subsystem.c
)

target_include_directories(network_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/protocols/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
)

target_link_libraries(network_stack PUBLIC common)
target_compile_options(network_stack PRIVATE -Wall -Wextra -Werror)

add_library(ota_update_stack STATIC
    app/protocols/src/http_client_raw.c
    app/subsystems/src/ota_manager.c
    app/subsystems/src/ota_staging.c
)

target_include_directories(ota_update_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/protocols/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
)

target_link_libraries(ota_update_stack PUBLIC
    common
    control_storage_stack
    network_stack
)
target_compile_options(ota_update_stack PRIVATE -Wall -Wextra -Werror)

add_library(ui_cli_stack STATIC
    app/devices/src/cli_transport.c
    app/devices/src/display_device.c
    app/devices/src/input_device.c
    app/subsystems/src/cli_subsystem.c
    app/ui/src/ui_subsystem.c
)

target_include_directories(ui_cli_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/devices/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/subsystems/include
    ${CMAKE_CURRENT_SOURCE_DIR}/app/ui/include
)

target_link_libraries(ui_cli_stack PUBLIC
    control_storage_stack
    lvgl
)
target_compile_options(ui_cli_stack PRIVATE -Wall -Wextra -Werror)

add_library(gateway_architecture STATIC
    app/core/src/app_context.c
    app/devices/src/watchdog_device.c
    app/subsystems/src/boot_confirmation.c
    app/subsystems/src/critical_timing_monitor.c
    app/subsystems/src/deep_power_controller.c
    app/subsystems/src/power_manager.c
    app/subsystems/src/supervisor_subsystem.c
)

target_include_directories(gateway_architecture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/core/include
)

target_link_libraries(gateway_architecture PUBLIC
    sensor_stack
    fieldbus_stack
    control_storage_stack
    network_stack
    ota_update_stack
    ui_cli_stack
)
target_compile_options(gateway_architecture PRIVATE -Wall -Wextra -Werror)
