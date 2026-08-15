#ifndef PLATFORM_CONSTANTS_H
#define PLATFORM_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PROJECT_TARGET_ID "STM32F407ZGT6_GATEWAY_V1"
#define PROJECT_SRAM_START 0x20000000u
#define PROJECT_SRAM_SIZE (128u * 1024u)

#define PROJECT_SRAM_END (PROJECT_SRAM_START + PROJECT_SRAM_SIZE)

#ifdef __cplusplus
}
#endif

#endif
