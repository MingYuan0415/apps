#include "setup_wifi_adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(bool) == 1U, "snapshot boolean validation requires bytes");
_Static_assert(sizeof(connectivity_manager_status_snapshot_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "connectivity status exceeds the event-bus envelope");

static bool _setup_wifi_adapter_terminal_result(esp_err_t result)
{
    return result == ESP_OK || result == ESP_ERR_NOT_FOUND ||
           result == ESP_ERR_INVALID_STATE || result == ESP_ERR_INVALID_ARG;
}

static bool _setup_wifi_adapter_status_bools_valid(const void *payload)
{
    const uint8_t *bytes = payload;
    return bytes[offsetof(connectivity_manager_status_snapshot_t,
                          available)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          radio_available)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          saved_profile)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          profile_persisted)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          auto_connect)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          manual_hold)] <= 1U &&
           bytes[offsetof(connectivity_manager_status_snapshot_t,
                          operation_complete)] <= 1U;
}

static bool _setup_wifi_adapter_status_valid(
    const connectivity_manager_status_snapshot_t *snapshot)
{
    return snapshot->generation != 0U &&
           (!snapshot->operation_complete || snapshot->operation_id != 0U) &&
           snapshot->state >= CONNECTIVITY_MANAGER_STATE_OFFLINE &&
           snapshot->state <= CONNECTIVITY_MANAGER_STATE_SUSPENDED &&
           snapshot->failure >= CONNECTIVITY_MANAGER_FAILURE_NONE &&
           snapshot->failure <= CONNECTIVITY_MANAGER_FAILURE_INTERNAL &&
           memchr(snapshot->ssid, '\0', sizeof(snapshot->ssid)) != NULL;
}

static void _setup_wifi_adapter_accept_status(
    setup_wifi_adapter_t *adapter,
    const connectivity_manager_status_snapshot_t *snapshot)
{
    if (snapshot->generation <= adapter->last_status_generation)
    {
        return;
    }
    adapter->last_status_generation = snapshot->generation;
    const bool exact_operation = adapter->operation_id != 0U &&
                                 snapshot->operation_id == adapter->operation_id;
    if (snapshot->operation_complete && snapshot->operation_id != 0U &&
            !exact_operation)
    {
        return;
    }
    const setup_wifi_operation_kind_t kind = exact_operation ?
        adapter->operation_kind : SETUP_WIFI_OPERATION_NONE;
    if (exact_operation && snapshot->operation_complete)
    {
        adapter->operation_id = 0U;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }
    adapter->callbacks.status(
        snapshot, exact_operation ? SETUP_WIFI_STATUS_OPERATION :
        SETUP_WIFI_STATUS_GLOBAL, kind, adapter->user_data);
}

static void _setup_wifi_adapter_status_event(
    event_bus_msg_id_t message_id, uint32_t subtype,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_wifi_adapter_t *adapter = user_data;
    if (adapter == NULL || message_id != CONNECTIVITY_MANAGER_MSG ||
            subtype != CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT ||
            payload == NULL ||
            payload_size != sizeof(connectivity_manager_status_snapshot_t) ||
            !_setup_wifi_adapter_status_bools_valid(payload))
    {
        return;
    }
    connectivity_manager_status_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    if (_setup_wifi_adapter_status_valid(&snapshot))
    {
        _setup_wifi_adapter_accept_status(adapter, &snapshot);
    }
}

