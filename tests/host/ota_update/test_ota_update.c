#include "boot_metadata.h"
#include "crc32.h"
#include "external_flash_layout.h"
#include "http_client_raw.h"
#include "ota_manager.h"
#include "ota_staging.h"
#include "platform_constants.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_EQ(expected, actual) do { \
    long expected_value = (long)(expected); \
    long actual_value = (long)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d expected=%ld actual=%ld\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    uint8_t response[1024];
    size_t response_length;
    size_t response_offset;
    size_t read_chunk;
    char host[HTTP_CLIENT_MAX_HOST_LEN];
    uint16_t port;
    char request[HTTP_CLIENT_MAX_REQUEST_LEN];
    uint32_t close_count;
    status_t close_status;
} fake_transport_t;

static status_t fake_transport_init(void *context)
{
    return context != 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t fake_transport_connect(void *context, const char *host,
                                       uint16_t port)
{
    fake_transport_t *fake = context;

    if (fake == 0 || host == 0) {
        return ERR_INVALID_ARG;
    }
    (void)snprintf(fake->host, sizeof(fake->host), "%s", host);
    fake->port = port;
    return SYS_OK;
}

static status_t fake_transport_send(void *context, const uint8_t *data,
                                    size_t length)
{
    fake_transport_t *fake = context;

    if (fake == 0 || data == 0 || length >= sizeof(fake->request)) {
        return ERR_INVALID_ARG;
    }
    memcpy(fake->request, data, length);
    fake->request[length] = '\0';
    return SYS_OK;
}

static status_t fake_transport_receive(void *context, uint8_t *data,
                                       size_t capacity, size_t *length,
                                       uint32_t timeout_ms)
{
    fake_transport_t *fake = context;
    size_t remaining;
    size_t chunk;

    (void)timeout_ms;
    if (fake == 0 || data == 0 || length == 0) {
        return ERR_INVALID_ARG;
    }
    remaining = fake->response_length - fake->response_offset;
    if (remaining == 0u) {
        *length = 0u;
        return ERR_TIMEOUT;
    }
    chunk = fake->read_chunk != 0u && fake->read_chunk < remaining
        ? fake->read_chunk : remaining;
    if (chunk > capacity) {
        chunk = capacity;
    }
    memcpy(data, &fake->response[fake->response_offset], chunk);
    fake->response_offset += chunk;
    *length = chunk;
    return SYS_OK;
}

static status_t fake_transport_close(void *context)
{
    fake_transport_t *fake = context;

    if (fake == 0) {
        return ERR_INVALID_ARG;
    }
    fake->close_count++;
    return fake->close_status;
}

static void test_http_client(void)
{
    static const network_transport_ops_t ops = {
        fake_transport_init,
        fake_transport_connect,
        fake_transport_send,
        fake_transport_receive,
        fake_transport_close,
        0,
        0
    };
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello world";
    fake_transport_t fake;
    network_transport_t transport;
    http_client_raw_t client;
    uint8_t body[16];
    size_t body_length = 0u;
    char host[32];
    char path[32];
    uint16_t port = 0u;
    http_header_info_t header;

    memset(&fake, 0, sizeof(fake));
    memcpy(fake.response, response, sizeof(response) - 1u);
    fake.response_length = sizeof(response) - 1u;
    fake.read_chunk = 9u;
    EXPECT_EQ(SYS_OK, network_transport_construct(&transport, &ops, &fake));
    EXPECT_EQ(SYS_OK, network_transport_init(&transport));
    EXPECT_EQ(SYS_OK, http_client_construct(&client, &transport, 100u));
    EXPECT_EQ(SYS_OK, http_get_document(&client,
        "http://example.com:8080/fw.bin", body, sizeof(body),
        &body_length));
    EXPECT_EQ(11u, body_length);
    EXPECT_TRUE(memcmp(body, "hello world", 11u) == 0);
    EXPECT_TRUE(strcmp(fake.host, "example.com") == 0);
    EXPECT_EQ(8080u, fake.port);
    EXPECT_TRUE(strstr(fake.request, "GET /fw.bin HTTP/1.1") != 0);
    EXPECT_EQ(1u, fake.close_count);

    fake.response_offset = 0u;
    EXPECT_EQ(SYS_OK, http_open_get(&client, "http://example.com/fw.bin"));
    fake.close_status = ERR_IO;
    EXPECT_EQ(ERR_IO, http_close(&client));
    EXPECT_TRUE(client.connected != 0u);
    EXPECT_EQ(ERR_IO, http_open_get(&client, "http://newhost/fw.bin"));
    EXPECT_TRUE(strcmp(fake.host, "example.com") == 0);
    fake.close_status = SYS_OK;
    EXPECT_EQ(SYS_OK, http_close(&client));
    EXPECT_EQ(0u, client.connected);

    EXPECT_EQ(ERR_UNSUPPORTED, http_parse_url("https://example.com/fw.bin",
    &(const http_url_output_t){ host, sizeof(host), path, sizeof(path), &port }));
    EXPECT_EQ(ERR_UNSUPPORTED, http_parse_header(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
        &header));
    EXPECT_EQ(ERR_HTTP, http_parse_header(
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n",
        &header));
}

#define FAKE_FLASH_SIZE (EXTERNAL_FLASH_OTA_METADATA_START + \
                         EXTERNAL_FLASH_OTA_METADATA_SIZE)

