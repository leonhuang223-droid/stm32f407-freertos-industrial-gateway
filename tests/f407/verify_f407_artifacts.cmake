cmake_minimum_required(VERSION 3.20)

set(ELF_INTERNAL_FLASH_END 0x08100000)
include("${CMAKE_CURRENT_LIST_DIR}/../phase1/verify_elf_flash_range.cmake")

foreach(REQUIRED_VARIABLE ARTIFACT_DIR OBJDUMP_TOOL NM_TOOL)
    if(NOT DEFINED ${REQUIRED_VARIABLE})
        message(FATAL_ERROR "${REQUIRED_VARIABLE} is required")
    endif()
endforeach()

set(IMAGE_LAYOUTS
    "f407_bootloader|08000000|08010000"
    "f407_app_a|08020000|08060000"
    "f407_app_b|08080000|080C0000"
)

foreach(IMAGE_LAYOUT IN LISTS IMAGE_LAYOUTS)
    string(REPLACE "|" ";" IMAGE_PARTS "${IMAGE_LAYOUT}")
    list(GET IMAGE_PARTS 0 IMAGE)
    list(GET IMAGE_PARTS 1 VECTOR_ADDRESS)
    list(GET IMAGE_PARTS 2 FLASH_LIMIT)

    foreach(SUFFIX elf bin hex map lst size.json)
        set(ARTIFACT "${ARTIFACT_DIR}/${IMAGE}.${SUFFIX}")
        if(NOT EXISTS "${ARTIFACT}")
            message(FATAL_ERROR "missing F407 artifact: ${ARTIFACT}")
        endif()
        file(SIZE "${ARTIFACT}" ARTIFACT_SIZE)
        if(ARTIFACT_SIZE EQUAL 0)
            message(FATAL_ERROR "empty F407 artifact: ${ARTIFACT}")
        endif()
    endforeach()

    execute_process(
        COMMAND "${OBJDUMP_TOOL}" -h "${ARTIFACT_DIR}/${IMAGE}.elf"
        RESULT_VARIABLE OBJDUMP_RESULT
        OUTPUT_VARIABLE OBJDUMP_OUTPUT
        ERROR_VARIABLE OBJDUMP_ERROR)
    if(NOT OBJDUMP_RESULT EQUAL 0)
        message(FATAL_ERROR "objdump failed for ${IMAGE}: ${OBJDUMP_ERROR}")
    endif()

    elf_vector_vma_check(
        "${OBJDUMP_OUTPUT}" "0x${VECTOR_ADDRESS}"
        VECTOR_OK VECTOR_DETAIL)
    if(NOT VECTOR_OK)
        message(FATAL_ERROR "${IMAGE}: ${VECTOR_DETAIL}")
    endif()
    elf_flash_range_check(
        "${OBJDUMP_OUTPUT}" "0x${VECTOR_ADDRESS}" "0x${FLASH_LIMIT}"
        RANGE_OK RANGE_DETAIL)
    if(NOT RANGE_OK)
        message(FATAL_ERROR "${IMAGE}: ${RANGE_DETAIL}")
    endif()

    execute_process(
        COMMAND "${OBJDUMP_TOOL}" -d --disassemble=SystemInit
            "${ARTIFACT_DIR}/${IMAGE}.elf"
        RESULT_VARIABLE SYSTEM_INIT_RESULT
        OUTPUT_VARIABLE SYSTEM_INIT_OUTPUT
        ERROR_VARIABLE SYSTEM_INIT_ERROR)
    string(REGEX REPLACE "^0+" "" VECTOR_ADDRESS_SHORT "${VECTOR_ADDRESS}")
    if(NOT SYSTEM_INIT_RESULT EQUAL 0 OR
       (NOT SYSTEM_INIT_OUTPUT MATCHES "0x${VECTOR_ADDRESS}" AND
        NOT SYSTEM_INIT_OUTPUT MATCHES "0x${VECTOR_ADDRESS_SHORT}"))
        message(FATAL_ERROR
            "${IMAGE} SystemInit does not program VTOR ${VECTOR_ADDRESS}: "
            "${SYSTEM_INIT_ERROR}")
    endif()
