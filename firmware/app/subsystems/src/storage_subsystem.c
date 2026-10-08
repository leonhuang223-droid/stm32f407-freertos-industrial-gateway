#include "storage_subsystem.h"

#include "crc32.h"

#include <string.h>

#define STORAGE_RECORD_MAGIC 0x31525747u
#define STORAGE_RECORD_VERSION 1u
#define STORAGE_CONFIG_MAGIC 0x31474643u
#define STORAGE_CONFIG_VERSION 1u
#define STORAGE_RECORD_CRC_OFFSET 124u
#define STORAGE_RECORD_PAYLOAD_OFFSET 28u
#define STORAGE_RECORD_PAYLOAD_CAPACITY 96u
#define STORAGE_CONFIG_CRC_OFFSET 252u
#define STORAGE_CONFIG_PAYLOAD_OFFSET 16u
#define STORAGE_CONFIG_PAYLOAD_CAPACITY 236u
#define STORAGE_FAULT_PAYLOAD_SIZE 76u
#define STORAGE_SLOTS_PER_SECTOR                                               \
    (EXTERNAL_FLASH_SECTOR_SIZE / STORAGE_RECORD_SIZE)

enum {
    STORAGE_WIRE_LOG = 1u,
    STORAGE_WIRE_ALARM = 2u,
    STORAGE_WIRE_CRASH = 3u,
    STORAGE_WIRE_REGION_HEADER = 0x80u
};

enum {
    STORAGE_REGION_RUNTIME_LOG = 1u,
    STORAGE_REGION_ALARM_LOG = 2u,
    STORAGE_REGION_CRASH = 3u
};

static int runtime_config_valid(const gateway_runtime_config_t *config)
{
    unsigned int i;

    if (config == 0 ||
        config->schema_version != GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION ||
        config->relay_safe_energized > 1u || config->rule_count == 0u ||
        config->rule_count > GATEWAY_MAX_ALARM_RULES) {
        return 0;
    }
    for (i = 0u; i < config->rule_count; ++i) {
        const gateway_alarm_rule_config_t *rule = &config->rules[i];
        unsigned int j;

        if (rule->point_id == 0u || rule->high_enabled > 1u ||
            rule->low_enabled > 1u || rule->relay_on_alarm > 1u ||
            (rule->high_enabled == 0u && rule->low_enabled == 0u) ||
            rule->assert_samples == 0u || rule->recover_samples == 0u ||
            rule->hysteresis < 0 ||
            (rule->high_enabled != 0u && rule->low_enabled != 0u &&
             rule->low_threshold >= rule->high_threshold)) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            if (config->rules[j].point_id == rule->point_id) {
                return 0;
            }
        }
    }
    return 1;
}

static void write_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8u);
    buffer[2] = (uint8_t)(value >> 16u);
    buffer[3] = (uint8_t)(value >> 24u);
}

static void write_u64(uint8_t *buffer, uint64_t value)
{
    write_u32(buffer, (uint32_t)value);
    write_u32(buffer + 4u, (uint32_t)(value >> 32u));
}

static uint16_t read_u16(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | (uint16_t)((uint16_t)buffer[1] << 8u);
}

static uint32_t read_u32(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8u) |
           ((uint32_t)buffer[2] << 16u) | ((uint32_t)buffer[3] << 24u);
}

static int generation_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

static int buffer_erased(const uint8_t *buffer, size_t length)
{
    size_t i;

    for (i = 0u; i < length; ++i) {
        if (buffer[i] != 0xffu) {
            return 0;
        }
    }
    return 1;
}

static status_t wire_crc_status(const uint8_t *wire, size_t crc_offset)
{
    uint32_t computed = 0u;
    status_t status = crc32_compute(wire, crc_offset, &computed);

    if (status != SYS_OK) {
        return status;
    }
    return computed == read_u32(&wire[crc_offset]) ? SYS_OK : ERR_CRC;
}

static status_t finalize_wire(uint8_t *wire, size_t crc_offset)
{
    uint32_t crc = 0u;
    status_t status = crc32_compute(wire, crc_offset, &crc);

    if (status == SYS_OK) {
        write_u32(&wire[crc_offset], crc);
    }
    return status;
}

/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    uint8_t flags;
    uint32_t generation;
    uint32_t sequence;
    uint64_t timestamp;
    const uint8_t *payload;
    size_t payload_length;
    uint8_t *wire;
} storage_record_input_t;

