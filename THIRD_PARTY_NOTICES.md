# Third-party notices

This repository vendors the components below so the firmware can be built
without downloading source code during configuration. Their original license
files remain next to the source and govern those components.

| Component | Version/source | License file |
|---|---|---|
| FreeRTOS Kernel | V11.3.0 | `third_party/freertos_kernel/LICENSE.md` |
| LVGL | v9.5.0 | `third_party/lvgl/LICENCE.txt` and `third_party/lvgl/COPYRIGHTS.md` |
| STM32F4 HAL subset | STM32CubeF4 V1.28.3 | `third_party/stm32f4_hal/LICENSE.md` |
| CMSIS and generated STM32 drivers | STM32CubeF4 V1.28.3 | License files under `firmware/platform/stm32f407/cubemx/generated/stm32f407_gateway/Drivers` |

LVGL development documentation, demos, examples, tests and font source assets
that do not participate in the firmware build are intentionally excluded from
Git. License files embedded under `third_party/lvgl/src` are retained.
