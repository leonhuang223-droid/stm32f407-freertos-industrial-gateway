#ifndef BOOT_RESET_H
#define BOOT_RESET_H

#include "boot_metadata.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_RCC_CSR_PINRSTF (1u << 26)
#define BOOT_RCC_CSR_PORRSTF (1u << 27)
#define BOOT_RCC_CSR_SFTRSTF (1u << 28)
#define BOOT_RCC_CSR_IWDGRSTF (1u << 29)
#define BOOT_RCC_CSR_WWDGRSTF (1u << 30)
#define BOOT_RCC_CSR_LPWRRSTF (1u << 31)

boot_reset_reason_t boot_reset_reason_from_rcc_csr(uint32_t csr);

#ifdef __cplusplus
}
#endif

#endif