typedef struct {
    uint8_t bytes[FAKE_FLASH_SIZE];
    uint32_t erases;
    uint32_t programs;
} fake_media_t;

static status_t fake_media_read(void *context, uint32_t address,
                                uint8_t *buffer, size_t length)
{
    fake_media_t *fake = context;

    if (fake == 0 || buffer == 0 || address >= sizeof(fake->bytes) ||
        length > sizeof(fake->bytes) - address) {
        return ERR_INVALID_ARG;
    }
    memcpy(buffer, &fake->bytes[address], length);
    return SYS_OK;
}

static status_t fake_media_program(void *context, uint32_t address,
                                   const uint8_t *data, size_t length)
{
    fake_media_t *fake = context;
    size_t index;

    if (fake == 0 || data == 0 || address >= sizeof(fake->bytes) ||
        length > sizeof(fake->bytes) - address) {
        return ERR_INVALID_ARG;
    }
    for (index = 0u; index < length; ++index) {
        fake->bytes[address + index] &= data[index];
    }
    fake->programs++;
    return SYS_OK;
}

static status_t fake_media_erase(void *context, uint32_t address)
{
    fake_media_t *fake = context;

    if (fake == 0 || address % EXTERNAL_FLASH_SECTOR_SIZE != 0u ||
        address >= sizeof(fake->bytes) ||
        EXTERNAL_FLASH_SECTOR_SIZE > sizeof(fake->bytes) - address) {
        return ERR_INVALID_ARG;
    }
    memset(&fake->bytes[address], 0xFF, EXTERNAL_FLASH_SECTOR_SIZE);
    fake->erases++;
    return SYS_OK;
}

static status_t fake_media_wake(void *context)
{
    return context != 0 ? SYS_OK : ERR_INVALID_ARG;
}

