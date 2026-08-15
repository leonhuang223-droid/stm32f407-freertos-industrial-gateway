add_executable(test_f407_boot_support
    test_f407_boot_support.c
    ${PROJECT_SOURCE_DIR}/firmware/platform/stm32f407/src/f407_flash.c
    ${PROJECT_SOURCE_DIR}/firmware/platform/stm32f407/src/w25q_boot.c
    ${PROJECT_SOURCE_DIR}/firmware/bootloader/src/boot_jump.c
)

target_include_directories(test_f407_boot_support PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware/platform/stm32f407/include
    ${PROJECT_SOURCE_DIR}/firmware/bootloader/include
)

target_compile_definitions(test_f407_boot_support PRIVATE STM32F407xx)
target_compile_options(test_f407_boot_support PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_f407_boot_support PRIVATE common)

add_test(NAME f407_boot_support.host COMMAND test_f407_boot_support)
