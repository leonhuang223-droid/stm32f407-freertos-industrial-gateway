#include "app_context.h"
#include "device.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_EQ(expected, actual) do { \
    int expected_value = (int)(expected); \
    int actual_value = (int)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d expected %d actual %d\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    unsigned int init_calls;
    unsigned int suspend_calls;
    unsigned int resume_calls;
    unsigned int self_test_calls;
} fake_device_context_t;

static status_t fake_init(gateway_device_t *device)
{
    fake_device_context_t *context = device->context;
    context->init_calls++;
    return SYS_OK;
}

static status_t fake_suspend(gateway_device_t *device)
{
    fake_device_context_t *context = device->context;
    context->suspend_calls++;
    return SYS_OK;
}

static status_t fake_resume(gateway_device_t *device)
{
    fake_device_context_t *context = device->context;
    context->resume_calls++;
    return SYS_OK;
}

static status_t fake_self_test(gateway_device_t *device)
{
    fake_device_context_t *context = device->context;
    context->self_test_calls++;
    return SYS_OK;
}

static void test_device_lifecycle(void)
{
    static const gateway_device_ops_t ops = {
        fake_init, fake_suspend, fake_resume, fake_self_test
    };
    fake_device_context_t fake = { 0 };
    gateway_device_t device = { &ops, &fake, "fake", 0u };

    EXPECT_EQ(ERR_INVALID_ARG, gateway_device_suspend(&device));
    EXPECT_EQ(SYS_OK, gateway_device_init(&device));
    EXPECT_EQ(1, device.initialized);
    EXPECT_EQ(SYS_OK, gateway_device_suspend(&device));
    EXPECT_EQ(SYS_OK, gateway_device_resume(&device));
    EXPECT_EQ(SYS_OK, gateway_device_self_test(&device));
    EXPECT_EQ(1, fake.init_calls);
    EXPECT_EQ(1, fake.suspend_calls);
    EXPECT_EQ(1, fake.resume_calls);
    EXPECT_EQ(1, fake.self_test_calls);
}

static void test_optional_operation(void)
{
    static const gateway_device_ops_t ops = {
        fake_init, 0, 0, 0
    };
    fake_device_context_t fake = { 0 };
    gateway_device_t device = { &ops, &fake, "minimal", 0u };

    EXPECT_EQ(SYS_OK, gateway_device_init(&device));
    EXPECT_EQ(ERR_UNSUPPORTED, gateway_device_suspend(&device));
    EXPECT_EQ(ERR_UNSUPPORTED, gateway_device_resume(&device));
    EXPECT_EQ(ERR_UNSUPPORTED, gateway_device_self_test(&device));
}

static void test_app_context(void)
{
    app_context_t context;

    memset(&context, 0xA5, sizeof(context));
    EXPECT_EQ(SYS_OK, app_context_init(&context));
    EXPECT_EQ(GATEWAY_QUALITY_UNAVAILABLE, context.snapshot.latest.quality);
    EXPECT_EQ(ERR_DEVICE_NOT_READY, context.acquisition_startup_status);
    EXPECT_EQ(ERR_DEVICE_NOT_READY, context.fieldbus_startup_status);
    EXPECT_EQ(0, context.heartbeat[GATEWAY_TASK_DATA_HUB]);
    app_context_mark_alive(&context, GATEWAY_TASK_DATA_HUB);
    EXPECT_EQ(1, context.heartbeat[GATEWAY_TASK_DATA_HUB]);
    app_context_mark_alive(&context, GATEWAY_TASK_COUNT);
    EXPECT_EQ(1, context.heartbeat[GATEWAY_TASK_DATA_HUB]);
}

int main(void)
{
    test_device_lifecycle();
    test_optional_operation();
    test_app_context();

    if (failures != 0) {
        printf("%d gateway architecture test(s) failed\n", failures);
        return 1;
    }
    puts("gateway architecture tests passed");
    return 0;
}