static status_t encode_record(uint8_t kind,
                              const storage_record_input_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    uint8_t flags = parameters->flags;
    uint32_t generation = parameters->generation;
    uint32_t sequence = parameters->sequence;
    uint64_t timestamp = parameters->timestamp;
    const uint8_t *payload = parameters->payload;
    size_t payload_length = parameters->payload_length;
    uint8_t *wire = parameters->wire;

    if (wire == 0 || payload_length > STORAGE_RECORD_PAYLOAD_CAPACITY ||
        (payload == 0 && payload_length != 0u)) {
        return ERR_INVALID_ARG;
    }
    memset(wire, 0xff, STORAGE_RECORD_SIZE);
    write_u32(&wire[0], STORAGE_RECORD_MAGIC);
    write_u16(&wire[4], STORAGE_RECORD_VERSION);
    wire[6] = kind;
    wire[7] = flags;
    write_u32(&wire[8], generation);
    write_u32(&wire[12], sequence);
    write_u64(&wire[16], timestamp);
    write_u16(&wire[24], (uint16_t)payload_length);
    write_u16(&wire[26], 0u);
    if (payload_length != 0u) {
        memcpy(&wire[STORAGE_RECORD_PAYLOAD_OFFSET], payload, payload_length);
    }
    return finalize_wire(wire, STORAGE_RECORD_CRC_OFFSET);
}

static status_t record_status(const uint8_t wire[STORAGE_RECORD_SIZE],
                              uint8_t expected_kind)
{
    if (read_u32(&wire[0]) != STORAGE_RECORD_MAGIC ||
        read_u16(&wire[4]) != STORAGE_RECORD_VERSION ||
        wire[6] != expected_kind ||
        read_u16(&wire[24]) > STORAGE_RECORD_PAYLOAD_CAPACITY) {
        return ERR_PROTOCOL;
    }
    return wire_crc_status(wire, STORAGE_RECORD_CRC_OFFSET);
}

static status_t
encode_fault_payload(const fault_record_t *record,
                     uint8_t payload[STORAGE_FAULT_PAYLOAD_SIZE])
{
    if (fault_record_validate(record) != SYS_OK) {
        return ERR_METADATA_INVALID;
    }
    write_u32(&payload[0], record->magic);
    write_u16(&payload[4], record->version);
    payload[6] = record->origin;
    payload[7] = record->reserved;
    write_u32(&payload[8], record->sequence);
    write_u32(&payload[12], record->reset_flags);
    write_u32(&payload[16], record->exception_return);
    write_u32(&payload[20], record->task_token);
    write_u32(&payload[24], record->stacked_r0);
    write_u32(&payload[28], record->stacked_r1);
    write_u32(&payload[32], record->stacked_r2);
    write_u32(&payload[36], record->stacked_r3);
    write_u32(&payload[40], record->stacked_r12);
    write_u32(&payload[44], record->stacked_lr);
    write_u32(&payload[48], record->stacked_pc);
    write_u32(&payload[52], record->stacked_xpsr);
    write_u32(&payload[56], record->cfsr);
    write_u32(&payload[60], record->hfsr);
    write_u32(&payload[64], record->mmfar);
    write_u32(&payload[68], record->bfar);
    write_u32(&payload[72], record->crc32);
    return SYS_OK;
}

static status_t decode_fault_payload(const uint8_t *payload,
                                     size_t length,
                                     fault_record_t *record)
{
    if (payload == 0 || record == 0 || length != STORAGE_FAULT_PAYLOAD_SIZE) {
        return ERR_PROTOCOL;
    }
    memset(record, 0, sizeof(*record));
    record->magic = read_u32(&payload[0]);
    record->version = read_u16(&payload[4]);
    record->origin = payload[6];
    record->reserved = payload[7];
    record->sequence = read_u32(&payload[8]);
    record->reset_flags = read_u32(&payload[12]);
    record->exception_return = read_u32(&payload[16]);
    record->task_token = read_u32(&payload[20]);
    record->stacked_r0 = read_u32(&payload[24]);
    record->stacked_r1 = read_u32(&payload[28]);
    record->stacked_r2 = read_u32(&payload[32]);
    record->stacked_r3 = read_u32(&payload[36]);
    record->stacked_r12 = read_u32(&payload[40]);
    record->stacked_lr = read_u32(&payload[44]);
    record->stacked_pc = read_u32(&payload[48]);
    record->stacked_xpsr = read_u32(&payload[52]);
    record->cfsr = read_u32(&payload[56]);
    record->hfsr = read_u32(&payload[60]);
    record->mmfar = read_u32(&payload[64]);
    record->bfar = read_u32(&payload[68]);
    record->crc32 = read_u32(&payload[72]);
    return fault_record_validate(record);
}

