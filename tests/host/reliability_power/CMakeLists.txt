add_executable(test_reliability_power
    test_reliability_power.c
)

target_link_libraries(test_reliability_power PRIVATE gateway_architecture)

add_test(NAME reliability_power.host COMMAND test_reliability_power)
