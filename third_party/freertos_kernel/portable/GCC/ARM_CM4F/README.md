# FreeRTOS ARM_CM4F Port

These two port files are from the official FreeRTOS Kernel `V11.3.0` tag so the STM32F407 target uses the same kernel release as the existing repository sources.

- Source: `https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.0/portable/GCC/ARM_CM4F`
- `port.c` SHA-256: `415481BD5077833EC512E6A9A73D568AB0E96A48395FF67584B889FA6D017B8C`
- `portmacro.h` SHA-256: `CAFF90FCB520C57DE4C4B8FFDBD1A119384E972058BCEAB4E2FFC3BC6F4EAD44`

Do not replace these files with a port from another FreeRTOS release without rebuilding both F407 application slots and rerunning the artifact gates.
