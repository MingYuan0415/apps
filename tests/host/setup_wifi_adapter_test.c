#include "setup_wifi_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct test_observer
{
    unsigned callbacks;
    setup_wifi_status_scope_t scope;
    setup_wifi_operation_kind_t kind;
    connectivity_manager_status_snapshot_t status;
} test_observer_t;

static event_bus_cb_t s_subscriber;
static void *s_subscriber_context;
static connectivity_manager_status_snapshot_t s_status;
static connectivity_manager_operation_id_t s_next_operation;
static connectivity_manager_operation_id_t s_canceled_operation;
static bool s_auto_connect_value;

static const uint8_t s_connectivity_manager_message;
const event_bus_msg_id_t CONNECTIVITY_MANAGER_MSG =
    &s_connectivity_manager_message;

esp_err_t event_bus_subscribe(
    event_bus_msg_id_t message_id, uint32_t subtype,
    event_bus_cb_t subscriber, void *user_data,
    event_bus_dispatch_context_t dispatch, event_bus_sub_handle_t *handle)
{
    assert(message_id == CONNECTIVITY_MANAGER_MSG);
    assert(subtype == CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT);
    assert(dispatch == EVENT_BUS_DISPATCH_UI);
    assert(subscriber != NULL && handle != NULL);
    s_subscriber = subscriber;
    s_subscriber_context = user_data;
    *handle = 1U;
    return ESP_OK;
}

esp_err_t event_bus_unsubscribe(event_bus_sub_handle_t handle)
{
    assert(handle == 1U);
    s_subscriber = NULL;
    s_subscriber_context = NULL;
    return ESP_OK;
}

esp_err_t connectivity_manager_get_status(
    connectivity_manager_status_snapshot_t *status)
{
    *status = s_status;
    return ESP_OK;
}

static esp_err_t _admit(connectivity_manager_operation_id_t *operation_id)
{
    *operation_id = s_next_operation++;
    return ESP_OK;
}

esp_err_t connectivity_manager_request_disconnect(
    connectivity_manager_operation_id_t *operation_id)
{
    return _admit(operation_id);
}

esp_err_t connectivity_manager_request_reconnect_saved(
    connectivity_manager_operation_id_t *operation_id)
{
    return _admit(operation_id);
}

esp_err_t connectivity_manager_request_forget(
    connectivity_manager_operation_id_t *operation_id)
{
    return _admit(operation_id);
}

esp_err_t connectivity_manager_set_auto_connect(
    bool enabled, connectivity_manager_operation_id_t *operation_id)
{
    s_auto_connect_value = enabled;
    return _admit(operation_id);
}

esp_err_t connectivity_manager_cancel(
    connectivity_manager_operation_id_t operation_id)
{
    s_canceled_operation = operation_id;
    return ESP_OK;
}

static void _status_callback(
    const connectivity_manager_status_snapshot_t *status,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t kind,
    void *user_data)
{
    test_observer_t *observer = user_data;
    ++observer->callbacks;
    observer->scope = scope;
    observer->kind = kind;
    observer->status = *status;
}

static void _reset(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.generation = 1U;
    s_status.available = true;
    s_status.radio_available = true;
    s_status.state = CONNECTIVITY_MANAGER_STATE_IDLE;
    s_status.failure = CONNECTIVITY_MANAGER_FAILURE_NONE;
    s_status.last_error = ESP_OK;
    s_next_operation = 10U;
    s_canceled_operation = 0U;
    s_auto_connect_value = false;
    s_subscriber = NULL;
    s_subscriber_context = NULL;
}

static void _emit(const connectivity_manager_status_snapshot_t *status)
{
    assert(s_subscriber != NULL);
    s_subscriber(CONNECTIVITY_MANAGER_MSG,
                 CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                 status, sizeof(*status), s_subscriber_context);
}

static void _open(setup_wifi_adapter_t *adapter, test_observer_t *observer)
{
    memset(adapter, 0, sizeof(*adapter));
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _status_callback,
    };
    assert(setup_wifi_adapter_open(adapter, &callbacks, observer) == ESP_OK);
    assert(observer->callbacks == 1U);
    assert(observer->scope == SETUP_WIFI_STATUS_GLOBAL);
}

static void _test_saved_network_operations(void)
{
    test_observer_t observer = {0};
    setup_wifi_adapter_t adapter;
    _open(&adapter, &observer);

    assert(setup_wifi_adapter_reconnect_saved(&adapter) == ESP_OK);
    assert(setup_wifi_adapter_has_operation(&adapter));
    assert(setup_wifi_adapter_disconnect(&adapter) == ESP_ERR_INVALID_STATE);

    connectivity_manager_status_snapshot_t terminal = s_status;
    terminal.generation = 2U;
    terminal.operation_id = 10U;
    terminal.operation_complete = true;
    terminal.state = CONNECTIVITY_MANAGER_STATE_IP_READY;
    memcpy(terminal.ssid, "saved", sizeof("saved"));
    _emit(&terminal);
    assert(observer.callbacks == 2U);
    assert(observer.scope == SETUP_WIFI_STATUS_OPERATION);
    assert(observer.kind == SETUP_WIFI_OPERATION_RECONNECT);
    assert(!setup_wifi_adapter_has_operation(&adapter));

    assert(setup_wifi_adapter_disconnect(&adapter) == ESP_OK);
    assert(setup_wifi_adapter_cancel(&adapter) == ESP_OK);
    assert(s_canceled_operation == 11U);
    terminal.generation = 3U;
    terminal.operation_id = 11U;
    terminal.operation_complete = true;
    terminal.last_error = ESP_ERR_NOT_FINISHED;
    _emit(&terminal);
    assert(observer.kind == SETUP_WIFI_OPERATION_DISCONNECT);

    assert(setup_wifi_adapter_forget(&adapter) == ESP_OK);
    terminal.generation = 4U;
    terminal.operation_id = 12U;
    terminal.last_error = ESP_OK;
    _emit(&terminal);
    assert(observer.kind == SETUP_WIFI_OPERATION_FORGET);

    assert(setup_wifi_adapter_set_auto_connect(&adapter, true) == ESP_OK);
    assert(s_auto_connect_value);
    terminal.generation = 5U;
    terminal.operation_id = 13U;
    _emit(&terminal);
    assert(observer.kind == SETUP_WIFI_OPERATION_POLICY);
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
}

static void _test_old_operation_terminal_is_filtered(void)
{
    test_observer_t observer = {0};
    setup_wifi_adapter_t adapter;
    _open(&adapter, &observer);
    assert(setup_wifi_adapter_reconnect_saved(&adapter) == ESP_OK);

    connectivity_manager_status_snapshot_t old = s_status;
    old.generation = 2U;
    old.operation_id = 99U;
    old.operation_complete = true;
    _emit(&old);
    assert(observer.callbacks == 1U);
    assert(setup_wifi_adapter_has_operation(&adapter));
    assert(setup_wifi_adapter_close(&adapter) == ESP_OK);
    assert(s_canceled_operation == 10U);
}

int main(void)
{
    _reset();
    _test_saved_network_operations();
    _reset();
    _test_old_operation_terminal_is_filtered();
    puts("setup Wi-Fi adapter tests passed");
    return 0;
}
