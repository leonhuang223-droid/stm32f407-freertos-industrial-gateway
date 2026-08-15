add_executable(test_network_mqtt
    test_network_mqtt.c
)

target_link_libraries(test_network_mqtt PRIVATE network_stack)
target_compile_options(test_network_mqtt PRIVATE -Wall -Wextra -Werror)

add_test(NAME network_mqtt.host COMMAND test_network_mqtt)
