#ifndef APP_RUNTIME_INTERNAL_H
#define APP_RUNTIME_INTERNAL_H
#include "app_rtos.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "platform_constants.h"
#include "version.h"
#include "request_lifecycle.h"
#include "config_transaction_service.h"
#include "task_contexts.h"

#define MEASUREMENT_QUEUE_LENGTH 16U
#define CAN_TX_QUEUE_LENGTH 8U
#define UI_SNAPSHOT_QUEUE_LENGTH 1U
#define NETWORK_TELEMETRY_QUEUE_LENGTH 1U
#define NETWORK_ALARM_QUEUE_LENGTH 8U
#define NETWORK_CONTROL_QUEUE_LENGTH 4U
#define NETWORK_CONTROL_RESULT_QUEUE_LENGTH 4U
#define OTA_COMMAND_QUEUE_LENGTH 4U
#define STORAGE_LOG_QUEUE_LENGTH 32U
#define STORAGE_ALARM_QUEUE_LENGTH 8U
#define STORAGE_CONFIG_QUEUE_LENGTH 4U
#define UI_COMMAND_QUEUE_LENGTH 8U
#define OTA_NETWORK_REQUEST_QUEUE_LENGTH 1U
#define OTA_STORAGE_REQUEST_QUEUE_LENGTH 1U
#define POWER_COMMAND_QUEUE_LENGTH 4U
#define STORAGE_QUEUE_SET_LENGTH                                               \
    (STORAGE_LOG_QUEUE_LENGTH + STORAGE_ALARM_QUEUE_LENGTH +                   \
     STORAGE_CONFIG_QUEUE_LENGTH + OTA_STORAGE_REQUEST_QUEUE_LENGTH)
#define UI_ACTIVE_LOCK_HOLD_MS 3000u
#define APP_RTOS_SYSTEM_TASK_CAPACITY 16u
#define OTA_IO_NOTIFY_DONE (1UL << 0)
#define OTA_IO_WAIT_SLICE_MS 100u
#define OTA_NETWORK_IO_TIMEOUT_MS 60000u
#define OTA_STORAGE_IO_TIMEOUT_MS 5000u
#define STORAGE_ECO_IDLE_MS 5000u

#define SYSTEM_EVENT_STORAGE_READY (1UL << 0)
#define SYSTEM_EVENT_ACQUISITION_READY (1UL << 1)
#define SYSTEM_EVENT_FIELDBUS_READY (1UL << 2)
#define SYSTEM_EVENT_CONTROL_READY (1UL << 3)
#define SYSTEM_EVENT_FAULT_ACTIVE (1UL << 4)
#define SYSTEM_EVENT_ALARM_ACTIVE (1UL << 5)
#define SYSTEM_EVENT_NETWORK_UP (1UL << 6)
#define SYSTEM_EVENT_MQTT_READY (1UL << 7)
#define SYSTEM_EVENT_OTA_ACTIVE (1UL << 8)
#define SYSTEM_EVENT_UI_READY (1UL << 9)
#define SYSTEM_EVENT_CLI_READY (1UL << 10)
#define SYSTEM_EVENT_POWER_QUIESCE_REQUEST (1UL << 11)
#define SYSTEM_EVENT_POWER_ACK_ACQUISITION (1UL << 12)
#define SYSTEM_EVENT_POWER_ACK_MODBUS (1UL << 13)
#define SYSTEM_EVENT_POWER_ACK_CAN (1UL << 14)
#define SYSTEM_EVENT_POWER_ACK_NETWORK (1UL << 15)
#define SYSTEM_EVENT_POWER_ACK_STORAGE (1UL << 16)
#define SYSTEM_EVENT_POWER_ACK_UI (1UL << 17)
#define SYSTEM_EVENT_POWER_ACK_MASK                                            \
    (SYSTEM_EVENT_POWER_ACK_ACQUISITION | SYSTEM_EVENT_POWER_ACK_MODBUS |      \
     SYSTEM_EVENT_POWER_ACK_CAN | SYSTEM_EVENT_POWER_ACK_NETWORK |             \
     SYSTEM_EVENT_POWER_ACK_STORAGE | SYSTEM_EVENT_POWER_ACK_UI)

typedef enum {
    OTA_NETWORK_FETCH_MANIFEST = 0,
    OTA_NETWORK_OPEN_PACKAGE,
    OTA_NETWORK_READ_PACKAGE,
    OTA_NETWORK_CLOSE_PACKAGE
} ota_network_operation_t;

typedef struct {
    ota_network_operation_t operation;
    const char *url;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    uint32_t content_length;
    status_t status;
    TaskHandle_t waiter;
    request_lifecycle_t lifecycle;
} ota_network_request_t;

typedef enum {
    OTA_STORAGE_BEGIN = 0,
    OTA_STORAGE_ERASE_NEXT,
    OTA_STORAGE_WRITE,
    OTA_STORAGE_COMMIT_METADATA
} ota_storage_operation_t;

typedef struct {
    ota_storage_operation_t operation;
    uint32_t offset;
    const uint8_t *data;
    size_t length;
    uint8_t complete;
    status_t status;
    TaskHandle_t waiter;
    request_lifecycle_t lifecycle;
} ota_storage_request_t;

