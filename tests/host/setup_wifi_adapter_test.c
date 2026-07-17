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
    wifi_service_status_snapshot_t status;
    wifi_service_scan_snapshot_t scan;
} test_observer_t;

static test_subscription_t s_subscriptions[TEST_SUBSCRIPTION_COUNT];
static event_bus_sub_handle_t s_next_handle;
static unsigned s_subscribe_calls;
static unsigned s_fail_subscribe_at;
static unsigned s_unsubscribe_calls;
static wifi_service_session_id_t s_session_id;
static wifi_service_operation_id_t s_operation_id;
static wifi_service_operation_id_t s_next_operation_id;
static unsigned s_cancel_calls;
static unsigned s_session_close_calls;
static esp_err_t s_request_connect_result;
static esp_err_t s_request_scan_result;
static esp_err_t s_request_disconnect_result;
static esp_err_t s_cancel_result;
static esp_err_t s_unsubscribe_result;
static esp_err_t s_session_close_result;
static wifi_service_credentials_t s_credentials;
static char s_ssid[WIFI_SERVICE_SSID_MAX_BYTES + 1U];
static uint8_t s_password[WIFI_SERVICE_PASSWORD_MAX_BYTES];

static const uint8_t s_wifi_service_message;
const event_bus_msg_id_t WIFI_SERVICE_MSG = &s_wifi_service_message;

static void _test_reset(void)
{
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    s_next_handle = 1U;
    s_subscribe_calls = 0U;
    s_fail_subscribe_at = 0U;
    s_unsubscribe_calls = 0U;
    s_session_id = 0U;
    s_operation_id = 0U;
    s_next_operation_id = 10U;
    s_cancel_calls = 0U;
    s_session_close_calls = 0U;
    s_request_connect_result = ESP_OK;
    s_request_scan_result = ESP_OK;
    s_request_disconnect_result = ESP_OK;
    s_cancel_result = ESP_OK;
    s_unsubscribe_result = ESP_OK;
    s_session_close_result = ESP_OK;
    memset(&s_credentials, 0, sizeof(s_credentials));
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_password, 0, sizeof(s_password));
}

static size_t _test_active_subscriptions(void)
{
    size_t count = 0U;
    for (size_t index = 0; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        if (s_subscriptions[index].active)
        {
            ++count;
        }
    }
    return count;
}

