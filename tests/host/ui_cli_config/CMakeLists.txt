add_executable(test_ui_cli_config test_ui_cli_config.c)

target_link_libraries(test_ui_cli_config PRIVATE ui_cli_stack)
target_compile_options(test_ui_cli_config PRIVATE -Wall -Wextra -Werror)

add_test(NAME ui_cli_config COMMAND test_ui_cli_config)
