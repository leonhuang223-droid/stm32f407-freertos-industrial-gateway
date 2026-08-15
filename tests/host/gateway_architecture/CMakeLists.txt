add_executable(test_gateway_architecture
    test_gateway_architecture.c
)

target_link_libraries(test_gateway_architecture PRIVATE gateway_architecture)

add_test(NAME gateway_architecture.host COMMAND test_gateway_architecture)
