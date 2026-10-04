include("${PROJECT_SOURCE_DIR}/firmware/app/rtos/CMakeLists.txt")
add_executable(test_rtos_runtime
    test_rtos_runtime.c mocks/mock_rtos.c ${APP_RTOS_SOURCES})
target_link_libraries(test_rtos_runtime PRIVATE gateway_architecture)
target_include_directories(test_rtos_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mocks"
    "${PROJECT_SOURCE_DIR}/firmware/app/rtos/include"
    "${PROJECT_SOURCE_DIR}/firmware/app/rtos/src")
target_compile_options(test_rtos_runtime PRIVATE -Wall -Wextra -Werror)
add_test(NAME rtos_runtime.host COMMAND test_rtos_runtime)
