add_executable(test_control_storage
    test_control_storage.c
)

target_link_libraries(test_control_storage PRIVATE gateway_architecture)
target_compile_options(test_control_storage PRIVATE -Wall -Wextra -Werror)

add_test(NAME control_storage.host COMMAND test_control_storage)
