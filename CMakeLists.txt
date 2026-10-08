cmake_minimum_required(VERSION 3.20)

project(stm32f407_industrial_gateway C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_HOST_TESTS "Build host-side unit tests" ON)
option(BUILD_STM32F407_FIRMWARE "Build STM32F407ZG firmware images" OFF)

if(BUILD_HOST_TESTS AND BUILD_STM32F407_FIRMWARE)
    message(FATAL_ERROR "Enable only one build profile")
endif()
if(NOT BUILD_HOST_TESTS AND NOT BUILD_STM32F407_FIRMWARE)
    message(FATAL_ERROR "Enable BUILD_HOST_TESTS or BUILD_STM32F407_FIRMWARE")
endif()

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/app/ui/include")
target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    LV_KCONFIG_IGNORE)
target_compile_options(lvgl PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections)

add_subdirectory(firmware)

if(BUILD_HOST_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter)
    add_subdirectory(tests/host/gateway_architecture)
    add_subdirectory(tests/host/f407_boot_support)
    add_subdirectory(tests/host/sensor_acquisition)
    add_subdirectory(tests/host/fieldbus_protocol)
    add_subdirectory(tests/host/control_storage)
    add_subdirectory(tests/host/network_mqtt)
    add_subdirectory(tests/host/ui_cli_config)
    add_subdirectory(tests/host/display_touch)
    add_subdirectory(tests/host/reliability_power)
    add_subdirectory(tests/host/runtime_services)
    add_subdirectory(tests/host/rtos_runtime)
    add_subdirectory(tests/host/ota_update)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME embedded_c_style.python
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/embedded_c_audit.py")
        add_test(
            NAME firmware_tools.python
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_f407_fwtools.py")
        set_tests_properties(firmware_tools.python PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    endif()
endif()