endforeach()

execute_process(
    COMMAND "${NM_TOOL}" --defined-only
        "${ARTIFACT_DIR}/f407_bootloader.elf"
    OUTPUT_VARIABLE BOOT_SYMBOLS
    RESULT_VARIABLE BOOT_NM_RESULT)
if(NOT BOOT_NM_RESULT EQUAL 0)
    message(FATAL_ERROR "nm failed for F407 bootloader")
endif()
foreach(FORBIDDEN_SYMBOL vTaskStartScheduler xQueueCreateStatic xEventGroupCreateStatic)
    if(BOOT_SYMBOLS MATCHES "(^|\n)[^\n]*[ \t]${FORBIDDEN_SYMBOL}(\n|$)")
        message(FATAL_ERROR
            "F407 bootloader contains FreeRTOS symbol ${FORBIDDEN_SYMBOL}")
    endif()
endforeach()
foreach(REQUIRED_BOOT_SYMBOL
        bootloader_execute
        image_install_package
        image_descriptor_confirm_candidate
        f407_flash_erase_inactive
        f407_flash_erase_metadata
        w25q_boot_read
        boot_jump_to_slot)
    if(NOT BOOT_SYMBOLS MATCHES
            "(^|\n)[^\n]*[ \t]${REQUIRED_BOOT_SYMBOL}(\n|$)")
        message(FATAL_ERROR
            "F407 bootloader is missing ${REQUIRED_BOOT_SYMBOL}")
    endif()
endforeach()