static void test_staging(void)
{
    static const storage_media_ops_t ops = {
        fake_media_read,
        fake_media_program,
        fake_media_erase,
        fake_media_wake,
        0
    };
    static fake_media_t fake;
    storage_media_t media;
    ota_staging_t staging;
    uint8_t first[256];
    uint8_t last[44];
    uint8_t metadata[OTA_STAGING_METADATA_WIRE_SIZE];
    uint8_t complete = 0u;

    memset(&fake, 0xFF, sizeof(fake));
    fake.erases = 0u;
    fake.programs = 0u;
    memset(first, 0x5A, sizeof(first));
    memset(last, 0xA5, sizeof(last));
    memset(metadata, 0x3C, sizeof(metadata));
    EXPECT_EQ(SYS_OK, storage_media_construct(&media,
    &ops,
    &fake,
    &(const storage_media_geometry_t){ sizeof(fake.bytes), EXTERNAL_FLASH_PAGE_SIZE, EXTERNAL_FLASH_SECTOR_SIZE }));
    EXPECT_EQ(SYS_OK, ota_staging_construct(&staging, &media));
    EXPECT_EQ(SYS_OK, ota_staging_begin(&staging, 300u));
    EXPECT_EQ(ERR_INVALID_ARG,
              ota_staging_write(&staging, 0u, first, sizeof(first)));
    EXPECT_EQ(SYS_OK, ota_staging_erase_next(&staging, &complete));
    EXPECT_EQ(1u, complete);
    EXPECT_EQ(SYS_OK,
              ota_staging_write(&staging, 0u, first, sizeof(first)));
    EXPECT_EQ(ERR_INVALID_ARG,
              ota_staging_write(&staging, 100u, last, sizeof(last)));
    EXPECT_EQ(SYS_OK,
              ota_staging_write(&staging, 256u, last, sizeof(last)));
    EXPECT_EQ(SYS_OK, ota_staging_commit_metadata(
        &staging, metadata, sizeof(metadata)));
    EXPECT_TRUE(memcmp(fake.bytes, first, sizeof(first)) == 0);
    EXPECT_TRUE(memcmp(&fake.bytes[256], last, sizeof(last)) == 0);
    EXPECT_TRUE(memcmp(&fake.bytes[EXTERNAL_FLASH_OTA_METADATA_START],
                       metadata, sizeof(metadata)) == 0);
    EXPECT_EQ(2u, fake.erases);
}

typedef struct {
    uint8_t package[512];
    size_t package_length;
    size_t read_offset;
    char manifest[768];
    boot_metadata_t metadata;
    uint8_t staged[512];
    uint8_t staging_metadata[OTA_STAGING_METADATA_WIRE_SIZE];
    uint32_t metadata_commits;
    uint32_t closes;
    status_t close_status;
} fake_ota_port_t;

static status_t manager_fetch(void *context, const char *url,
                              uint8_t *buffer, size_t capacity,
                              size_t *out_length)
{
    fake_ota_port_t *fake = context;
    size_t length = strlen(fake->manifest);

    (void)url;
    if (length > capacity) {
        return ERR_NO_MEMORY;
    }
    memcpy(buffer, fake->manifest, length);
    *out_length = length;
    return SYS_OK;
}

static status_t manager_http_open(void *context, const char *url,
                                  uint32_t *out_content_length)
{
    fake_ota_port_t *fake = context;

    (void)url;
    fake->read_offset = 0u;
    *out_content_length = (uint32_t)fake->package_length;
    return SYS_OK;
}

static status_t manager_http_read(void *context, uint8_t *buffer,
                                  size_t capacity, size_t *out_length)
{
    fake_ota_port_t *fake = context;
    size_t remaining = fake->package_length - fake->read_offset;
    size_t length = remaining < capacity ? remaining : capacity;

    memcpy(buffer, &fake->package[fake->read_offset], length);
    fake->read_offset += length;
    *out_length = length;
    return SYS_OK;
}

static status_t manager_http_close(void *context)
{
    fake_ota_port_t *fake = context;
    if (fake == 0) {
        return ERR_INVALID_ARG;
    }
    fake->closes++;
    return fake->close_status;
}

static status_t manager_staging_begin(void *context, size_t package_size)
{
    fake_ota_port_t *fake = context;

    memset(fake->staged, 0xFF, sizeof(fake->staged));
    return package_size <= sizeof(fake->staged) ? SYS_OK : ERR_NO_MEMORY;
}

static status_t manager_staging_write(void *context, uint32_t offset,
                                      const uint8_t *data, size_t length)
{
    fake_ota_port_t *fake = context;

    if (offset > sizeof(fake->staged) ||
        length > sizeof(fake->staged) - offset) {
        return ERR_INVALID_ARG;
    }
    memcpy(&fake->staged[offset], data, length);
    return SYS_OK;
}

