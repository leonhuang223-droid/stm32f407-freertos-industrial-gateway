if(NOT DEFINED SIZE_TOOL OR
   NOT DEFINED ELF_FILE OR
   NOT DEFINED IMAGE OR
   NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "SIZE_TOOL, ELF_FILE, IMAGE, and OUTPUT_FILE are required")
endif()

execute_process(
    COMMAND "${SIZE_TOOL}" "${ELF_FILE}"
    RESULT_VARIABLE SIZE_RESULT
    OUTPUT_VARIABLE SIZE_OUTPUT
    ERROR_VARIABLE SIZE_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT SIZE_RESULT EQUAL 0)
    message(FATAL_ERROR "arm-none-eabi-size failed: ${SIZE_ERROR}")
endif()

string(REGEX MATCH
    "[\r\n][ \t]*([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9a-fA-F]+)[ \t]+[^\r\n]+$"
    SIZE_LINE
    "${SIZE_OUTPUT}")
if(NOT SIZE_LINE)
    message(FATAL_ERROR "unable to parse arm-none-eabi-size output: ${SIZE_OUTPUT}")
endif()

set(TEXT_SIZE "${CMAKE_MATCH_1}")
set(DATA_SIZE "${CMAKE_MATCH_2}")
set(BSS_SIZE "${CMAKE_MATCH_3}")
set(DEC_SIZE "${CMAKE_MATCH_4}")
string(TOLOWER "${CMAKE_MATCH_5}" HEX_SIZE)

math(EXPR COMPUTED_DEC "${TEXT_SIZE} + ${DATA_SIZE} + ${BSS_SIZE}")
if(NOT COMPUTED_DEC EQUAL DEC_SIZE)
    message(FATAL_ERROR
        "size total mismatch: reported ${DEC_SIZE}, computed ${COMPUTED_DEC}")
endif()

file(WRITE "${OUTPUT_FILE}"
    "{\n"
    "  \"image\": \"${IMAGE}\",\n"
    "  \"text\": ${TEXT_SIZE},\n"
    "  \"data\": ${DATA_SIZE},\n"
    "  \"bss\": ${BSS_SIZE},\n"
    "  \"dec\": ${DEC_SIZE},\n"
    "  \"hex\": \"0x${HEX_SIZE}\"\n"
    "}\n")
