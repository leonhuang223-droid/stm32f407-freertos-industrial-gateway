include_guard(GLOBAL)

function(elf_vector_vma_check OBJDUMP_OUTPUT EXPECTED_VMA OUT_OK OUT_DETAIL)
    string(REPLACE "\r\n" "\n" NORMALIZED_OUTPUT "${OBJDUMP_OUTPUT}")
    string(REPLACE "\r" "\n" NORMALIZED_OUTPUT "${NORMALIZED_OUTPUT}")
    string(REPLACE "\n" ";" OBJDUMP_LINES "${NORMALIZED_OUTPUT}")
    math(EXPR EXPECTED_VMA_VALUE "${EXPECTED_VMA}")

    set(VECTOR_FOUND FALSE)
    set(VECTOR_OK FALSE)
    set(VECTOR_DETAIL ".isr_vector section not found")
    foreach(OBJDUMP_LINE IN LISTS OBJDUMP_LINES)
        if(OBJDUMP_LINE MATCHES
                "^[ \t]*[0-9]+[ \t]+\\.isr_vector[ \t]+[0-9A-Fa-f]+[ \t]+([0-9A-Fa-f]+)[ \t]+([0-9A-Fa-f]+)[ \t]+")
            set(VECTOR_FOUND TRUE)
            math(EXPR VECTOR_VMA_VALUE "0x${CMAKE_MATCH_1}")
            if(VECTOR_VMA_VALUE EQUAL EXPECTED_VMA_VALUE)
                set(VECTOR_OK TRUE)
                set(VECTOR_DETAIL
                    ".isr_vector VMA matches 0x${CMAKE_MATCH_1}")
            else()
                set(VECTOR_DETAIL
                    ".isr_vector VMA 0x${CMAKE_MATCH_1} does not match expected ${EXPECTED_VMA}; LMA is not accepted as a substitute")
            endif()
            break()
        endif()
    endforeach()

    if(NOT VECTOR_FOUND)
        set(VECTOR_OK FALSE)
    endif()
    set(${OUT_OK} "${VECTOR_OK}" PARENT_SCOPE)
    set(${OUT_DETAIL} "${VECTOR_DETAIL}" PARENT_SCOPE)
endfunction()

function(elf_flash_range_check OBJDUMP_OUTPUT FLASH_START FLASH_LIMIT OUT_OK OUT_DETAIL)
    string(REPLACE "\r\n" "\n" NORMALIZED_OUTPUT "${OBJDUMP_OUTPUT}")
    string(REPLACE "\r" "\n" NORMALIZED_OUTPUT "${NORMALIZED_OUTPUT}")
    string(REPLACE "\n" ";" OBJDUMP_LINES "${NORMALIZED_OUTPUT}")

    set(PENDING_SECTION "")
    set(PENDING_SIZE 0)
    set(PENDING_VMA 0)
    set(PENDING_LMA 0)
    set(RANGE_OK TRUE)
    set(RANGE_DETAIL "no loadable FLASH sections found")
    set(LOAD_SECTION_COUNT 0)
    set(FLASH_SECTION_COUNT 0)

    math(EXPR FLASH_START_VALUE "${FLASH_START}")
    math(EXPR FLASH_LIMIT_VALUE "${FLASH_LIMIT}")
    set(INTERNAL_FLASH_BASE 0x08000000)
    if(DEFINED ELF_INTERNAL_FLASH_END)
        set(INTERNAL_FLASH_END "${ELF_INTERNAL_FLASH_END}")
    else()
        set(INTERNAL_FLASH_END 0x08080000)
    endif()

    foreach(OBJDUMP_LINE IN LISTS OBJDUMP_LINES)
        if(OBJDUMP_LINE MATCHES
                "^[ \t]*[0-9]+[ \t]+([^ \t]+)[ \t]+([0-9A-Fa-f]+)[ \t]+([0-9A-Fa-f]+)[ \t]+([0-9A-Fa-f]+)[ \t]+")
            set(PENDING_SECTION "${CMAKE_MATCH_1}")
            math(EXPR PENDING_SIZE "0x${CMAKE_MATCH_2}")
            math(EXPR PENDING_VMA "0x${CMAKE_MATCH_3}")
            math(EXPR PENDING_LMA "0x${CMAKE_MATCH_4}")
            continue()
        endif()

        if(PENDING_SECTION AND
                OBJDUMP_LINE MATCHES "CONTENTS,[ \t]*ALLOC,[ \t]*LOAD")
            math(EXPR LOAD_SECTION_COUNT "${LOAD_SECTION_COUNT} + 1")
            foreach(ADDRESS_KIND VMA LMA)
                if(ADDRESS_KIND STREQUAL "VMA")
                    set(SECTION_ADDRESS "${PENDING_VMA}")
                else()
                    set(SECTION_ADDRESS "${PENDING_LMA}")
                endif()

                if(SECTION_ADDRESS GREATER_EQUAL INTERNAL_FLASH_BASE AND
                        SECTION_ADDRESS LESS INTERNAL_FLASH_END)
                    math(EXPR FLASH_SECTION_COUNT "${FLASH_SECTION_COUNT} + 1")
                    math(EXPR SECTION_END "${SECTION_ADDRESS} + ${PENDING_SIZE}")
                    if(SECTION_ADDRESS LESS FLASH_START_VALUE OR
                            SECTION_END GREATER FLASH_LIMIT_VALUE)
                        set(RANGE_OK FALSE)
                        set(RANGE_DETAIL
                            "${PENDING_SECTION} ${ADDRESS_KIND} load range 0x${SECTION_ADDRESS}..0x${SECTION_END} crosses the application descriptor boundary 0x${FLASH_LIMIT_VALUE}")
                        break()
                    endif()
                    set(RANGE_DETAIL
                        "loadable FLASH sections remain below descriptor boundary")
                endif()
            endforeach()
            if(NOT RANGE_OK)
                break()
            endif()
        endif()

        set(PENDING_SECTION "")
    endforeach()

    if(LOAD_SECTION_COUNT EQUAL 0 OR FLASH_SECTION_COUNT EQUAL 0)
        set(RANGE_OK FALSE)
    endif()

    set(${OUT_OK} "${RANGE_OK}" PARENT_SCOPE)
    set(${OUT_DETAIL} "${RANGE_DETAIL}" PARENT_SCOPE)
endfunction()
