#include "request_lifecycle.h"
#include "config_transaction_service.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static status_t fake_relay_init(void *opaque, int active)
{
    *(int *)opaque = active;
    return SYS_OK;
}
static status_t relay_write(void *opaque, int active)
{
    *(int *)opaque = active;
    return SYS_OK;
}
static status_t persist(void *opaque, const gateway_storage_config_request_t *request)
{
    (void)request;
    return *(status_t *)opaque;
}

static void test_request_ownership(void)
{
    request_lifecycle_t request = { 0 };
    uint32_t first;
    assert(request_lifecycle_submit(&request, 100u, 50u) == SYS_OK);
    first = request.generation;
    assert(request_lifecycle_poll(&request, 149u) == ERR_IN_PROGRESS);
    assert(request_lifecycle_poll(&request, 150u) == ERR_TIMEOUT);
    /* A caller timeout does not make the worker-owned buffer reusable. */
    assert(request_lifecycle_submit(&request, 151u, 50u) == ERR_DEVICE_NOT_READY);
    assert(request_lifecycle_begin(&request, first, 151u) == ERR_TIMEOUT);
    assert(request.state == REQUEST_COMPLETED);
    assert(request_lifecycle_submit(&request, 200u, 50u) == SYS_OK);
    assert(request_lifecycle_complete(&request, first, SYS_OK) == ERR_INVALID_ARG);
    assert(request_lifecycle_begin(&request, request.generation, 200u) == SYS_OK);
    request_lifecycle_cancel(&request);
    assert(request_lifecycle_poll(&request, 250u) == ERR_TIMEOUT);
    assert(request_lifecycle_submit(&request, 251u, 50u) == ERR_DEVICE_NOT_READY);
    assert(request_lifecycle_complete(&request, request.generation, ERR_IO) == SYS_OK);
    assert(request_lifecycle_poll(&request, 251u) == ERR_IO);
    assert(request_lifecycle_submit(&request, UINT32_MAX - 10u, 20u) == SYS_OK);
    assert(request_lifecycle_poll(&request, 8u) == ERR_IN_PROGRESS);
    assert(request_lifecycle_poll(&request, 9u) == ERR_TIMEOUT);
    assert(request_lifecycle_begin(&request, request.generation, 9u) == ERR_TIMEOUT);
    assert(request_lifecycle_submit(&request, 100u, 100u) == SYS_OK);
    request_lifecycle_cancel(&request);
    assert(request_lifecycle_begin(&request, request.generation, 101u) == ERR_OTA_ABORTED);
}

static void test_configuration_rollback(void)
{
    gateway_runtime_config_t initial = { 0 };
    config_subsystem_t config;
    alarm_subsystem_t alarm;
    relay_t relay;
    int level = 0;
    status_t storage_status = ERR_IO;
    const relay_ops_t ops = { .init = fake_relay_init, .write = relay_write };
    const relay_config_t relay_config = { 1u, RELAY_DEENERGIZED };
    config_patch_t patch = { GATEWAY_POINT_LOOP_CURRENT, CONFIG_FIELD_HIGH_THRESHOLD, 22000 };
    gateway_storage_config_request_t request;
    initial.schema_version = GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION;
    initial.revision = 1u;
    initial.rule_count = 1u;
    initial.rules[0].point_id = GATEWAY_POINT_LOOP_CURRENT;
    initial.rules[0].high_enabled = 1u;
    initial.rules[0].high_threshold = 20000;
    initial.rules[0].assert_samples = 1u;
    initial.rules[0].recover_samples = 1u;
    assert(relay_construct(&relay, &ops, &level, &relay_config) == SYS_OK);
    assert(alarm_subsystem_construct(&alarm, &relay, &initial) == SYS_OK);
    assert(alarm_subsystem_start(&alarm) == SYS_OK);
    assert(config_subsystem_construct(&config, &initial) == SYS_OK);
    assert(config_subsystem_prepare(&config, &patch, &request) == SYS_OK);
    assert(config_transaction_execute(&config, &alarm, &request, persist,
                                       &storage_status) == ERR_IO);
    assert(config.health.pending_requests == 0u);
    assert(config.active.rules[0].high_threshold == 20000);
    assert(alarm.config.rules[0].high_threshold == 20000);
    assert(config_transaction_execute(&config, &alarm, &request, persist,
                                       &storage_status) == ERR_INVALID_ARG);
    storage_status = SYS_OK;
    assert(config_subsystem_prepare(&config, &patch, &request) == SYS_OK);
    assert(config_transaction_execute(&config, &alarm, &request, persist,
                                       &storage_status) == SYS_OK);
    assert(config.active.revision == 2u && alarm.config.rules[0].high_threshold == 22000);
}

int main(void)
{
    test_request_ownership();
    test_configuration_rollback();
    puts("runtime service fault tests passed");
    return 0;
}