static status_t manager_metadata_load(void *context,
                                      boot_metadata_t *out_metadata,
                                      app_slot_t *out_copy_slot)
{
    fake_ota_port_t *fake = context;

    *out_metadata = fake->metadata;
    *out_copy_slot = SLOT_A;
    return SYS_OK;
}

static status_t manager_metadata_commit(void *context,
    const boot_meta_commit_request_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const boot_metadata_t *current = parameters->current;
    app_slot_t current_copy_slot = parameters->current_copy_slot;
    const boot_metadata_t *desired = parameters->desired;
    boot_metadata_t *out_committed = parameters->out_committed;
    app_slot_t *out_copy_slot = parameters->out_copy_slot;

    fake_ota_port_t *fake = context;

    (void)current_copy_slot;
    *out_committed = *desired;
    out_committed->sequence = current->sequence + 1u;
    EXPECT_EQ(SYS_OK, boot_meta_refresh_crc(out_committed));
    fake->metadata = *out_committed;
    fake->metadata_commits++;
    *out_copy_slot = SLOT_B;
    return SYS_OK;
}

static status_t manager_staging_metadata(void *context,
                                         const uint8_t *record,
                                         size_t record_size)
{
    fake_ota_port_t *fake = context;

    if (record_size != sizeof(fake->staging_metadata)) {
        return ERR_INVALID_ARG;
    }
    memcpy(fake->staging_metadata, record, record_size);
    return SYS_OK;
}

static void manager_enter(void *context)
{
    (void)context;
}

static void manager_exit(void *context)
{
    (void)context;
}

static void build_manager(ota_manager_t *manager, fake_ota_port_t *fake,
                          uint32_t manifest_crc)
{
    ota_manager_port_t port;
    ota_manager_config_t config;
    uint8_t sha[IMAGE_SHA256_LEN];
    char sha_text[IMAGE_SHA256_LEN * 2u + 1u];
    size_t index;

    memset(fake, 0, sizeof(*fake));
    fake->package_length = sizeof(fake->package);
    for (index = 0u; index < fake->package_length; ++index) {
        fake->package[index] = (uint8_t)(index * 7u + 3u);
    }
    EXPECT_EQ(SYS_OK, sha256_compute(fake->package,
        fake->package_length, sha));
    for (index = 0u; index < sizeof(sha); ++index) {
        (void)snprintf(&sha_text[index * 2u], 3u, "%02x", sha[index]);
    }
    EXPECT_EQ(SYS_OK, boot_meta_init_default(
        &fake->metadata, SLOT_A, "1.0.0"));
    (void)snprintf(fake->manifest, sizeof(fake->manifest),
        "{\"manifest_version\":1,\"target_id\":\"%s\","
        "\"version\":\"2.0.0\",\"target_slot\":1,"
        "\"link_address\":134742016,\"image_size\":512,"
        "\"crc32\":%lu,\"sha256\":\"%s\","
        "\"download_url\":\"http://server/app_b.pkg\","
        "\"min_bootloader_version\":\"1.0.0\","
        "\"force_update\":false,\"release_note\":\"host\"}",
        PROJECT_TARGET_ID, (unsigned long)manifest_crc, sha_text);

    memset(&port, 0, sizeof(port));
    port.fetch_manifest = manager_fetch;
    port.http_open = manager_http_open;
    port.http_read = manager_http_read;
    port.http_close = manager_http_close;
    port.staging_begin = manager_staging_begin;
    port.staging_write = manager_staging_write;
    port.metadata_load = manager_metadata_load;
    port.metadata_commit = manager_metadata_commit;
    port.ota_metadata_commit = manager_staging_metadata;
    port.enter_critical = manager_enter;
    port.exit_critical = manager_exit;
    port.context = fake;
    config.manifest_url = "http://server/manifest.json";
    config.target_id = PROJECT_TARGET_ID;
    config.current_version = "1.0.0";
    config.current_bootloader_version = "1.0.0";
    EXPECT_EQ(SYS_OK, ota_manager_construct(manager, &port, &config));
}