foreach(APP f407_app_a f407_app_b)
    execute_process(
        COMMAND "${NM_TOOL}" --defined-only "${ARTIFACT_DIR}/${APP}.elf"
        OUTPUT_VARIABLE APP_SYMBOLS
        RESULT_VARIABLE APP_NM_RESULT)
    if(NOT APP_NM_RESULT EQUAL 0)
        message(FATAL_ERROR "nm failed for ${APP}")
    endif()
    foreach(REQUIRED_SYMBOL
            vTaskStartScheduler
            xQueueCreateSetStatic
            xEventGroupCreateStatic
            xQueueCreateMutexStatic
            acquisition_subsystem_process
            ads1115_sample
            max31865_sample
            sht30_sample
            f407_acquisition_configure
            modbus_master_poll_next
            modbus_rtu_crc16
            fieldbus_subsystem_process_can
            f407_fieldbus_configure
            alarm_subsystem_process
            alarm_subsystem_reconfigure
            config_subsystem_prepare
            config_subsystem_commit
            relay_force_safe
            storage_subsystem_append_log
            storage_subsystem_append_alarm
            storage_subsystem_archive_fault
            storage_subsystem_load_latest_fault
            storage_subsystem_save_config
            periodic_timing_monitor_note
            mqtt_encode_publish_qos1
            network_subsystem_process
            network_subsystem_suspend
            network_subsystem_resume
            network_subsystem_acquire_ota_lease
            esp8266_tcp_send
            f407_network_configure
            display_device_flush
            input_device_read
            ui_subsystem_process
            ui_subsystem_navigate
            lcd_controller_detect
            lcd_controller_flush
            gt911_init
            gt911_read
            f407_external_sram_self_test
            EXTI9_5_IRQHandler
            cli_subsystem_process
            cli_parse_command
            f407_display_configure
            f407_cli_configure
            f407_reliability_configure
            supervisor_subsystem_process
            power_manager_prepare_tickless
            deep_power_controller_request
            deep_power_controller_process
            critical_timing_monitor_enter
            critical_timing_monitor_exit
            boot_confirmation_confirm
            boot_meta_confirm_boot_ok
            watchdog_device_refresh
            uxTaskGetStackHighWaterMark
            uxTaskGetSystemState
            fault_record_validate
            fault_recorder_get
            fault_recorder_inject
            f407_fault_capture_exception
            platform_runtime_stats_counter
            lv_init
            lv_display_create
            lv_label_create
            lv_button_create
            w25q128_program
            w25q128_erase_sector
            storage_subsystem_power_down
            storage_subsystem_wake
            f407_control_storage_configure
            HAL_I2C_Master_Transmit
            HAL_SPI_TransmitReceive
            HAL_UART_Transmit_DMA
            HAL_UARTEx_ReceiveToIdle_DMA
            HAL_CAN_AddTxMessage
            HAL_CAN_GetRxMessage
            xTaskGenericNotifyFromISR
            SVC_Handler
            PendSV_Handler
            SysTick_Handler)
        if(NOT APP_SYMBOLS MATCHES
                "(^|\n)[^\n]*[ \t]${REQUIRED_SYMBOL}(\n|$)")
            message(FATAL_ERROR "${APP} is missing ${REQUIRED_SYMBOL}")
        endif()
    endforeach()
    if(ARTIFACT_DIR MATCHES "/debug/artifacts$")
        foreach(DEBUG_FAULT_SYMBOL
                f407_fault_prepare_watchdog_injection
                f407_fault_inject_hardfault)
            if(NOT APP_SYMBOLS MATCHES
                    "(^|\n)[^\n]*[ \t]${DEBUG_FAULT_SYMBOL}(\n|$)")
                message(FATAL_ERROR
                    "${APP} debug image is missing ${DEBUG_FAULT_SYMBOL}")
            endif()
        endforeach()
    elseif(ARTIFACT_DIR MATCHES "/release/artifacts$")
        foreach(RELEASE_FORBIDDEN_SYMBOL
                f407_fault_prepare_watchdog_injection
                f407_fault_inject_hardfault)
            if(APP_SYMBOLS MATCHES
                    "(^|\n)[^\n]*[ \t]${RELEASE_FORBIDDEN_SYMBOL}(\n|$)")
                message(FATAL_ERROR
                    "${APP} release image retains ${RELEASE_FORBIDDEN_SYMBOL}")
            endif()
        endforeach()
    endif()
endforeach()

