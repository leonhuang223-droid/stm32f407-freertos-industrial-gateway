#include "error_code.h"

const char *error_to_string(status_t status)
{
    switch (status) {
    case SYS_OK:
        return "SYS_OK";
    case ERR_INVALID_ARG:
        return "ERR_INVALID_ARG";
    case ERR_TIMEOUT:
        return "ERR_TIMEOUT";
    case ERR_CRC:
        return "ERR_CRC";
    case ERR_SHA256:
        return "ERR_SHA256";
    case ERR_FLASH_ERASE:
        return "ERR_FLASH_ERASE";
    case ERR_FLASH_WRITE:
        return "ERR_FLASH_WRITE";
    case ERR_FLASH_VERIFY:
        return "ERR_FLASH_VERIFY";
    case ERR_METADATA_INVALID:
        return "ERR_METADATA_INVALID";
    case ERR_IMAGE_INVALID:
        return "ERR_IMAGE_INVALID";
    case ERR_SLOT_MISMATCH:
        return "ERR_SLOT_MISMATCH";
    case ERR_WIFI:
        return "ERR_WIFI";
    case ERR_HTTP:
        return "ERR_HTTP";
    case ERR_OTA_ABORTED:
        return "ERR_OTA_ABORTED";
    case ERR_NO_MEMORY:
        return "ERR_NO_MEMORY";
    case ERR_QUEUE_FULL:
        return "ERR_QUEUE_FULL";
    case ERR_UNSUPPORTED:
        return "ERR_UNSUPPORTED";
    case ERR_IO:
        return "ERR_IO";
    case ERR_PROTOCOL:
        return "ERR_PROTOCOL";
    case ERR_BUS_OFF:
        return "ERR_BUS_OFF";
    case ERR_DEVICE_NOT_READY:
        return "ERR_DEVICE_NOT_READY";
    case ERR_SENSOR_FAULT:
        return "ERR_SENSOR_FAULT";
    case ERR_RESET_REQUIRED:
        return "ERR_RESET_REQUIRED";
    default:
        return "UNKNOWN";
    }
}
