#include "boot_reset.h"

boot_reset_reason_t boot_reset_reason_from_rcc_csr(uint32_t csr)
{
    if ((csr & BOOT_RCC_CSR_IWDGRSTF) != 0u) {
        return BOOT_RESET_REASON_IWDG;
    }
    if ((csr & BOOT_RCC_CSR_WWDGRSTF) != 0u) {
        return BOOT_RESET_REASON_WWDG;
    }
    if ((csr & BOOT_RCC_CSR_SFTRSTF) != 0u) {
        return BOOT_RESET_REASON_SOFTWARE;
    }
    if ((csr & BOOT_RCC_CSR_PORRSTF) != 0u) {
        return BOOT_RESET_REASON_POWER_ON;
    }
    if ((csr & BOOT_RCC_CSR_PINRSTF) != 0u) {
        return BOOT_RESET_REASON_PIN;
    }
    if ((csr & BOOT_RCC_CSR_LPWRRSTF) != 0u) {
        return BOOT_RESET_REASON_BROWNOUT;
    }
    return BOOT_RESET_REASON_UNKNOWN;
}
