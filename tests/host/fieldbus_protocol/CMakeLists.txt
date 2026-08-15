add_executable(test_fieldbus_protocol
    test_fieldbus_protocol.c
)

target_link_libraries(test_fieldbus_protocol PRIVATE fieldbus_stack)
target_compile_options(test_fieldbus_protocol PRIVATE -Wall -Wextra -Werror)

add_test(NAME fieldbus_protocol.host COMMAND test_fieldbus_protocol)