/* Queue-owned mailboxes survive a caller timeout. Workers never access an
 * OTA stack buffer. A timed-out mailbox cannot be reused until completion. */

typedef struct {
    struct app_ota_task_context *context;
    uint8_t cancel_seen;
} ota_rtos_port_t;

typedef enum {
    POWER_COMMAND_REQUEST_STOP = 0,
    POWER_COMMAND_REQUEST_STANDBY,
    POWER_COMMAND_CANCEL
} power_command_operation_t;

typedef struct {
    power_command_operation_t operation;
    uint32_t duration_ms;
} power_command_t;

typedef struct {
    QueueHandle_t measurement;
    QueueHandle_t can_tx;
    QueueHandle_t ui_snapshot;
    QueueHandle_t network_telemetry;
    QueueHandle_t network_alarm;
    QueueHandle_t network_control;
    QueueHandle_t network_control_result;
    QueueHandle_t ota_command;
    QueueHandle_t storage_log;
    QueueHandle_t storage_alarm;
    QueueHandle_t storage_config;
    QueueHandle_t ota_network_request;
    QueueHandle_t ota_storage_request;
    QueueHandle_t ui_command;
    QueueHandle_t power_command;
    QueueSetHandle_t storage_set;
    EventGroupHandle_t system_events;
    SemaphoreHandle_t snapshot_mutex;
    SemaphoreHandle_t config_mutex;
} app_channel_handles_t;

typedef struct {
    uint8_t fault_archive_complete;
    uint8_t power_suspended;
    uint8_t mount_attempted;
    uint32_t last_activity_ms;
    uint32_t last_mount_ms;
} app_storage_task_state_t;

typedef struct {
    uint8_t configured;
    uint8_t ota_lock_held;
    uint8_t power_suspended;
    status_t http_status;
} app_network_task_state_t;

void app_storage_task_step(app_storage_task_context_t *context,
                           app_storage_task_state_t *state,
                           uint32_t wait_ms);
void app_network_task_init(app_network_task_context_t *context,
                           app_network_task_state_t *state);
void app_network_task_step(app_network_task_context_t *context,
                           app_network_task_state_t *state);

extern app_channel_handles_t channels;
extern TaskHandle_t task_handles[GATEWAY_TASK_COUNT];
const char *app_runtime_task_name(gateway_task_id_t task);
void app_runtime_mark_alive(gateway_task_id_t task);
void supervisor_task(void *argument);
void acquisition_task(void *argument);
void data_hub_task(void *argument);
void modbus_task(void *argument);
void can_task(void *argument);
void network_task(void *argument);
void storage_task(void *argument);
void ota_poll_cancel_commands(ota_rtos_port_t *port);
void ota_task(void *argument);
void ui_task(void *argument);
void cli_task(void *argument);
void notify_ota_waiter(TaskHandle_t waiter);
void ota_network_request_complete(ota_network_request_t *request,
                                  status_t status);
void ota_storage_request_complete(ota_storage_request_t *request,
                                  status_t status);
status_t ota_port_fetch_manifest(void *opaque,
                                 const char *url,
                                 uint8_t *buffer,
                                 size_t capacity,
                                 size_t *out_length);
status_t
ota_port_http_open(void *opaque, const char *url, uint32_t *out_content_length);
status_t ota_port_http_read(void *opaque,
                            uint8_t *buffer,
                            size_t capacity,
                            size_t *out_length);
status_t ota_port_http_close(void *opaque);
status_t ota_port_staging_begin(void *opaque, size_t package_size);
status_t ota_port_staging_write(void *opaque,
                                uint32_t offset,
                                const uint8_t *data,
                                size_t length);
status_t ota_port_metadata_load(void *opaque,
                                boot_metadata_t *out_metadata,
                                app_slot_t *out_copy_slot);
status_t ota_port_metadata_commit(void *opaque,
                                  const boot_meta_commit_request_t *parameters);
status_t ota_port_staging_metadata_commit(void *opaque,
                                          const uint8_t *record,
                                          size_t record_size);
void ota_port_enter_critical(void *opaque);
void ota_port_exit_critical(void *opaque);
status_t submit_config_patch(const config_patch_t *patch, uint32_t *request_id);
status_t acknowledge_alarm(uint32_t event_id);
status_t app_config_read(gateway_runtime_config_t *config,
                         config_health_t *health,
                         uint32_t timeout_ms);
status_t
app_config_persist_request(const gateway_storage_config_request_t *request,
                           config_persist_fn persist,
                           void *persist_context);
status_t app_runtime_read_snapshot(gateway_system_snapshot_t *snapshot);
uint32_t deep_power_quiesced_mask(EventBits_t bits);
void app_critical_enter(void);
void app_critical_exit(void);
status_t power_lock_acquire(power_lock_id_t lock);
status_t power_lock_release(power_lock_id_t lock);
void collect_rtos_diagnostics(void);
status_t app_runtime_read_diagnostics(app_rtos_diagnostics_t *diagnostics);
const char *task_name_from_token(uint32_t task_token);
const char *power_mode_name(power_mode_t mode);
const char *power_policy_name(power_policy_t policy);
#endif
