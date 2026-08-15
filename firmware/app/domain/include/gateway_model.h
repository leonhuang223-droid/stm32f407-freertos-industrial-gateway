#ifndef GATEWAY_MODEL_H
#define GATEWAY_MODEL_H

#include "error_code.h"

#include <stdint.h>

typedef enum {
    GATEWAY_QUALITY_GOOD = 0,
    GATEWAY_QUALITY_STALE,
    GATEWAY_QUALITY_COMM_ERROR,
    GATEWAY_QUALITY_OUT_OF_RANGE,
    GATEWAY_QUALITY_SENSOR_FAULT,
    GATEWAY_QUALITY_UNAVAILABLE
} gateway_quality_t;

typedef enum {
    GATEWAY_SOURCE_ADS1115 = 0,
    GATEWAY_SOURCE_MAX31865,
    GATEWAY_SOURCE_SHT30,
    GATEWAY_SOURCE_MODBUS,
    GATEWAY_SOURCE_CAN
} gateway_measurement_source_t;

typedef enum {
    GATEWAY_UNIT_MICROAMP = 0,
    GATEWAY_UNIT_MILLICELSIUS,
    GATEWAY_UNIT_MILLIPERCENT_RH,
    GATEWAY_UNIT_RAW
} gateway_measurement_unit_t;

enum {
    GATEWAY_POINT_LOOP_CURRENT = 1001,
    GATEWAY_POINT_PT100_TEMPERATURE = 1002,
    GATEWAY_POINT_AMBIENT_TEMPERATURE = 1003,
    GATEWAY_POINT_RELATIVE_HUMIDITY = 1004,
    GATEWAY_POINT_MODBUS_PROCESS_VALUE = 2001,
    GATEWAY_POINT_CAN_REMOTE_VALUE = 3001
};

#define GATEWAY_MAX_ALARM_RULES 8u
#define GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION 1u

typedef struct {
    uint16_t point_id;
    uint8_t source;
    uint8_t unit;
    uint32_t sequence;
    uint32_t monotonic_ms;
    uint64_t wall_time_ms;
    int32_t raw_value;
    int32_t engineering_value;
    gateway_quality_t quality;
    status_t error;
} gateway_measurement_t;

typedef struct {
    uint32_t sequence;
    gateway_measurement_t latest;
    uint32_t active_alarm_count;
    uint32_t system_flags;
    uint8_t relay_energized;
} gateway_system_snapshot_t;

typedef enum {
    GATEWAY_ALARM_HIGH = 0,
    GATEWAY_ALARM_LOW,
    GATEWAY_ALARM_DATA_QUALITY
} gateway_alarm_type_t;

typedef enum {
    GATEWAY_ALARM_ENTERED = 0,
    GATEWAY_ALARM_RECOVERED,
    GATEWAY_ALARM_ACKNOWLEDGED
} gateway_alarm_transition_t;

typedef struct {
    uint32_t event_id;
    uint32_t measurement_sequence;
    uint32_t monotonic_ms;
    uint64_t wall_time_ms;
    uint16_t point_id;
    gateway_alarm_type_t type;
    gateway_alarm_transition_t transition;
    gateway_quality_t quality;
    int32_t threshold;
    int32_t value;
} gateway_alarm_event_t;

typedef struct {
    uint16_t point_id;
    uint8_t high_enabled;
    uint8_t low_enabled;
    uint8_t relay_on_alarm;
    uint8_t assert_samples;
    uint8_t recover_samples;
    int32_t high_threshold;
    int32_t low_threshold;
    int32_t hysteresis;
} gateway_alarm_rule_config_t;

typedef struct {
    uint32_t schema_version;
    uint32_t revision;
    uint8_t relay_safe_energized;
    uint8_t rule_count;
    gateway_alarm_rule_config_t rules[GATEWAY_MAX_ALARM_RULES];
} gateway_runtime_config_t;

typedef struct {
    uint32_t sequence;
    gateway_measurement_t measurement;
} gateway_storage_log_request_t;

typedef struct {
    gateway_alarm_event_t event;
} gateway_storage_alarm_request_t;

typedef struct {
    uint32_t request_id;
    gateway_runtime_config_t config;
} gateway_storage_config_request_t;

typedef enum {
    GATEWAY_NETWORK_TELEMETRY = 0,
    GATEWAY_NETWORK_ALARM
} gateway_network_event_type_t;

typedef struct {
    gateway_network_event_type_t type;
    uint32_t sequence;
    uint8_t qos;
    union {
        gateway_measurement_t measurement;
        gateway_alarm_event_t alarm;
    } payload;
} gateway_network_event_t;

typedef struct {
    uint32_t request_id;
    gateway_measurement_t measurement;
} gateway_can_tx_message_t;

typedef enum {
    GATEWAY_OTA_COMMAND_CONFIRM_BOOT = 0,
    GATEWAY_OTA_COMMAND_CHECK,
    GATEWAY_OTA_COMMAND_START,
    GATEWAY_OTA_COMMAND_APPLY,
    GATEWAY_OTA_COMMAND_CANCEL
} gateway_ota_command_type_t;

typedef struct {
    gateway_ota_command_type_t type;
    uint32_t request_id;
} gateway_ota_command_t;

typedef enum {
    GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE = 0,
    GATEWAY_NETWORK_CONTROL_OTA_RELEASE
} gateway_network_control_type_t;

typedef struct {
    gateway_network_control_type_t type;
    uint32_t request_id;
} gateway_network_control_request_t;

typedef struct {
    gateway_network_control_type_t type;
    uint32_t request_id;
    status_t status;
} gateway_network_control_result_t;

#endif
