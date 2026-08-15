set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_ARM_HINTS)
if(DEFINED ENV{ARM_GNU_TOOLCHAIN_ROOT} AND
   NOT "$ENV{ARM_GNU_TOOLCHAIN_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{ARM_GNU_TOOLCHAIN_ROOT}" ARM_GNU_TOOLCHAIN_ROOT)
    list(APPEND _ARM_HINTS "${ARM_GNU_TOOLCHAIN_ROOT}/bin")
endif()

find_program(ARM_GNU_GCC arm-none-eabi-gcc HINTS ${_ARM_HINTS} REQUIRED)
get_filename_component(ARM_GNU_BIN "${ARM_GNU_GCC}" DIRECTORY)
set(CMAKE_C_COMPILER "${ARM_GNU_GCC}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
find_program(CMAKE_AR arm-none-eabi-ar HINTS "${ARM_GNU_BIN}" REQUIRED)
find_program(CMAKE_NM arm-none-eabi-nm HINTS "${ARM_GNU_BIN}" REQUIRED)
find_program(CMAKE_OBJCOPY arm-none-eabi-objcopy HINTS "${ARM_GNU_BIN}" REQUIRED)
find_program(CMAKE_OBJDUMP arm-none-eabi-objdump HINTS "${ARM_GNU_BIN}" REQUIRED)
find_program(CMAKE_SIZE arm-none-eabi-size HINTS "${ARM_GNU_BIN}" REQUIRED)

foreach(_TOOL
        CMAKE_C_COMPILER CMAKE_ASM_COMPILER CMAKE_AR CMAKE_NM
        CMAKE_OBJCOPY CMAKE_OBJDUMP CMAKE_SIZE)
    if(NOT EXISTS "${${_TOOL}}")
        message(FATAL_ERROR "${_TOOL} does not exist: ${${_TOOL}}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_C_COMPILER}" --version
    RESULT_VARIABLE ARM_GCC_VERSION_RESULT
    OUTPUT_VARIABLE ARM_GCC_VERSION_OUTPUT
    ERROR_VARIABLE ARM_GCC_VERSION_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ARM_GCC_VERSION_RESULT EQUAL 0 OR
   NOT ARM_GCC_VERSION_OUTPUT MATCHES "14\\.3\\.1")
    message(FATAL_ERROR
        "Arm GNU Toolchain GCC 14.3.1 is required: ${ARM_GCC_VERSION_ERROR}")
endif()

set(F407_ARCH_FLAGS
    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT
    "${F407_ARCH_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${F407_ARCH_FLAGS}")