static status_t program_verified(storage_subsystem_t *subsystem,
                                 uint32_t address,
                                 const uint8_t *data,
                                 size_t length)
{
    uint8_t verify[STORAGE_CONFIG_WIRE_SIZE];
    status_t status;

    if (length > sizeof(verify)) {
        return ERR_INVALID_ARG;
    }
    status = storage_media_program(subsystem->media, address, data, length);
    if (status != SYS_OK) {
        subsystem->health.write_failures++;
        return status;
    }
    status = storage_media_read(subsystem->media, address, verify, length);
    if (status != SYS_OK || memcmp(verify, data, length) != 0) {
        subsystem->health.verify_failures++;
        return status == SYS_OK ? ERR_FLASH_VERIFY : status;
    }
    return SYS_OK;
}

static status_t erase_sector(storage_subsystem_t *subsystem, uint32_t address)
{
    status_t status = storage_media_erase_sector(subsystem->media, address);

    if (status == SYS_OK) {
        subsystem->health.sector_erases++;
    } else {
        subsystem->health.write_failures++;
    }
    return status;
}

static status_t write_region_header(storage_subsystem_t *subsystem,
                                    storage_region_cursor_t *region,
                                    uint32_t sector_index,
                                    uint32_t generation)
{
    uint8_t payload[1];
    uint8_t wire[STORAGE_RECORD_SIZE];
    uint32_t address =
        region->partition.start + sector_index * EXTERNAL_FLASH_SECTOR_SIZE;
    status_t status;

    payload[0] = region->region_id;
    status = encode_record(STORAGE_WIRE_REGION_HEADER,
                           &(const storage_record_input_t){region->region_id,
                                                           generation,
                                                           0u,
                                                           0u,
                                                           payload,
                                                           sizeof(payload),
                                                           wire});
    return status == SYS_OK
               ? program_verified(subsystem, address, wire, sizeof(wire))
               : status;
}

static status_t format_region(storage_subsystem_t *subsystem,
                              storage_region_cursor_t *region)
{
    status_t status = erase_sector(subsystem, region->partition.start);

    if (status == SYS_OK) {
        status = write_region_header(subsystem, region, 0u, 1u);
    }
    if (status == SYS_OK) {
        region->active_sector = 0u;
        region->generation = 1u;
        region->next_slot = 1u;
    }
    return status;
}

static status_t mount_region(storage_subsystem_t *subsystem,
                             storage_region_cursor_t *region)
{
    uint32_t sector_count = region->partition.size / EXTERNAL_FLASH_SECTOR_SIZE;
    uint32_t selected_sector = 0u;
    uint32_t selected_generation = 0u;
    uint32_t sector;
    int found = 0;

    for (sector = 0u; sector < sector_count; ++sector) {
        uint8_t wire[STORAGE_RECORD_SIZE];
        uint32_t address =
            region->partition.start + sector * EXTERNAL_FLASH_SECTOR_SIZE;
        status_t status =
            storage_media_read(subsystem->media, address, wire, sizeof(wire));

        if (status != SYS_OK) {
            return status;
        }
        if (buffer_erased(wire, sizeof(wire))) {
            continue;
        }
        if (record_status(wire, STORAGE_WIRE_REGION_HEADER) != SYS_OK ||
            wire[7] != region->region_id ||
            wire[STORAGE_RECORD_PAYLOAD_OFFSET] != region->region_id) {
            subsystem->health.mount_invalid_records++;
            continue;
        }
        if (!found ||
            generation_newer(read_u32(&wire[8]), selected_generation)) {
            selected_sector = sector;
            selected_generation = read_u32(&wire[8]);
            found = 1;
        }
    }
    if (!found) {
        return format_region(subsystem, region);
    }
    region->active_sector = selected_sector;
    region->generation = selected_generation;
    region->next_slot = 1u;
    while (region->next_slot < STORAGE_SLOTS_PER_SECTOR) {
        uint8_t wire[STORAGE_RECORD_SIZE];
        uint32_t address = region->partition.start +
                           selected_sector * EXTERNAL_FLASH_SECTOR_SIZE +
                           (uint32_t)region->next_slot * STORAGE_RECORD_SIZE;
        status_t status =
            storage_media_read(subsystem->media, address, wire, sizeof(wire));

        if (status != SYS_OK) {
            return status;
        }
        if (buffer_erased(wire, sizeof(wire))) {
            break;
        }
        if (wire_crc_status(wire, STORAGE_RECORD_CRC_OFFSET) != SYS_OK) {
            subsystem->health.mount_invalid_records++;
        }
        region->next_slot++;
    }
    return SYS_OK;
}