set(F407_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(BOARD_IOC
    "${F407_ROOT}/firmware/platform/stm32f407/cubemx/stm32f407_gateway.ioc")
set(ACQUISITION_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_acquisition_port.c")
set(FIELDBUS_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_fieldbus_port.c")
set(CONTROL_STORAGE_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_control_storage_port.c")
set(NETWORK_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_network_port.c")
set(CLI_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_cli_port.c")
set(DISPLAY_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_display_port.c")
set(LCD_BUS_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_lcd_bus.c")
set(GT911_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_gt911_port.c")
set(APP_RTOS
    "${F407_ROOT}/firmware/app/rtos/src/app_rtos.c")
set(STORAGE_SUBSYSTEM
    "${F407_ROOT}/firmware/app/subsystems/src/storage_subsystem.c")
set(PERIODIC_TIMING_MONITOR
    "${F407_ROOT}/firmware/app/subsystems/src/periodic_timing_monitor.c")
set(RELIABILITY_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_reliability_port.c")
set(FAULT_CAPTURE_PORT
    "${F407_ROOT}/firmware/platform/stm32f407/src/f407_fault_capture.c")
set(FREERTOS_CONFIG
    "${F407_ROOT}/firmware/platform/stm32f407/include/FreeRTOSConfig.h")
set(GENERATED_MAIN_H
    "${F407_ROOT}/firmware/platform/stm32f407/cubemx/generated/stm32f407_gateway/Core/Inc/main.h")
file(READ "${BOARD_IOC}" BOARD_IOC_CONTENT)
file(READ "${ACQUISITION_PORT}" ACQUISITION_PORT_CONTENT)
file(READ "${FIELDBUS_PORT}" FIELDBUS_PORT_CONTENT)
file(READ "${CONTROL_STORAGE_PORT}" CONTROL_STORAGE_PORT_CONTENT)
file(READ "${NETWORK_PORT}" NETWORK_PORT_CONTENT)
file(READ "${CLI_PORT}" CLI_PORT_CONTENT)
file(READ "${DISPLAY_PORT}" DISPLAY_PORT_CONTENT)
file(READ "${LCD_BUS_PORT}" LCD_BUS_PORT_CONTENT)
file(READ "${GT911_PORT}" GT911_PORT_CONTENT)
file(READ "${APP_RTOS}" APP_RTOS_CONTENT)
file(READ "${STORAGE_SUBSYSTEM}" STORAGE_SUBSYSTEM_CONTENT)
file(READ "${PERIODIC_TIMING_MONITOR}" PERIODIC_TIMING_MONITOR_CONTENT)
file(READ "${RELIABILITY_PORT}" RELIABILITY_PORT_CONTENT)
file(READ "${FAULT_CAPTURE_PORT}" FAULT_CAPTURE_PORT_CONTENT)
file(READ "${FREERTOS_CONFIG}" FREERTOS_CONFIG_CONTENT)
file(READ "${GENERATED_MAIN_H}" GENERATED_MAIN_CONTENT)

foreach(REQUIRED_IOC_SETTING
        "PA11.Signal=CAN1_RX"
        "PA12.Signal=CAN1_TX"
        "PA2.Signal=USART2_TX"
        "PA3.Signal=USART2_RX"
        "PC0.GPIO_Label=RS485_DE"
        "Dma.USART2_RX.2.Instance=DMA1_Stream5"
        "Dma.USART2_TX.3.Instance=DMA1_Stream6"
        "PA9.Signal=USART1_TX"
        "PA10.Signal=USART1_RX"
        "Dma.USART1_RX.0.Instance=DMA2_Stream2"
        "Dma.USART1_RX.0.Mode=DMA_CIRCULAR"
        "Dma.USART1_TX.1.Instance=DMA2_Stream7"
        "Dma.USART1_TX.1.Mode=DMA_NORMAL"
        "NVIC.USART1_IRQn=true"
        "PB10.Signal=USART3_TX"
        "PB11.Signal=USART3_RX"
        "Dma.USART3_RX.4.Instance=DMA1_Stream1"
        "Dma.USART3_TX.5.Instance=DMA1_Stream3"
        "NVIC.USART3_IRQn=true"
        "CAN1.CalculateBaudRate=250000"
        "PA4.GPIO_Label=MAX31865_CS"
        "PG2.GPIO_Label=RELAY_DO"
        "PG6.GPIO_Label=W25Q128_CS"
        "SPI1.CLKPolarity=SPI_POLARITY_HIGH"
        "SPI1.CLKPhase=SPI_PHASE_2EDGE"
        "SPI2.CLKPolarity=SPI_POLARITY_LOW"
        "SPI2.CLKPhase=SPI_PHASE_2EDGE"
        "PB13.Signal=SPI2_SCK"
        "PB14.Signal=SPI2_MISO"
        "PB15.Signal=SPI2_MOSI")
    string(FIND "${BOARD_IOC_CONTENT}" "${REQUIRED_IOC_SETTING}" SETTING_INDEX)
    if(SETTING_INDEX EQUAL -1)
        message(FATAL_ERROR "F407 .ioc is missing ${REQUIRED_IOC_SETTING}")
    endif()
endforeach()

foreach(REQUIRED_RELIABILITY_MARKER
        "IWDG->KR = F407_IWDG_START_KEY"
        "app_context_configure_reliability"
        "platform_f407_bind_reliability"
        "boot_meta_validate"
        "fault_recorder_ops"
        "F407_ENABLE_FAULT_INJECTION"
        "deep_power_ops"
        "F407_STOP_PERIODIC_ENABLED"
        "F407_STANDBY_SHIPPING_ENABLED")
    string(FIND "${RELIABILITY_PORT_CONTENT}"
        "${REQUIRED_RELIABILITY_MARKER}" RELIABILITY_INDEX)
    if(RELIABILITY_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 reliability port is missing ${REQUIRED_RELIABILITY_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_FAULT_CAPTURE_MARKER
        "RTC->BKP0R"
        "fault_record_finalize"
        "FAULT_ORIGIN_WATCHDOG_INJECTION"
        "NVIC_SystemReset"
        "DWT->CYCCNT")
    string(FIND "${FAULT_CAPTURE_PORT_CONTENT}"
        "${REQUIRED_FAULT_CAPTURE_MARKER}" FAULT_CAPTURE_INDEX)
    if(FAULT_CAPTURE_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 fault capture port is missing ${REQUIRED_FAULT_CAPTURE_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_RUNTIME_CONFIG_MARKER
        "configUSE_TRACE_FACILITY 1"
        "configGENERATE_RUN_TIME_STATS 1"
        "portCONFIGURE_TIMER_FOR_RUN_TIME_STATS"
        "portGET_RUN_TIME_COUNTER_VALUE"
        "traceTASK_SWITCHED_IN")
    string(FIND "${FREERTOS_CONFIG_CONTENT}"
        "${REQUIRED_RUNTIME_CONFIG_MARKER}" RUNTIME_CONFIG_INDEX)
    if(RUNTIME_CONFIG_INDEX EQUAL -1)
        message(FATAL_ERROR
            "FreeRTOS runtime config is missing ${REQUIRED_RUNTIME_CONFIG_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_CLI_MARKER
        "HAL_UART_Transmit_DMA"
        "HAL_UARTEx_ReceiveToIdle_DMA"
        "xTaskNotifyFromISR"
        "F407_CLI_RING_CAPACITY"
        "app_context_configure_cli")
    string(FIND "${CLI_PORT_CONTENT}"
        "${REQUIRED_CLI_MARKER}" CLI_PORT_INDEX)
    if(CLI_PORT_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 CLI port is missing ${REQUIRED_CLI_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_UI_MARKER
        "lcd_controller_detect"
        "lcd_controller_flush"
        "gt911_init"
        "F407_LCD_WIDTH"
        "f407_external_sram_self_test"
        "app_context_configure_ui")
    string(FIND "${DISPLAY_PORT_CONTENT}"
        "${REQUIRED_UI_MARKER}" DISPLAY_PORT_INDEX)
    if(DISPLAY_PORT_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 display port is missing ${REQUIRED_UI_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_LCD_BUS_MARKER
        "0x68000000u"
        "0x68000002u"
        "0x6C000000u"
        "FSMC_Bank1->BTCR[4]"
        "FSMC_Bank1->BTCR[6]"
        "GPIO_AF9_TIM14")
    string(FIND "${LCD_BUS_PORT_CONTENT}"
        "${REQUIRED_LCD_BUS_MARKER}" LCD_BUS_INDEX)
    if(LCD_BUS_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 LCD/SRAM port is missing ${REQUIRED_LCD_BUS_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_TOUCH_MARKER
        "GT911_SCL_PIN GPIO_PIN_7"
        "GT911_SDA_PIN GPIO_PIN_3"
        "GT911_RESET_PIN GPIO_PIN_6"
        "GT911_INT_PIN GPIO_PIN_8"
        "xTaskNotifyFromISR"
        "EXTI9_5_IRQHandler")
    string(FIND "${GT911_PORT_CONTENT}"
        "${REQUIRED_TOUCH_MARKER}" GT911_PORT_INDEX)
    if(GT911_PORT_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 GT911 port is missing ${REQUIRED_TOUCH_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_RTOS_UI_CLI_MARKER
        "q_ui_command"
        "config_mutex"
        "ui_subsystem_process"
        "ui_subsystem_take_input_activity"
        "PM_LOCK_UI_ACTIVE"
        "xTaskNotifyWait"
        "cli_subsystem_process"
        "alarm_subsystem_reconfigure"
        "storage_subsystem_save_config")
    string(FIND "${APP_RTOS_CONTENT}"
        "${REQUIRED_RTOS_UI_CLI_MARKER}" APP_RTOS_INDEX)
    if(APP_RTOS_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 RTOS UI/CLI/config route is missing ${REQUIRED_RTOS_UI_CLI_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_RTOS_RELIABILITY_MARKER
        "supervisor_subsystem_boot_confirm_ready"
        "GATEWAY_OTA_COMMAND_CONFIRM_BOOT"
        "boot_confirmation_queued"
        "boot_confirmation_confirm"
        "PM_LOCK_FLASH_WRITE"
        "CLI_COMMAND_RTOS_RUNTIME"
        "CLI_COMMAND_RTOS_TIMING"
        "periodic_timing_monitor_note"
        "storage_subsystem_archive_fault"
        "fault_recorder_inject"
        "SYSTEM_EVENT_POWER_QUIESCE_REQUEST"
        "SYSTEM_EVENT_POWER_ACK_STORAGE"
        "deep_power_controller_process"
        "DEEP_POWER_CONFIRMATION"
        "STORAGE_ECO_IDLE_MS"
        "critical_timing_monitor_enter")
    string(FIND "${APP_RTOS_CONTENT}"
        "${REQUIRED_RTOS_RELIABILITY_MARKER}" RTOS_RELIABILITY_INDEX)
    if(RTOS_RELIABILITY_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 RTOS reliability route is missing ${REQUIRED_RTOS_RELIABILITY_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_RTOS_OTA_MARKER
        "q_ota_network"
        "q_ota_storage"
        "ota_manager_check"
        "ota_manager_download"
        "ota_manager_commit_pending"
        "GATEWAY_OTA_COMMAND_APPLY")
    string(FIND "${APP_RTOS_CONTENT}"
        "${REQUIRED_RTOS_OTA_MARKER}" RTOS_OTA_INDEX)
    if(RTOS_OTA_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 RTOS OTA route is missing ${REQUIRED_RTOS_OTA_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_CRASH_ARCHIVE_MARKER
        "EXTERNAL_FLASH_PARTITION_CRASH"
        "STORAGE_REGION_CRASH"
        "fault_record_validate"
        "crash_duplicates"
        "scan_latest_fault")
    string(FIND "${STORAGE_SUBSYSTEM_CONTENT}"
        "${REQUIRED_CRASH_ARCHIVE_MARKER}" CRASH_ARCHIVE_INDEX)
    if(CRASH_ARCHIVE_INDEX EQUAL -1)
        message(FATAL_ERROR
            "Storage crash archive is missing ${REQUIRED_CRASH_ARCHIVE_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_TIMING_MARKER
        "release_tolerance_ms"
        "deadline_misses"
        "max_early_ms"
        "max_late_ms")
    string(FIND "${PERIODIC_TIMING_MONITOR_CONTENT}"
        "${REQUIRED_TIMING_MARKER}" TIMING_INDEX)
    if(TIMING_INDEX EQUAL -1)
        message(FATAL_ERROR
            "Periodic timing monitor is missing ${REQUIRED_TIMING_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_NETWORK_MARKER
        "HAL_UART_Transmit_DMA"
        "HAL_UARTEx_ReceiveToIdle_DMA"
        "xTaskNotifyFromISR"
        "MX_USART3_UART_Init"
        "app_context_configure_network")
    string(FIND "${NETWORK_PORT_CONTENT}"
        "${REQUIRED_NETWORK_MARKER}" NETWORK_PORT_INDEX)
    if(NETWORK_PORT_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 network port is missing ${REQUIRED_NETWORK_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_CONTROL_STORAGE_MARKER
        "HAL_SPI_TransmitReceive"
        "RELAY_DO_GPIO_Port"
        "EXTERNAL_FLASH_JEDEC_ID"
        "app_context_configure_control_storage")
    string(FIND "${CONTROL_STORAGE_PORT_CONTENT}"
        "${REQUIRED_CONTROL_STORAGE_MARKER}" CONTROL_STORAGE_INDEX)
    if(CONTROL_STORAGE_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 control/storage port is missing ${REQUIRED_CONTROL_STORAGE_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_I2C_ROUTE_MARKER
        "gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7"
        "gpio.Alternate = GPIO_AF4_I2C1"
        "HAL_I2C_Init(&f407_i2c1)")
    string(FIND "${ACQUISITION_PORT_CONTENT}"
        "${REQUIRED_I2C_ROUTE_MARKER}" I2C_ROUTE_INDEX)
    if(I2C_ROUTE_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 I2C1 acquisition route is missing ${REQUIRED_I2C_ROUTE_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_ROUTE_MARKER
        "MX_SPI2_Init()"
        "MAX31865_CS_GPIO_Port"
        "MAX31865_CS_Pin")
    string(FIND "${ACQUISITION_PORT_CONTENT}" "${REQUIRED_ROUTE_MARKER}" ROUTE_INDEX)
    if(ROUTE_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 SPI2 acquisition route is missing ${REQUIRED_ROUTE_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_FIELDBUS_MARKER
        "HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12)"
        "gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9"
        "gpio.Alternate = GPIO_AF9_CAN1"
        "HAL_UART_Transmit_DMA"
        "HAL_UARTEx_ReceiveToIdle_DMA"
        "xTaskNotifyFromISR"
        "HAL_CAN_ActivateNotification")
    string(FIND "${FIELDBUS_PORT_CONTENT}"
        "${REQUIRED_FIELDBUS_MARKER}" FIELDBUS_INDEX)
    if(FIELDBUS_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 fieldbus port is missing ${REQUIRED_FIELDBUS_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_CS_MARKER
        "#define MAX31865_CS_Pin GPIO_PIN_4"
        "#define MAX31865_CS_GPIO_Port GPIOA")
    string(FIND "${GENERATED_MAIN_CONTENT}" "${REQUIRED_CS_MARKER}" CS_INDEX)
    if(CS_INDEX EQUAL -1)
        message(FATAL_ERROR "F407 generated CS mapping is missing ${REQUIRED_CS_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_RS485_MARKER
        "#define RS485_DE_Pin GPIO_PIN_0"
        "#define RS485_DE_GPIO_Port GPIOC")
    string(FIND "${GENERATED_MAIN_CONTENT}" "${REQUIRED_RS485_MARKER}"
        RS485_INDEX)
    if(RS485_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 generated RS485 mapping is missing ${REQUIRED_RS485_MARKER}")
    endif()
endforeach()

foreach(REQUIRED_CONTROL_STORAGE_GPIO
        "#define RELAY_DO_Pin GPIO_PIN_2"
        "#define RELAY_DO_GPIO_Port GPIOG"
        "#define W25Q128_CS_Pin GPIO_PIN_6"
        "#define W25Q128_CS_GPIO_Port GPIOG")
    string(FIND "${GENERATED_MAIN_CONTENT}"
        "${REQUIRED_CONTROL_STORAGE_GPIO}" CONTROL_STORAGE_GPIO_INDEX)
    if(CONTROL_STORAGE_GPIO_INDEX EQUAL -1)
        message(FATAL_ERROR
            "F407 generated control/storage mapping is missing ${REQUIRED_CONTROL_STORAGE_GPIO}")
    endif()
endforeach()

message(STATUS
    "verified F407 vectors, Flash ranges, RTOS, reliability, deep-power barrier, acquisition, fieldbus, alarm, storage, MQTT, online OTA, UI, CLI, and config routes")
