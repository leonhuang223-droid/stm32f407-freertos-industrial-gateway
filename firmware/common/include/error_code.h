#ifndef ERROR_CODE_H
#define ERROR_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Project-wide status values shared by host tests, bootloader, and app code.
 */
typedef enum {
    SYS_OK = 0,       /**< Operation completed successfully. */
    ERR_INVALID_ARG,  /**< Null pointer, invalid enum, or out-of-range value. */
    ERR_TIMEOUT,      /**< Transport, peripheral, or RTOS wait timed out. */
    ERR_CRC,          /**< CRC32 check failed. */
    ERR_SHA256,       /**< SHA256 check failed. */
    ERR_FLASH_ERASE,  /**< Internal or external flash erase failed. */
    ERR_FLASH_WRITE,  /**< Flash program operation failed. */
    ERR_FLASH_VERIFY, /**< Post-write verification failed. */
    ERR_METADATA_INVALID, /**< Boot metadata is missing or inconsistent. */
    ERR_IMAGE_INVALID, /**< Image header, vector table, or body is invalid. */
    ERR_SLOT_MISMATCH, /**< Image/header slot does not match expected slot. */
    ERR_WIFI,          /**< ESP8266 or Wi-Fi command failed. */
    ERR_HTTP,          /**< Raw HTTP request or response failed. */
    ERR_OTA_ABORTED, /**< OTA operation was cancelled or could not continue. */
    ERR_NO_MEMORY,   /**< Static resource creation or pool allocation failed. */
    ERR_QUEUE_FULL,  /**< FreeRTOS queue/stream could not accept data. */
    ERR_UNSUPPORTED, /**< Feature or command is not supported in this build. */
    ERR_IO, /**< HAL or transport reported a non-timeout I/O failure. */
    ERR_DEVICE_NOT_READY, /**< Device is uninitialized, suspended, or busy. */
    ERR_SENSOR_FAULT, /**< Sensor reported a physical input or wiring fault. */
    ERR_RESET_REQUIRED, /**< Operation requires a controlled system reset. */
    ERR_PROTOCOL,   /**< A fieldbus frame or protocol response is malformed. */
    ERR_BUS_OFF,    /**< CAN controller entered the bus-off state. */
    ERR_IN_PROGRESS /**< Accepted operation needs another bounded service step.
                     */
} status_t;

/**
 * @brief Convert a status value to a stable diagnostic string.
 * @param status Status code to format.
 * @return Static string owned by the module.
 */
const char *error_to_string(status_t status);

#ifdef __cplusplus
}
#endif

#endif