static status_t scan_latest_fault(storage_subsystem_t *subsystem)
{
    uint32_t sector_count =
        subsystem->crash_log.partition.size / EXTERNAL_FLASH_SECTOR_SIZE;
    uint32_t latest_generation = 0u;
    uint16_t latest_slot = 0u;
    uint32_t sector;
    int found = 0;

    memset(&subsystem->latest_fault, 0, sizeof(subsystem->latest_fault));
    subsystem->health.latest_crash_valid = 0u;
    subsystem->health.latest_crash_sequence = 0u;
    for (sector = 0u; sector < sector_count; ++sector) {
        uint8_t header[STORAGE_RECORD_SIZE];
        uint32_t sector_address = subsystem->crash_log.partition.start +
                                  sector * EXTERNAL_FLASH_SECTOR_SIZE;
        uint32_t generation;
        uint16_t slot;
        status_t status = storage_media_read(
            subsystem->media, sector_address, header, sizeof(header));

        if (status != SYS_OK) {
            return status;
        }
        if (buffer_erased(header, sizeof(header))) {
            continue;
        }
        if (record_status(header, STORAGE_WIRE_REGION_HEADER) != SYS_OK ||
            header[7] != STORAGE_REGION_CRASH ||
            header[STORAGE_RECORD_PAYLOAD_OFFSET] != STORAGE_REGION_CRASH) {
            continue;
        }
        generation = read_u32(&header[8]);
        for (slot = 1u; slot < STORAGE_SLOTS_PER_SECTOR; ++slot) {
            uint8_t wire[STORAGE_RECORD_SIZE];
            fault_record_t record;
            uint32_t address =
                sector_address + (uint32_t)slot * STORAGE_RECORD_SIZE;

            status = storage_media_read(
                subsystem->media, address, wire, sizeof(wire));
            if (status != SYS_OK) {
                return status;
            }
            if (buffer_erased(wire, sizeof(wire))) {
                break;
            }
            if (record_status(wire, STORAGE_WIRE_CRASH) != SYS_OK ||
                read_u32(&wire[8]) != generation ||
                decode_fault_payload(&wire[STORAGE_RECORD_PAYLOAD_OFFSET],
                                     read_u16(&wire[24]),
                                     &record) != SYS_OK) {
                subsystem->health.mount_invalid_records++;
                continue;
            }
            if (!found || generation_newer(generation, latest_generation) ||
                (generation == latest_generation && slot > latest_slot)) {
                subsystem->latest_fault = record;
                latest_generation = generation;
                latest_slot = slot;
                found = 1;
            }
        }
    }
    if (found) {
        subsystem->health.latest_crash_sequence =
            subsystem->latest_fault.sequence;
        subsystem->health.latest_crash_valid = 1u;
    }
    return SYS_OK;
}

static status_t rotate_region(storage_subsystem_t *subsystem,
                              storage_region_cursor_t *region)
{
    uint32_t sector_count = region->partition.size / EXTERNAL_FLASH_SECTOR_SIZE;
    uint32_t next_sector = (region->active_sector + 1u) % sector_count;
    uint32_t next_generation = region->generation + 1u;
    uint32_t address =
        region->partition.start + next_sector * EXTERNAL_FLASH_SECTOR_SIZE;
    status_t status = erase_sector(subsystem, address);

    if (status == SYS_OK) {
        status = write_region_header(
            subsystem, region, next_sector, next_generation);
    }
    if (status == SYS_OK) {
        region->active_sector = next_sector;
        region->generation = next_generation;
        region->next_slot = 1u;
    }
    return status;
}