static void _test_status_callback(
    const wifi_service_status_snapshot_t *snapshot,
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

static void _test_scan_callback(const wifi_service_scan_snapshot_t *snapshot,
                                void *user_data)
{
    test_observer_t *observer = user_data;
    ++observer->scan_count;
    observer->scan = *snapshot;
}

static void _test_publish(uint32_t subtype, const void *payload, size_t size)
{
    for (size_t index = 0; index < TEST_SUBSCRIPTION_COUNT; ++index)
    {
        test_subscription_t *subscription = &s_subscriptions[index];
        if (subscription->active && subscription->subtype == subtype)
        {
            subscription->callback(WIFI_SERVICE_MSG, subtype, payload, size,
                                   subscription->user_data);
        }
    }
}

static wifi_service_status_snapshot_t _test_status(uint64_t generation,
        wifi_service_state_t state)
{
    wifi_service_status_snapshot_t snapshot =
    {
        .generation = generation,
        .session_id = s_session_id,
        .operation_id = s_operation_id,
        .state = state,
        .available = state != WIFI_SERVICE_STATE_OFFLINE,
    };
    return snapshot;
}

static wifi_service_scan_snapshot_t _test_scan(uint64_t generation,
        wifi_service_scan_state_t state)
{
    wifi_service_scan_snapshot_t snapshot =
    {
        .generation = generation,
        .session_id = s_session_id,
        .operation_id = s_operation_id,
        .state = state,
    };
    return snapshot;
}

esp_err_t event_bus_subscribe(event_bus_msg_id_t msg_id, uint32_t subtype,
                              event_bus_cb_t callback, void *user_data,
                              event_bus_dispatch_context_t context,
                              event_bus_sub_handle_t *out_handle)
{
    (void)msg_id;
    (void)context;
    ++s_subscribe_calls;
    if (s_fail_subscribe_at == s_subscribe_calls)
    {
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < TEST_SUBSCRIPTION_COUNT; ++index)
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
    for (size_t index = 0; index < TEST_SUBSCRIPTION_COUNT; ++index)
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

esp_err_t wifi_service_session_open(wifi_service_session_id_t *out_session)
{
    s_session_id = 1U;
    *out_session = s_session_id;
    return ESP_OK;
}

esp_err_t wifi_service_session_close(wifi_service_session_id_t session)
{
    ++s_session_close_calls;
    if (s_session_close_result != ESP_OK)
    {
        const esp_err_t result = s_session_close_result;
        s_session_close_result = ESP_OK;
        return result;
    }
    if (session != s_session_id)
    {
        return ESP_ERR_NOT_FOUND;
    }
    s_session_id = 0U;
    s_operation_id = 0U;
    return ESP_OK;
}

esp_err_t wifi_service_request_scan(wifi_service_session_id_t session,
                                    wifi_service_operation_id_t *out_operation)
{
    if (s_request_scan_result != ESP_OK)
    {
        return s_request_scan_result;
    }
    if (session != s_session_id)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_operation_id = s_next_operation_id++;
    *out_operation = s_operation_id;
    return ESP_OK;
}

esp_err_t wifi_service_request_connect(
    wifi_service_session_id_t session,
    const wifi_service_credentials_t *credentials,
    wifi_service_operation_id_t *out_operation)
{
    if (s_request_connect_result != ESP_OK)
    {
        return s_request_connect_result;
    }
    if (session != s_session_id)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_credentials = *credentials;
    memcpy(s_ssid, credentials->ssid, credentials->ssid_length);
    s_ssid[credentials->ssid_length] = '\0';
    s_credentials.ssid = s_ssid;
    if (credentials->password_length > 0U)
    {
        memcpy(s_password, credentials->password, credentials->password_length);
    }
    s_credentials.password = (const char *)s_password;
    s_operation_id = s_next_operation_id++;
    *out_operation = s_operation_id;
    return ESP_OK;
}

esp_err_t wifi_service_request_disconnect(
    wifi_service_session_id_t session,
    wifi_service_operation_id_t *out_operation)
{
    if (s_request_disconnect_result != ESP_OK || session != s_session_id)
    {
        return s_request_disconnect_result != ESP_OK ?
               s_request_disconnect_result : ESP_ERR_INVALID_STATE;
    }
    s_operation_id = s_next_operation_id++;
    *out_operation = s_operation_id;
    return ESP_OK;
}

esp_err_t wifi_service_cancel(wifi_service_session_id_t session,
                              wifi_service_operation_id_t operation)
{
    ++s_cancel_calls;
    if (session != s_session_id || operation != s_operation_id)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_cancel_result == ESP_OK)
    {
        s_operation_id = 0U;
    }
    return s_cancel_result;
}

esp_err_t wifi_service_get_status(wifi_service_status_snapshot_t *snapshot)
{
    *snapshot = _test_status(1U, WIFI_SERVICE_STATE_IDLE);
    snapshot->session_id = 0U;
    snapshot->operation_id = 0U;
    return ESP_OK;
}

esp_err_t wifi_service_get_scan_snapshot(wifi_service_scan_snapshot_t *snapshot)
{
    *snapshot = _test_scan(1U, WIFI_SERVICE_SCAN_IDLE);
    snapshot->session_id = 0U;
    snapshot->operation_id = 0U;
    return ESP_OK;
}

void wifi_service_secure_zero(void *memory, size_t size)
{
    volatile uint8_t *bytes = memory;
    while (size-- > 0U)
    {
        *bytes++ = 0U;
    }
}

const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "test-error";
}

static void _test_open_and_filter(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    assert(observer.status_count == 1U);
    assert(_test_active_subscriptions() == 2U);

    assert(setup_wifi_adapter_scan(&adapter) == ESP_OK);
    wifi_service_scan_snapshot_t running =
        _test_scan(2U, WIFI_SERVICE_SCAN_RUNNING);
    _test_publish(WIFI_SERVICE_MSG_SUB_TYPE_SCAN_SNAPSHOT, &running,
                  sizeof(running));
    assert(observer.scan_count == 1U);
    wifi_service_scan_snapshot_t results =
        _test_scan(3U, WIFI_SERVICE_SCAN_RESULTS);
    results.record_count = 1U;
    memcpy(results.records[0].ssid, "Test AP", sizeof("Test AP"));
    results.records[0].security = WIFI_SERVICE_SECURITY_OPEN;
    _test_publish(WIFI_SERVICE_MSG_SUB_TYPE_SCAN_SNAPSHOT, &results,
                  sizeof(results));
    assert(observer.scan_count == 2U);
    assert(!setup_wifi_adapter_has_operation(&adapter));

    wifi_service_scan_snapshot_t stale = results;
    stale.generation = 2U;
    _test_publish(WIFI_SERVICE_MSG_SUB_TYPE_SCAN_SNAPSHOT, &stale,
                  sizeof(stale));
    assert(observer.scan_count == 2U);
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(_test_active_subscriptions() == 0U);
}

