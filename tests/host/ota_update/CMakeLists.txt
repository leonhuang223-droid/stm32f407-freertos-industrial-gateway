add_executable(test_ota_pipeline
    test_ota_update.c
)

target_link_libraries(test_ota_pipeline PRIVATE ota_update_stack)
target_compile_options(test_ota_pipeline PRIVATE -Wall -Wextra -Werror)

add_test(NAME ota_update.host COMMAND test_ota_pipeline)