static status_t append_record(storage_subsystem_t *subsystem,
                              storage_region_cursor_t *region,
                              uint8_t wire[STORAGE_RECORD_SIZE])
{
    uint32_t address;
    status_t status;

    if (region->next_slot >= STORAGE_SLOTS_PER_SECTOR) {
        status = rotate_region(subsystem, region);
        if (status != SYS_OK) {
            return status;
        }
        write_u32(&wire[8], region->generation);
        status = finalize_wire(wire, STORAGE_RECORD_CRC_OFFSET);
        if (status != SYS_OK) {
            return status;
        }
    }
    address = region->partition.start +
              region->active_sector * EXTERNAL_FLASH_SECTOR_SIZE +
              (uint32_t)region->next_slot * STORAGE_RECORD_SIZE;
    status = program_verified(subsystem, address, wire, STORAGE_RECORD_SIZE);
    region->next_slot++;
    return status;
}

static size_t
encode_measurement_payload(const gateway_measurement_t *measurement,
                           uint8_t *payload)
{
    write_u16(&payload[0], measurement->point_id);
    payload[2] = measurement->source;
    payload[3] = measurement->unit;
    write_u32(&payload[4], measurement->sequence);
    write_u32(&payload[8], measurement->monotonic_ms);
    write_u64(&payload[12], measurement->wall_time_ms);
    write_u32(&payload[20], (uint32_t)measurement->raw_value);
    write_u32(&payload[24], (uint32_t)measurement->engineering_value);
    payload[28] = (uint8_t)measurement->quality;
    write_u32(&payload[29], (uint32_t)measurement->error);
    return 33u;
}

static size_t encode_alarm_payload(const gateway_alarm_event_t *event,
                                   uint8_t *payload)
{
    write_u32(&payload[0], event->event_id);
    write_u32(&payload[4], event->measurement_sequence);
    write_u32(&payload[8], event->monotonic_ms);
    write_u64(&payload[12], event->wall_time_ms);
    write_u16(&payload[20], event->point_id);
    payload[22] = (uint8_t)event->type;
    payload[23] = (uint8_t)event->transition;
    payload[24] = (uint8_t)event->quality;
    write_u32(&payload[25], (uint32_t)event->threshold);
    write_u32(&payload[29], (uint32_t)event->value);
    return 33u;
}

static size_t encode_config_payload(const gateway_runtime_config_t *config,
                                    uint8_t *payload)
{
    unsigned int i;

    write_u32(&payload[0], config->schema_version);
    write_u32(&payload[4], config->revision);
    payload[8] = config->relay_safe_energized;
    payload[9] = config->rule_count;
    write_u16(&payload[10], 0u);
    for (i = 0u; i < config->rule_count; ++i) {
        const gateway_alarm_rule_config_t *rule = &config->rules[i];
        uint8_t *wire_rule = &payload[12u + i * 20u];

        write_u16(&wire_rule[0], rule->point_id);
        wire_rule[2] = (uint8_t)((rule->high_enabled != 0u ? 1u : 0u) |
                                 (rule->low_enabled != 0u ? 2u : 0u));
        wire_rule[3] = rule->assert_samples;
        wire_rule[4] = rule->recover_samples;
        wire_rule[5] = rule->relay_on_alarm;
        write_u16(&wire_rule[6], 0u);
        write_u32(&wire_rule[8], (uint32_t)rule->high_threshold);
        write_u32(&wire_rule[12], (uint32_t)rule->low_threshold);
        write_u32(&wire_rule[16], (uint32_t)rule->hysteresis);
    }
    return 12u + (size_t)config->rule_count * 20u;
}

