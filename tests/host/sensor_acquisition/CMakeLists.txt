add_executable(test_sensor_acquisition
    test_sensor_acquisition.c
)

target_link_libraries(test_sensor_acquisition PRIVATE gateway_architecture)

add_test(NAME sensor_acquisition.host COMMAND test_sensor_acquisition)
