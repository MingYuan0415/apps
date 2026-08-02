#include "setup_wifi_adapter.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_SUBSCRIPTION_COUNT 2U

typedef struct test_subscription
{
    bool active;
    event_bus_sub_handle_t handle;
    event_bus_cb_t callback;
    void *user_data;
    uint32_t subtype;
} test_subscription_t;

typedef struct test_observer
{
    unsigned status_count;
    unsigned scan_count;
    setup_wifi_status_scope_t status_scope;
    setup_wifi_operation_kind_t operation_kind;
    connectivity_manager_status_snapshot_t status;
    connectivity_manager_scan_snapshot_t scan;
} test_observer_t;

static test_subscription_t s_subscriptions[TEST_SUBSCRIPTION_COUNT];
static event_bus_sub_handle_t s_next_handle;
static unsigned s_subscribe_calls;
static unsigned s_fail_subscribe_at;
static unsigned s_unsubscribe_calls;
static connectivity_manager_operation_id_t s_operation_id;
static connectivity_manager_operation_id_t s_next_operation_id;
static unsigned s_cancel_calls;
static unsigned s_disconnect_calls;
static unsigned s_reconnect_calls;
static unsigned s_forget_calls;
static unsigned s_auto_connect_calls;
static bool s_auto_connect_value;
static esp_err_t s_request_result;
static esp_err_t s_cancel_result;
static esp_err_t s_unsubscribe_result;
static connectivity_manager_credentials_t s_credentials;
static char s_ssid[CONNECTIVITY_MANAGER_SSID_MAX_BYTES + 1U];
static uint8_t s_password[CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES];

static const uint8_t s_connectivity_manager_message;
const event_bus_msg_id_t CONNECTIVITY_MANAGER_MSG =
    &s_connectivity_manager_message;

static void _test_reset(void)
{
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    s_next_handle = 1U;
    s_subscribe_calls = 0U;
    s_fail_subscribe_at = 0U;
    s_unsubscribe_calls = 0U;
    s_operation_id = 0U;
    s_next_operation_id = 10U;
    s_cancel_calls = 0U;
    s_disconnect_calls = 0U;
    s_reconnect_calls = 0U;
    s_forget_calls = 0U;
    s_auto_connect_calls = 0U;
    s_auto_connect_value = false;
    s_request_result = ESP_OK;
    s_cancel_result = ESP_OK;
    s_unsubscribe_result = ESP_OK;
    memset(&s_credentials, 0, sizeof(s_credentials));
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_password, 0, sizeof(s_password));
}

static size_t _test_active_subscriptions(void)
{
    size_t count = 0U;
    for (size_t index = 0U; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        if (s_subscriptions[index].active)
        {
            ++count;
        }
    }
    return count;
}

static void _test_status_callback(
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind,
    void *user_data)
{
    test_observer_t *observer = user_data;
    ++observer->status_count;
    observer->status_scope = scope;
    observer->operation_kind = operation_kind;
    observer->status = *snapshot;
}

static void _test_scan_callback(
    const connectivity_manager_scan_snapshot_t *snapshot,
    void *user_data)
{
    test_observer_t *observer = user_data;
    ++observer->scan_count;
    observer->scan = *snapshot;
}

static void _test_publish(uint32_t subtype, const void *payload, size_t size)
{
    for (size_t index = 0U; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        test_subscription_t *subscription = &s_subscriptions[index];
        if (subscription->active && subscription->subtype == subtype)
        {
            subscription->callback(CONNECTIVITY_MANAGER_MSG, subtype, payload,
                                   size, subscription->user_data);
        }
    }
}

static connectivity_manager_status_snapshot_t _test_status(
    uint64_t generation, connectivity_manager_state_t state)
{
    const connectivity_manager_status_snapshot_t snapshot =
    {
        .generation = generation,
        .operation_id = s_operation_id,
        .state = state,
        .available = state != CONNECTIVITY_MANAGER_STATE_OFFLINE,
        .radio_available = state != CONNECTIVITY_MANAGER_STATE_OFFLINE,
    };
    return snapshot;
}

static connectivity_manager_scan_snapshot_t _test_scan(
    uint64_t generation, bool running, esp_err_t result)
{
    const connectivity_manager_scan_snapshot_t snapshot =
    {
        .generation = generation,
        .operation_id = s_operation_id,
        .last_error = result,
        .running = running,
    };
    return snapshot;
}