static esp_err_t _setup_wifi_adapter_begin(
    setup_wifi_adapter_t *adapter, setup_wifi_operation_kind_t kind,
    esp_err_t (*request)(connectivity_manager_operation_id_t *))
{
    if (!setup_wifi_adapter_is_open(adapter) ||
            setup_wifi_adapter_has_operation(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }
    connectivity_manager_operation_id_t operation_id = 0U;
    esp_err_t result = request(&operation_id);
    if (result == ESP_OK && operation_id == 0U)
    {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result == ESP_OK)
    {
        adapter->operation_id = operation_id;
        adapter->operation_kind = kind;
    }
    return result;
}

esp_err_t setup_wifi_adapter_open(
    setup_wifi_adapter_t *adapter,
    const setup_wifi_adapter_callbacks_t *callbacks,
    void *user_data)
{
    if (adapter == NULL || callbacks == NULL || callbacks->status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->operation_id != 0U ||
            adapter->status_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return ESP_ERR_INVALID_STATE;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->callbacks = *callbacks;
    adapter->user_data = user_data;
    esp_err_t result = event_bus_subscribe(
                           CONNECTIVITY_MANAGER_MSG,
                           CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                           _setup_wifi_adapter_status_event, adapter,
                           EVENT_BUS_DISPATCH_UI,
                           &adapter->status_subscription);
    connectivity_manager_status_snapshot_t status;
    if (result == ESP_OK)
    {
        result = connectivity_manager_get_status(&status);
    }
    if (result == ESP_OK &&
            (!_setup_wifi_adapter_status_bools_valid(&status) ||
             !_setup_wifi_adapter_status_valid(&status)))
    {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result == ESP_OK)
    {
        adapter->last_status_generation = status.generation;
        adapter->callbacks.status(&status, SETUP_WIFI_STATUS_GLOBAL,
                                  SETUP_WIFI_OPERATION_NONE,
                                  adapter->user_data);
    }
    else
    {
        (void)setup_wifi_adapter_close(adapter);
    }
    return result;
}

esp_err_t setup_wifi_adapter_disconnect(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_begin(
               adapter, SETUP_WIFI_OPERATION_DISCONNECT,
               connectivity_manager_request_disconnect);
}

esp_err_t setup_wifi_adapter_reconnect_saved(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_begin(
               adapter, SETUP_WIFI_OPERATION_RECONNECT,
               connectivity_manager_request_reconnect_saved);
}

esp_err_t setup_wifi_adapter_forget(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_begin(
               adapter, SETUP_WIFI_OPERATION_FORGET,
               connectivity_manager_request_forget);
}

esp_err_t setup_wifi_adapter_set_auto_connect(setup_wifi_adapter_t *adapter,
        bool enabled)
{
    if (!setup_wifi_adapter_is_open(adapter) ||
            setup_wifi_adapter_has_operation(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }
    connectivity_manager_operation_id_t operation_id = 0U;
    esp_err_t result = connectivity_manager_set_auto_connect(
                           enabled, &operation_id);
    if (result == ESP_OK && operation_id == 0U)
    {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result == ESP_OK)
    {
        adapter->operation_id = operation_id;
        adapter->operation_kind = SETUP_WIFI_OPERATION_POLICY;
    }
    return result;
}

esp_err_t setup_wifi_adapter_cancel(setup_wifi_adapter_t *adapter)
{
    if (!setup_wifi_adapter_is_open(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return setup_wifi_adapter_has_operation(adapter) ?
           connectivity_manager_cancel(adapter->operation_id) :
           ESP_ERR_NOT_FOUND;
}

esp_err_t setup_wifi_adapter_close(setup_wifi_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    if (adapter->status_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        result = event_bus_unsubscribe(adapter->status_subscription);
        if (_setup_wifi_adapter_terminal_result(result))
        {
            adapter->status_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            result = ESP_OK;
        }
    }
    if (result == ESP_OK && adapter->operation_id != 0U)
    {
        const esp_err_t cancel_result = connectivity_manager_cancel(
                                            adapter->operation_id);
        if (!_setup_wifi_adapter_terminal_result(cancel_result))
        {
            result = cancel_result;
        }
        else
        {
            adapter->operation_id = 0U;
            adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
        }
    }
    if (result == ESP_OK)
    {
        memset(adapter, 0, sizeof(*adapter));
    }
    return result;
}

bool setup_wifi_adapter_is_open(const setup_wifi_adapter_t *adapter)
{
    return adapter != NULL &&
           adapter->status_subscription != EVENT_BUS_SUB_HANDLE_INVALID;
}

bool setup_wifi_adapter_has_operation(const setup_wifi_adapter_t *adapter)
{
    return setup_wifi_adapter_is_open(adapter) &&
           adapter->operation_id != 0U &&
           adapter->operation_kind != SETUP_WIFI_OPERATION_NONE;
}