static void test_ota_manager(void)
{
    ota_manager_t manager;
    fake_ota_port_t fake;
    ota_status_t status;
    uint32_t crc;

    memset(&fake, 0, sizeof(fake));
    fake.package_length = sizeof(fake.package);
    {
        size_t index;
        for (index = 0u; index < fake.package_length; ++index) {
            fake.package[index] = (uint8_t)(index * 7u + 3u);
        }
    }
    EXPECT_EQ(SYS_OK, crc32_compute(fake.package,
                                    fake.package_length, &crc));
    build_manager(&manager, &fake, crc);
    {
        ota_manifest_t parsed;
        size_t last = strlen(fake.manifest) - 1u;
        EXPECT_EQ(SYS_OK, manifest_parse_json(fake.manifest, &parsed));
        fake.manifest[last] = ',';
        EXPECT_TRUE(manifest_parse_json(fake.manifest, &parsed) != SYS_OK);
        fake.manifest[last] = '}';
    }
    EXPECT_EQ(SYS_OK, ota_manager_check(&manager));
    EXPECT_EQ(SYS_OK, ota_manager_download(&manager));
    EXPECT_TRUE(memcmp(fake.package, fake.staged,
                       fake.package_length) == 0);
    EXPECT_EQ(SYS_OK, ota_manager_get_status(&manager, &status));
    EXPECT_EQ(OTA_READY, status.state);
    EXPECT_EQ(1u, status.crc_verified);
    EXPECT_EQ(1u, status.sha256_verified);
    EXPECT_EQ(2u, status.chunk_count);
    EXPECT_EQ(SYS_OK, ota_manager_commit_pending(&manager));
    EXPECT_EQ(BOOT_STATE_PENDING, fake.metadata.boot_state);
    EXPECT_EQ(SLOT_B, fake.metadata.pending_slot);
    EXPECT_EQ(1u, fake.metadata_commits);
    EXPECT_EQ(OTA_STAGING_METADATA_MAGIC,
        (uint32_t)fake.staging_metadata[0] |
        ((uint32_t)fake.staging_metadata[1] << 8u) |
        ((uint32_t)fake.staging_metadata[2] << 16u) |
        ((uint32_t)fake.staging_metadata[3] << 24u));

    build_manager(&manager, &fake, crc ^ 1u);
    EXPECT_EQ(SYS_OK, ota_manager_check(&manager));
    EXPECT_EQ(ERR_CRC, ota_manager_download(&manager));
    EXPECT_EQ(ERR_INVALID_ARG, ota_manager_commit_pending(&manager));

    build_manager(&manager, &fake, crc);
    fake.metadata.active_slot = SLOT_B;
    fake.metadata.previous_slot = SLOT_B;
    EXPECT_EQ(SYS_OK, boot_meta_refresh_crc(&fake.metadata));
    EXPECT_EQ(ERR_SLOT_MISMATCH, ota_manager_check(&manager));

    build_manager(&manager, &fake, crc);
    EXPECT_EQ(SYS_OK, ota_manager_check(&manager));
    fake.close_status = ERR_IO;
    EXPECT_EQ(ERR_IO, ota_manager_download(&manager));
    EXPECT_TRUE(manager.connection_open != 0u);
    EXPECT_EQ(1u, fake.closes);
    fake.close_status = SYS_OK;
    EXPECT_EQ(SYS_OK, ota_manager_check(&manager));
    EXPECT_EQ(2u, fake.closes);
    EXPECT_EQ(0u, manager.connection_open);
    manager.connection_open = 1u;
    manager.status.state = OTA_DOWNLOADING;
    EXPECT_EQ(ERR_INVALID_ARG, ota_manager_check(&manager));
    EXPECT_EQ(2u, fake.closes);
    EXPECT_TRUE(manager.connection_open != 0u);
}

int main(void)
{
    test_http_client();
    test_staging();
    test_ota_manager();

    if (failures != 0) {
        printf("ota update tests failed: %d\n", failures);
        return 1;
    }
    printf("ota update tests passed\n");
    return 0;
}