esp_err_t event_bus_subscribe(event_bus_msg_id_t msg_id, uint32_t subtype,
                              event_bus_cb_t callback, void *user_data,
                              event_bus_dispatch_context_t context,
                              event_bus_sub_handle_t *out_handle)
{
    assert(msg_id == CONNECTIVITY_MANAGER_MSG);
    assert(context == EVENT_BUS_DISPATCH_UI);
    ++s_subscribe_calls;
    if (s_fail_subscribe_at == s_subscribe_calls)
    {
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0U; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        if (!s_subscriptions[index].active)
        {
            s_subscriptions[index].active = true;
            s_subscriptions[index].handle = s_next_handle++;
            s_subscriptions[index].callback = callback;
            s_subscriptions[index].user_data = user_data;
            s_subscriptions[index].subtype = subtype;
            *out_handle = s_subscriptions[index].handle;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t event_bus_unsubscribe(event_bus_sub_handle_t handle)
{
    ++s_unsubscribe_calls;
    if (s_unsubscribe_result != ESP_OK)
    {
        const esp_err_t result = s_unsubscribe_result;
        s_unsubscribe_result = ESP_OK;
        return result;
    }
    for (size_t index = 0U; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        if (s_subscriptions[index].active &&
                s_subscriptions[index].handle == handle)
        {
            memset(&s_subscriptions[index], 0, sizeof(s_subscriptions[index]));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t _test_admit(
    connectivity_manager_operation_id_t *out_operation)
{
    if (s_request_result != ESP_OK)
    {
        return s_request_result;
    }
    s_operation_id = s_next_operation_id++;
    *out_operation = s_operation_id;
    return ESP_OK;
}

esp_err_t connectivity_manager_request_scan(
    connectivity_manager_operation_id_t *out_operation)
{
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_request_connect(
    const connectivity_manager_credentials_t *credentials,
    connectivity_manager_operation_id_t *out_operation)
{
    s_credentials = *credentials;
    memcpy(s_ssid, credentials->ssid, credentials->ssid_length);
    s_ssid[credentials->ssid_length] = '\0';
    s_credentials.ssid = s_ssid;
    if (credentials->password_length > 0U)
    {
        memcpy(s_password, credentials->password, credentials->password_length);
    }
    s_credentials.password = (const char *)s_password;
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_request_disconnect(
    connectivity_manager_operation_id_t *out_operation)
{
    ++s_disconnect_calls;
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_request_reconnect_saved(
    connectivity_manager_operation_id_t *out_operation)
{
    ++s_reconnect_calls;
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_request_forget(
    connectivity_manager_operation_id_t *out_operation)
{
    ++s_forget_calls;
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_set_auto_connect(
    bool enabled, connectivity_manager_operation_id_t *out_operation)
{
    ++s_auto_connect_calls;
    s_auto_connect_value = enabled;
    return _test_admit(out_operation);
}

esp_err_t connectivity_manager_cancel(
    connectivity_manager_operation_id_t operation)
{
    ++s_cancel_calls;
    if (operation != s_operation_id)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_cancel_result == ESP_OK)
    {
        s_operation_id = 0U;
    }
    return s_cancel_result;
}

esp_err_t connectivity_manager_get_status(
    connectivity_manager_status_snapshot_t *snapshot)
{
    *snapshot = _test_status(1U, CONNECTIVITY_MANAGER_STATE_IDLE);
    snapshot->operation_id = 0U;
    return ESP_OK;
}

esp_err_t connectivity_manager_get_scan_snapshot(
    connectivity_manager_scan_snapshot_t *snapshot)
{
    *snapshot = _test_scan(1U, false, ESP_ERR_INVALID_STATE);
    snapshot->operation_id = 0U;
    return ESP_OK;
}

static setup_wifi_adapter_callbacks_t _test_callbacks(void)
{
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };
    return callbacks;
}

static void _test_open_and_filter(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks = _test_callbacks();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    assert(observer.status_count == 1U);
    assert(_test_active_subscriptions() == 2U);

    assert(setup_wifi_adapter_scan(&adapter) == ESP_OK);
    connectivity_manager_scan_snapshot_t running =
        _test_scan(2U, true, ESP_OK);
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_SCAN_SNAPSHOT, &running,
                  sizeof(running));
    assert(observer.scan_count == 1U);

    connectivity_manager_scan_snapshot_t results =
        _test_scan(3U, false, ESP_OK);
    results.record_count = 1U;
    memcpy(results.records[0].ssid, "Test AP", sizeof("Test AP"));
    results.records[0].security = CONNECTIVITY_MANAGER_SECURITY_OPEN;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_SCAN_SNAPSHOT, &results,
                  sizeof(results));
    assert(observer.scan_count == 2U);
    assert(!setup_wifi_adapter_has_operation(&adapter));

    connectivity_manager_scan_snapshot_t stale = results;
    stale.generation = 2U;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_SCAN_SNAPSHOT, &stale,
                  sizeof(stale));
    assert(observer.scan_count == 2U);
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(_test_active_subscriptions() == 0U);
}

static void _test_connect_scrub_and_operations(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks = _test_callbacks();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);

    uint8_t password[CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES];
    memset(password, 'x', sizeof(password));
    assert(setup_wifi_adapter_connect(
               &adapter, "Test AP", 7U,
               CONNECTIVITY_MANAGER_SECURITY_PERSONAL,
               password, 8U) == ESP_OK);
    for (size_t index = 0U; index < sizeof(password); ++index)
    {
        assert(password[index] == 0U);
    }
    assert(memcmp(s_password, "xxxxxxxx", 8U) == 0);

    connectivity_manager_status_snapshot_t short_retry =
        _test_status(3U, CONNECTIVITY_MANAGER_STATE_RETRY_WAIT);
    short_retry.retry_delay_ms = 0U;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                  &short_retry, sizeof(short_retry));
    assert(setup_wifi_adapter_has_operation(&adapter));
    assert(setup_wifi_adapter_cancel(&adapter) == ESP_OK);
    assert(s_cancel_calls == 1U);
    connectivity_manager_status_snapshot_t canceled =
        _test_status(4U, CONNECTIVITY_MANAGER_STATE_IDLE);
    canceled.operation_id = short_retry.operation_id;
    canceled.last_error = ESP_ERR_NOT_FINISHED;
    canceled.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                  &canceled, sizeof(canceled));

    memset(password, 'x', sizeof(password));
    assert(setup_wifi_adapter_connect(
               &adapter, "Test AP", 7U,
               CONNECTIVITY_MANAGER_SECURITY_PERSONAL,
               password, 8U) == ESP_OK);
    connectivity_manager_status_snapshot_t long_retry =
        _test_status(5U, CONNECTIVITY_MANAGER_STATE_RETRY_WAIT);
    long_retry.retry_delay_ms = 1000U;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                  &long_retry, sizeof(long_retry));
    assert(setup_wifi_adapter_has_operation(&adapter));

    connectivity_manager_status_snapshot_t old_done =
        _test_status(6U, CONNECTIVITY_MANAGER_STATE_IDLE);
    old_done.operation_id = short_retry.operation_id;
    old_done.operation_complete = true;
    const unsigned status_before_old_done = observer.status_count;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                  &old_done, sizeof(old_done));
    assert(setup_wifi_adapter_has_operation(&adapter));
    assert(observer.status_count == status_before_old_done);

    connectivity_manager_status_snapshot_t done =
        _test_status(7U, CONNECTIVITY_MANAGER_STATE_IDLE);
    done.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(!setup_wifi_adapter_has_operation(&adapter));

    assert(setup_wifi_adapter_disconnect(&adapter) == ESP_OK);
    assert(s_disconnect_calls == 1U);
    done = _test_status(8U, CONNECTIVITY_MANAGER_STATE_IDLE);
    done.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(setup_wifi_adapter_reconnect_saved(&adapter) == ESP_OK);
    assert(s_reconnect_calls == 1U);
    done = _test_status(9U, CONNECTIVITY_MANAGER_STATE_IP_READY);
    done.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(setup_wifi_adapter_set_auto_connect(&adapter, false) == ESP_OK);
    assert(s_auto_connect_calls == 1U);
    assert(!s_auto_connect_value);
    done = _test_status(10U, CONNECTIVITY_MANAGER_STATE_IP_READY);
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(setup_wifi_adapter_has_operation(&adapter));
    done = _test_status(11U, CONNECTIVITY_MANAGER_STATE_IP_READY);
    done.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(setup_wifi_adapter_forget(&adapter) == ESP_OK);
    assert(s_forget_calls == 1U);
    done = _test_status(12U, CONNECTIVITY_MANAGER_STATE_IDLE);
    done.operation_complete = true;
    _test_publish(CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
}

static void _test_open_rollback_and_close_retry(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks = _test_callbacks();

    _test_reset();
    s_fail_subscribe_at = 2U;
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) ==
           ESP_ERR_NO_MEM);
    assert(_test_active_subscriptions() == 0U);

    _test_reset();
    memset(&adapter, 0, sizeof(adapter));
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    s_unsubscribe_result = ESP_FAIL;
    assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
    assert(adapter.status_subscription != EVENT_BUS_SUB_HANDLE_INVALID);
    assert(adapter.scan_subscription == EVENT_BUS_SUB_HANDLE_INVALID);
    assert(_test_active_subscriptions() == 1U);
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(_test_active_subscriptions() == 0U);
}

static void _test_close_retries_cancel(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks = _test_callbacks();

    _test_reset();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    assert(setup_wifi_adapter_scan(&adapter) == ESP_OK);
    s_cancel_result = ESP_FAIL;
    assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
    assert(!setup_wifi_adapter_is_open(&adapter));
    assert(setup_wifi_adapter_has_operation(&adapter) == false);
    assert(adapter.operation_id != 0U);
    assert(s_cancel_calls == 1U);

    s_cancel_result = ESP_OK;
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(adapter.user_data == NULL);
    assert(s_cancel_calls == 2U);
}

int main(void)
{
    _test_reset();
    _test_open_and_filter();
    _test_reset();
    _test_connect_scrub_and_operations();
    _test_open_rollback_and_close_retry();
    _test_close_retries_cancel();
    return 0;
}
