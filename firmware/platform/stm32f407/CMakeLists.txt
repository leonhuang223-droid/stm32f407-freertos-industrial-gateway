enable_language(ASM)

set(F407_CUBE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/cubemx/generated/stm32f407_gateway")
set(FREERTOS_ROOT "${PROJECT_SOURCE_DIR}/third_party/freertos_kernel")
set(F407_VENDOR_HAL_ROOT "${PROJECT_SOURCE_DIR}/third_party/stm32f4_hal")

if(NOT EXISTS "${F407_CUBE_ROOT}/Core/Inc/main.h")
    message(FATAL_ERROR
        "STM32F407 CubeMX sources are missing. Run "
        "tools/generate-stm32f407-cubemx.ps1 first.")
endif()

set(F407_HAL_SOURCES
    "${F407_CUBE_ROOT}/Core/Src/gpio.c"
    "${F407_CUBE_ROOT}/Core/Src/dma.c"
    "${F407_CUBE_ROOT}/Core/Src/can.c"
    "${F407_CUBE_ROOT}/Core/Src/spi.c"
    "${F407_CUBE_ROOT}/Core/Src/usart.c"
    "${F407_CUBE_ROOT}/Core/Src/stm32f4xx_hal_msp.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c"
    "${F407_VENDOR_HAL_ROOT}/Src/stm32f4xx_hal_i2c.c"
    "${F407_VENDOR_HAL_ROOT}/Src/stm32f4xx_hal_i2c_ex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c"
)

set(F407_HAL_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${F407_CUBE_ROOT}/Core/Inc"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc"
    "${F407_CUBE_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy"
    "${F407_VENDOR_HAL_ROOT}/Inc"
    "${F407_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
    "${F407_CUBE_ROOT}/Drivers/CMSIS/Include"
)

add_library(stm32f407_hal STATIC ${F407_HAL_SOURCES})
target_include_directories(stm32f407_hal PUBLIC ${F407_HAL_INCLUDES})
target_compile_definitions(stm32f407_hal PUBLIC
    STM32F407xx USE_HAL_DRIVER HAL_I2C_MODULE_ENABLED)
target_compile_options(stm32f407_hal PRIVATE -Wall -Wextra)

add_library(freertos_kernel_cm4f STATIC
    "${FREERTOS_ROOT}/tasks.c"
    "${FREERTOS_ROOT}/queue.c"
    "${FREERTOS_ROOT}/list.c"
    "${FREERTOS_ROOT}/timers.c"
    "${FREERTOS_ROOT}/event_groups.c"
    "${FREERTOS_ROOT}/stream_buffer.c"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c"
)
target_include_directories(freertos_kernel_cm4f PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${FREERTOS_ROOT}/include"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F"
    ${F407_HAL_INCLUDES}
)
target_compile_definitions(freertos_kernel_cm4f PUBLIC STM32F407xx)
target_compile_options(freertos_kernel_cm4f PRIVATE -Wall -Wextra)

set(F407_COMMON_SOURCES
    "${PROJECT_SOURCE_DIR}/firmware/common/src/boot_metadata.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/crc32.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/error_code.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/external_flash_layout.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/image_descriptor.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/image_header.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/image_verify.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/manifest.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/partition_table.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/sha256.c"
    "${PROJECT_SOURCE_DIR}/firmware/common/src/version.c"
)

add_library(f407_ota_portable_compile OBJECT
    ${F407_COMMON_SOURCES}
    "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/bootloader_core.c"
    "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/image_install.c"
)
target_include_directories(f407_ota_portable_compile PRIVATE
    "${PROJECT_SOURCE_DIR}/firmware/common/include"
    "${PROJECT_SOURCE_DIR}/firmware/bootloader/include"
)
target_compile_definitions(f407_ota_portable_compile PRIVATE STM32F407xx)
target_compile_options(f407_ota_portable_compile PRIVATE -Wall -Wextra -Werror)

function(add_stm32f407_image TARGET_NAME)
    set(OPTIONS USE_FREERTOS)
    set(ONE_VALUE_ARGS LINKER_SCRIPT VECTOR_OFFSET)
    cmake_parse_arguments(FW "${OPTIONS}" "${ONE_VALUE_ARGS}" "" ${ARGN})

    if(NOT FW_LINKER_SCRIPT OR NOT DEFINED FW_VECTOR_OFFSET)
        message(FATAL_ERROR
            "${TARGET_NAME} requires LINKER_SCRIPT and VECTOR_OFFSET")
    endif()

    set(ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")
    add_executable(${TARGET_NAME}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_interrupts.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform_f407.c"
        "${F407_CUBE_ROOT}/startup_stm32f407xx.s"
        "${F407_CUBE_ROOT}/Core/Src/system_stm32f4xx.c"
        "${F407_CUBE_ROOT}/Core/Src/syscalls.c"
        "${PROJECT_SOURCE_DIR}/firmware/common/src/error_code.c"
        "${PROJECT_SOURCE_DIR}/firmware/common/src/crc32.c"
        "${PROJECT_SOURCE_DIR}/firmware/common/src/external_flash_layout.c"
        "${PROJECT_SOURCE_DIR}/firmware/common/src/partition_table.c"
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${ARTIFACT_DIR}"
        LINK_DEPENDS "${FW_LINKER_SCRIPT}"
    )
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/firmware/common/include"
        ${F407_HAL_INCLUDES}
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        STM32F407xx
        USE_HAL_DRIVER
        USER_VECT_TAB_ADDRESS
        VECT_TAB_OFFSET=${FW_VECTOR_OFFSET}
        FIRMWARE_IMAGE_NAME="${TARGET_NAME}"
        FIRMWARE_VECTOR_OFFSET=${FW_VECTOR_OFFSET}
    )
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wall -Wextra -Werror
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-Os>
    )
    target_link_options(${TARGET_NAME} PRIVATE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        --specs=nano.specs
        --specs=nosys.specs
        "-T${FW_LINKER_SCRIPT}"
        "-L${PROJECT_SOURCE_DIR}/firmware/linker"
        "-Wl,-Map=${ARTIFACT_DIR}/${TARGET_NAME}.map"
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,--no-warn-rwx-segments
    )
    target_link_libraries(${TARGET_NAME} PRIVATE stm32f407_hal c m nosys)

    if(FW_USE_FREERTOS)
        target_sources(${TARGET_NAME} PRIVATE
            "${PROJECT_SOURCE_DIR}/firmware/common/src/boot_metadata.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/manifest.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/sha256.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/version.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_acquisition_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_control_storage_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_fieldbus_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_fault_capture.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_network_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_reliability_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_flash.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_cli_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_display_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_gt911_port.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_lcd_bus.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_uart_dispatch.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/core/src/app_context.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/ads1115.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/can_bus.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/cli_transport.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/device.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/display_device.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/gt911.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/i2c_bus.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/input_device.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/lcd_bus.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/lcd_controller.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/lcd_ili9806g.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/lcd_nt35510.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/max31865.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/network_transport.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/esp8266.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/fault_recorder.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/relay.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/rs485_bus.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/sht30.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/spi_bus.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/storage_media.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/w25q128.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/src/watchdog_device.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/rtos/src/app_rtos.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/protocols/src/can_protocol.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/protocols/src/modbus_rtu_master.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/protocols/src/mqtt_codec.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/protocols/src/http_client_raw.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/acquisition_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/alarm_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/cli_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/config_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/fieldbus_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/network_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/ota_manager.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/ota_staging.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/periodic_timing_monitor.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/storage_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/boot_confirmation.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/critical_timing_monitor.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/deep_power_controller.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/power_manager.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/src/supervisor_subsystem.c"
            "${PROJECT_SOURCE_DIR}/firmware/app/ui/src/ui_subsystem.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_freertos_hooks.c"
        )
        target_include_directories(${TARGET_NAME} PRIVATE
            "${PROJECT_SOURCE_DIR}/firmware/app/core/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/devices/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/domain/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/protocols/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/rtos/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/subsystems/include"
            "${PROJECT_SOURCE_DIR}/firmware/app/ui/include"
        )
        target_compile_definitions(${TARGET_NAME} PRIVATE
            FIRMWARE_USE_FREERTOS=1
            $<$<CONFIG:Debug>:F407_ENABLE_FAULT_INJECTION=1>
            $<$<CONFIG:Release>:F407_ENABLE_FAULT_INJECTION=0>)
        target_link_libraries(${TARGET_NAME} PRIVATE freertos_kernel_cm4f lvgl)
    else()
        target_sources(${TARGET_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_boot_platform.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/f407_flash.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/w25q_boot.c"
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/boot_jump.c"
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/boot_reset.c"
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/boot_staging_package.c"
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/bootloader_core.c"
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/src/image_install.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/boot_metadata.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/crc32.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/image_descriptor.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/image_header.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/image_verify.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/sha256.c"
            "${PROJECT_SOURCE_DIR}/firmware/common/src/version.c")
        target_include_directories(${TARGET_NAME} PRIVATE
            "${PROJECT_SOURCE_DIR}/firmware/bootloader/include")
        target_compile_definitions(${TARGET_NAME} PRIVATE
            FIRMWARE_BOOTLOADER=1)
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:${TARGET_NAME}>"
            "${ARTIFACT_DIR}/${TARGET_NAME}.bin"
        COMMAND "${CMAKE_OBJCOPY}" -O ihex
            "$<TARGET_FILE:${TARGET_NAME}>"
            "${ARTIFACT_DIR}/${TARGET_NAME}.hex"
        COMMAND "${CMAKE_COMMAND}"
            "-DOBJDUMP_TOOL=${CMAKE_OBJDUMP}"
            "-DELF_FILE=$<TARGET_FILE:${TARGET_NAME}>"
            "-DOUTPUT_FILE=${ARTIFACT_DIR}/${TARGET_NAME}.lst"
            -P "${PROJECT_SOURCE_DIR}/cmake/write-listing.cmake"
        COMMAND "${CMAKE_COMMAND}"
            "-DSIZE_TOOL=${CMAKE_SIZE}"
            "-DELF_FILE=$<TARGET_FILE:${TARGET_NAME}>"
            "-DIMAGE=${TARGET_NAME}"
            "-DOUTPUT_FILE=${ARTIFACT_DIR}/${TARGET_NAME}.size.json"
            -P "${PROJECT_SOURCE_DIR}/cmake/write-size-json.cmake"
        COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${TARGET_NAME}>"
        BYPRODUCTS
            "${ARTIFACT_DIR}/${TARGET_NAME}.bin"
            "${ARTIFACT_DIR}/${TARGET_NAME}.hex"
            "${ARTIFACT_DIR}/${TARGET_NAME}.map"
            "${ARTIFACT_DIR}/${TARGET_NAME}.lst"
            "${ARTIFACT_DIR}/${TARGET_NAME}.size.json"
        VERBATIM
    )
endfunction()

add_stm32f407_image(f407_bootloader
    LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/firmware/linker/stm32f407zg_bootloader.ld"
    VECTOR_OFFSET 0x00000000U)
add_stm32f407_image(f407_app_a
    USE_FREERTOS
    LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/firmware/linker/stm32f407zg_app_a.ld"
    VECTOR_OFFSET 0x00020000U)
add_stm32f407_image(f407_app_b
    USE_FREERTOS
    LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/firmware/linker/stm32f407zg_app_b.ld"
    VECTOR_OFFSET 0x00080000U)

add_custom_target(f407_firmware ALL
    DEPENDS
        f407_bootloader
        f407_app_a
        f407_app_b
        f407_ota_portable_compile)

add_custom_target(verify_f407_firmware ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DARTIFACT_DIR=${CMAKE_BINARY_DIR}/artifacts"
        "-DOBJDUMP_TOOL=${CMAKE_OBJDUMP}"
        "-DNM_TOOL=${CMAKE_NM}"
        -P "${PROJECT_SOURCE_DIR}/tests/f407/verify_f407_artifacts.cmake"
    DEPENDS f407_firmware
    VERBATIM)