static void _test_connect_scrub_and_rollback(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    uint8_t password[WIFI_SERVICE_PASSWORD_MAX_BYTES];
    memset(password, 'x', sizeof(password));
    assert(setup_wifi_adapter_connect(&adapter, "Test AP", 7U,
                                      WIFI_SERVICE_SECURITY_PERSONAL,
                                      password, 8U) == ESP_OK);
    for (size_t index = 0; index < sizeof(password); ++index)
    {
        assert(password[index] == 0U);
    }
    assert(memcmp(s_password, "xxxxxxxx", 8U) == 0);
    wifi_service_status_snapshot_t done =
        _test_status(4U, WIFI_SERVICE_STATE_IDLE);
    _test_publish(WIFI_SERVICE_MSG_SUB_TYPE_STATUS_SNAPSHOT, &done,
                  sizeof(done));
    assert(!setup_wifi_adapter_has_operation(&adapter));
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);

    _test_reset();
    s_fail_subscribe_at = 2U;
    memset(&adapter, 0, sizeof(adapter));
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) ==
           ESP_ERR_NO_MEM);
    assert(_test_active_subscriptions() == 0U);
}

static void _test_close_retries_unsubscribe(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };

    _test_reset();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    s_unsubscribe_result = ESP_FAIL;
    assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
    assert(adapter.status_subscription != EVENT_BUS_SUB_HANDLE_INVALID);
    assert(adapter.scan_subscription == EVENT_BUS_SUB_HANDLE_INVALID);
    assert(_test_active_subscriptions() == 1U);
    assert(s_unsubscribe_calls == 2U);
    assert(s_session_close_calls == 1U);
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(_test_active_subscriptions() == 0U);
    assert(s_unsubscribe_calls == 3U);
    assert(s_session_close_calls == 1U);
    assert(adapter.user_data == NULL);
}

static void _test_close_retries_cancel(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };

    _test_reset();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    assert(setup_wifi_adapter_scan(&adapter) == ESP_OK);
    s_cancel_result = ESP_FAIL;
    s_session_close_result = ESP_FAIL;
    assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
    assert(setup_wifi_adapter_is_open(&adapter));
    assert(setup_wifi_adapter_has_operation(&adapter));
    assert(_test_active_subscriptions() == 0U);
    assert(s_unsubscribe_calls == 2U);
    assert(s_cancel_calls == 1U);
    assert(s_session_close_calls == 1U);

    s_cancel_result = ESP_OK;
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(!setup_wifi_adapter_is_open(&adapter));
    assert(!setup_wifi_adapter_has_operation(&adapter));
    assert(s_unsubscribe_calls == 2U);
    assert(s_cancel_calls == 2U);
    assert(s_session_close_calls == 2U);
    assert(adapter.user_data == NULL);
}

static void _test_close_retries_session(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };

    _test_reset();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    s_session_close_result = ESP_FAIL;
    assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
    assert(setup_wifi_adapter_is_open(&adapter));
    assert(!setup_wifi_adapter_has_operation(&adapter));
    assert(_test_active_subscriptions() == 0U);
    assert(s_unsubscribe_calls == 2U);
    assert(s_cancel_calls == 0U);
    assert(s_session_close_calls == 1U);

    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(!setup_wifi_adapter_is_open(&adapter));
    assert(s_unsubscribe_calls == 2U);
    assert(s_cancel_calls == 0U);
    assert(s_session_close_calls == 2U);
    assert(adapter.user_data == NULL);
}

static void _test_close_preserves_owner_across_persistent_failure(void)
{
    setup_wifi_adapter_t adapter = {0};
    test_observer_t observer = {0};
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _test_status_callback,
        .scan = _test_scan_callback,
    };

    _test_reset();
    assert(setup_wifi_adapter_open(&adapter, &callbacks, &observer) == ESP_OK);
    for (int attempt = 0; attempt < 3; attempt++)
    {
        s_session_close_result = ESP_FAIL;
        assert(setup_wifi_adapter_close(&adapter) == ESP_FAIL);
        assert(setup_wifi_adapter_is_open(&adapter));
        assert(adapter.user_data == &observer);
    }
    assert(s_session_close_calls == 3U);

    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(!setup_wifi_adapter_is_open(&adapter));
    assert(adapter.user_data == NULL);
    assert(s_session_close_calls == 4U);
}

int main(void)
{
    _test_reset();
    _test_open_and_filter();
    _test_connect_scrub_and_rollback();
    _test_close_retries_unsubscribe();
    _test_close_retries_cancel();
    _test_close_retries_session();
    _test_close_preserves_owner_across_persistent_failure();
    return 0;
}
