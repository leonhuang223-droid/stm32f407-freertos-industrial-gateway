add_executable(test_display_touch test_display_touch.c)

target_link_libraries(test_display_touch PRIVATE display_touch_stack)
target_compile_options(test_display_touch PRIVATE -Wall -Wextra -Werror)

add_test(NAME display_touch.host COMMAND test_display_touch)