static status_t decode_config_payload(const uint8_t *payload,
                                      size_t length,
                                      gateway_runtime_config_t *config)
{
    uint8_t rule_count;
    unsigned int i;

    if (payload == 0 || config == 0 || length < 12u) {
        return ERR_PROTOCOL;
    }
    rule_count = payload[9];
    if (read_u32(&payload[0]) != GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION ||
        rule_count == 0u || rule_count > GATEWAY_MAX_ALARM_RULES ||
        length != 12u + (size_t)rule_count * 20u) {
        return ERR_PROTOCOL;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = read_u32(&payload[0]);
    config->revision = read_u32(&payload[4]);
    config->relay_safe_energized = payload[8];
    config->rule_count = rule_count;
    for (i = 0u; i < rule_count; ++i) {
        gateway_alarm_rule_config_t *rule = &config->rules[i];
        const uint8_t *wire_rule = &payload[12u + i * 20u];

        rule->point_id = read_u16(&wire_rule[0]);
        rule->high_enabled = (wire_rule[2] & 1u) != 0u ? 1u : 0u;
        rule->low_enabled = (wire_rule[2] & 2u) != 0u ? 1u : 0u;
        rule->assert_samples = wire_rule[3];
        rule->recover_samples = wire_rule[4];
        rule->relay_on_alarm = wire_rule[5];
        rule->high_threshold = (int32_t)read_u32(&wire_rule[8]);
        rule->low_threshold = (int32_t)read_u32(&wire_rule[12]);
        rule->hysteresis = (int32_t)read_u32(&wire_rule[16]);
    }
    return runtime_config_valid(config) ? SYS_OK : ERR_PROTOCOL;
}

static status_t encode_config_wire(const gateway_runtime_config_t *config,
                                   uint32_t generation,
                                   uint8_t wire[STORAGE_CONFIG_WIRE_SIZE])
{
    size_t payload_length;

    if (wire == 0 || !runtime_config_valid(config)) {
        return ERR_INVALID_ARG;
    }
    memset(wire, 0xff, STORAGE_CONFIG_WIRE_SIZE);
    write_u32(&wire[0], STORAGE_CONFIG_MAGIC);
    write_u16(&wire[4], STORAGE_CONFIG_VERSION);
    write_u16(&wire[6], 0u);
    write_u32(&wire[8], generation);
    payload_length =
        encode_config_payload(config, &wire[STORAGE_CONFIG_PAYLOAD_OFFSET]);
    if (payload_length > STORAGE_CONFIG_PAYLOAD_CAPACITY) {
        return ERR_NO_MEMORY;
    }
    write_u16(&wire[12], (uint16_t)payload_length);
    write_u16(&wire[14], 0u);
    return finalize_wire(wire, STORAGE_CONFIG_CRC_OFFSET);
}

static status_t decode_config_wire(const uint8_t wire[STORAGE_CONFIG_WIRE_SIZE],
                                   uint32_t *generation,
                                   gateway_runtime_config_t *config)
{
    size_t payload_length;
    status_t status;

    if (wire == 0 || generation == 0 || config == 0 ||
        read_u32(&wire[0]) != STORAGE_CONFIG_MAGIC ||
        read_u16(&wire[4]) != STORAGE_CONFIG_VERSION) {
        return ERR_PROTOCOL;
    }
    payload_length = read_u16(&wire[12]);
    if (payload_length > STORAGE_CONFIG_PAYLOAD_CAPACITY) {
        return ERR_PROTOCOL;
    }
    status = wire_crc_status(wire, STORAGE_CONFIG_CRC_OFFSET);
    if (status == SYS_OK) {
        status = decode_config_payload(
            &wire[STORAGE_CONFIG_PAYLOAD_OFFSET], payload_length, config);
    }
    if (status == SYS_OK) {
        *generation = read_u32(&wire[8]);
    }
    return status;
}

static status_t commit_config(storage_subsystem_t *subsystem,
                              const gateway_runtime_config_t *config)
{
    const external_flash_partition_t *target;
    uint8_t wire[STORAGE_CONFIG_WIRE_SIZE];
    uint8_t target_copy = subsystem->health.active_config_copy == 0u ? 1u : 0u;
    uint32_t generation = subsystem->health.active_config_generation + 1u;
    status_t status;

    if (subsystem->health.active_config_copy > 1u) {
        target_copy = 0u;
    }
    target = external_flash_partition_get(
        target_copy == 0u ? EXTERNAL_FLASH_PARTITION_CONFIG_A
                          : EXTERNAL_FLASH_PARTITION_CONFIG_B);
    status = encode_config_wire(config, generation, wire);
    if (status == SYS_OK) {
        status = erase_sector(subsystem, target->start);
    }
    if (status == SYS_OK) {
        status = program_verified(subsystem, target->start, wire, sizeof(wire));
    }
    if (status == SYS_OK) {
        subsystem->loaded_config = *config;
        subsystem->health.active_config_copy = target_copy;
        subsystem->health.active_config_generation = generation;
        subsystem->health.config_commits++;
    }
    return status;
}

static status_t mount_config(storage_subsystem_t *subsystem)
{
    gateway_runtime_config_t configs[2];
    uint32_t generations[2] = {0u, 0u};
    int valid[2] = {0, 0};
    unsigned int i;

    for (i = 0u; i < 2u; ++i) {
        const external_flash_partition_t *partition =
            external_flash_partition_get(
                i == 0u ? EXTERNAL_FLASH_PARTITION_CONFIG_A
                        : EXTERNAL_FLASH_PARTITION_CONFIG_B);
        uint8_t wire[STORAGE_CONFIG_WIRE_SIZE];
        status_t status = storage_media_read(
            subsystem->media, partition->start, wire, sizeof(wire));

        if (status != SYS_OK) {
            return status;
        }
        if (!buffer_erased(wire, sizeof(wire)) &&
            decode_config_wire(wire, &generations[i], &configs[i]) == SYS_OK) {
            valid[i] = 1;
        } else if (!buffer_erased(wire, sizeof(wire))) {
            subsystem->health.mount_invalid_records++;
        }
    }
    if (!valid[0] && !valid[1]) {
        subsystem->loaded_config = subsystem->default_config;
        subsystem->health.active_config_copy = 0xffu;
        subsystem->health.active_config_generation = 0u;
        return commit_config(subsystem, &subsystem->default_config);
    }
    i = valid[1] &&
                (!valid[0] || generation_newer(generations[1], generations[0]))
            ? 1u
            : 0u;
    subsystem->loaded_config = configs[i];
    subsystem->health.active_config_copy = (uint8_t)i;
    subsystem->health.active_config_generation = generations[i];
    return SYS_OK;
}

status_t
storage_subsystem_construct(storage_subsystem_t *subsystem,
                            storage_media_t *media,
                            const gateway_runtime_config_t *default_config)
{
    const external_flash_partition_t *partition;

    if (subsystem == 0 || media == 0 || !runtime_config_valid(default_config) ||
        external_flash_layout_validate() != SYS_OK ||
        media->total_size < EXTERNAL_FLASH_TOTAL_SIZE ||
        media->page_size != EXTERNAL_FLASH_PAGE_SIZE ||
        media->sector_size != EXTERNAL_FLASH_SECTOR_SIZE) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->media = media;
    subsystem->default_config = *default_config;
    subsystem->health.active_config_copy = 0xffu;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;

    partition =
        external_flash_partition_get(EXTERNAL_FLASH_PARTITION_RUNTIME_LOG);
    subsystem->runtime_log.partition = *partition;
    subsystem->runtime_log.region_id = STORAGE_REGION_RUNTIME_LOG;
    partition =
        external_flash_partition_get(EXTERNAL_FLASH_PARTITION_ALARM_LOG);
    subsystem->alarm_log.partition = *partition;
    subsystem->alarm_log.region_id = STORAGE_REGION_ALARM_LOG;
    partition = external_flash_partition_get(EXTERNAL_FLASH_PARTITION_CRASH);
    subsystem->crash_log.partition = *partition;
    subsystem->crash_log.region_id = STORAGE_REGION_CRASH;
    return SYS_OK;
}

status_t storage_subsystem_start(storage_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->media == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.mounted != 0u) {
        return SYS_OK;
    }
    status = storage_media_wake(subsystem->media);
    if (status == SYS_OK) {
        status = mount_region(subsystem, &subsystem->runtime_log);
    }
    if (status == SYS_OK) {
        status = mount_region(subsystem, &subsystem->alarm_log);
    }
    if (status == SYS_OK) {
        status = mount_region(subsystem, &subsystem->crash_log);
    }
    if (status == SYS_OK) {
        status = scan_latest_fault(subsystem);
    }
    if (status == SYS_OK) {
        subsystem->health.mounted = 1u;
        status = mount_config(subsystem);
    }
    if (status != SYS_OK) {
        subsystem->health.mounted = 0u;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t
storage_subsystem_append_log(storage_subsystem_t *subsystem,
                             const gateway_storage_log_request_t *request)
{
    uint8_t payload[STORAGE_RECORD_PAYLOAD_CAPACITY];
    uint8_t wire[STORAGE_RECORD_SIZE];
    size_t payload_length;
    status_t status;

    if (subsystem == 0 || request == 0 || subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    payload_length = encode_measurement_payload(&request->measurement, payload);
    status = encode_record(
        STORAGE_WIRE_LOG,
        &(const storage_record_input_t){0u,
                                        subsystem->runtime_log.generation,
                                        request->sequence,
                                        request->measurement.wall_time_ms,
                                        payload,
                                        payload_length,
                                        wire});
    if (status == SYS_OK) {
        status = append_record(subsystem, &subsystem->runtime_log, wire);
    }
    if (status == SYS_OK) {
        subsystem->health.log_records++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t
storage_subsystem_append_alarm(storage_subsystem_t *subsystem,
                               const gateway_storage_alarm_request_t *request)
{
    uint8_t payload[STORAGE_RECORD_PAYLOAD_CAPACITY];
    uint8_t wire[STORAGE_RECORD_SIZE];
    size_t payload_length;
    status_t status;

    if (subsystem == 0 || request == 0 || subsystem->health.mounted == 0u ||
        request->event.event_id == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    payload_length = encode_alarm_payload(&request->event, payload);
    status = encode_record(
        STORAGE_WIRE_ALARM,
        &(const storage_record_input_t){(uint8_t)request->event.transition,
                                        subsystem->alarm_log.generation,
                                        request->event.event_id,
                                        request->event.wall_time_ms,
                                        payload,
                                        payload_length,
                                        wire});
    if (status == SYS_OK) {
        status = append_record(subsystem, &subsystem->alarm_log, wire);
    }
    if (status == SYS_OK) {
        subsystem->health.alarm_records++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t storage_subsystem_archive_fault(storage_subsystem_t *subsystem,
                                         const fault_record_t *record)
{
    uint8_t payload[STORAGE_FAULT_PAYLOAD_SIZE];
    uint8_t wire[STORAGE_RECORD_SIZE];
    status_t status;

    if (subsystem == 0 || record == 0 || subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = fault_record_validate(record);
    if (status != SYS_OK) {
        return status;
    }
    if (subsystem->health.latest_crash_valid != 0u &&
        subsystem->latest_fault.sequence == record->sequence &&
        subsystem->latest_fault.crc32 == record->crc32) {
        subsystem->health.crash_duplicates++;
        subsystem->health.last_error = SYS_OK;
        return SYS_OK;
    }
    status = encode_fault_payload(record, payload);
    if (status == SYS_OK) {
        status = encode_record(
            STORAGE_WIRE_CRASH,
            &(const storage_record_input_t){record->origin,
                                            subsystem->crash_log.generation,
                                            record->sequence,
                                            0u,
                                            payload,
                                            sizeof(payload),
                                            wire});
    }
    if (status == SYS_OK) {
        status = append_record(subsystem, &subsystem->crash_log, wire);
    }
    if (status == SYS_OK) {
        subsystem->latest_fault = *record;
        subsystem->health.latest_crash_valid = 1u;
        subsystem->health.latest_crash_sequence = record->sequence;
        subsystem->health.crash_archives++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t
storage_subsystem_load_latest_fault(const storage_subsystem_t *subsystem,
                                    fault_record_t *record)
{
    if (subsystem == 0 || record == 0 || subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->health.latest_crash_valid == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *record = subsystem->latest_fault;
    return fault_record_validate(record);
}

status_t
storage_subsystem_save_config(storage_subsystem_t *subsystem,
                              const gateway_storage_config_request_t *request)
{
    status_t status;

    if (subsystem == 0 || request == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (!runtime_config_valid(&request->config)) {
        return ERR_INVALID_ARG;
    }
    status = commit_config(subsystem, &request->config);
    subsystem->health.last_error = status;
    return status;
}

status_t storage_subsystem_load_config(const storage_subsystem_t *subsystem,
                                       gateway_runtime_config_t *config)
{
    if (subsystem == 0 || config == 0 || subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *config = subsystem->loaded_config;
    return SYS_OK;
}

status_t storage_subsystem_power_down(storage_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->media == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.mounted == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->health.powered_down != 0u) {
        return SYS_OK;
    }
    status = storage_media_power_down(subsystem->media);
    if (status == SYS_OK) {
        subsystem->health.powered_down = 1u;
        subsystem->health.power_downs++;
    } else {
        subsystem->health.power_failures++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t storage_subsystem_wake(storage_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->media == 0) {
        return ERR_INVALID_ARG;
    }
    if (subsystem->health.powered_down == 0u) {
        return SYS_OK;
    }
    status = storage_media_wake(subsystem->media);
    if (status == SYS_OK) {
        subsystem->health.powered_down = 0u;
        subsystem->health.wakeups++;
    } else {
        subsystem->health.power_failures++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t storage_subsystem_get_health(const storage_subsystem_t *subsystem,
                                      storage_health_t *health)
{
    if (subsystem == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}
