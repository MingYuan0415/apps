#include "setup_wifi_adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(bool) == 1U, "snapshot boolean validation requires bytes");
_Static_assert(sizeof(connectivity_manager_status_snapshot_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "connectivity status exceeds the event-bus envelope");
_Static_assert(sizeof(connectivity_manager_scan_snapshot_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "connectivity scan exceeds the event-bus envelope");

static void _setup_wifi_adapter_secure_zero(void *data, size_t size)
{
    volatile uint8_t *bytes = data;
    while (size > 0U)
    {
        *bytes = 0U;
        ++bytes;
        --size;
    }
}

static bool _setup_wifi_adapter_terminal_result(esp_err_t result)
{
    return result == ESP_OK || result == ESP_ERR_NOT_FOUND ||
           result == ESP_ERR_INVALID_STATE || result == ESP_ERR_INVALID_ARG;
}

static void _setup_wifi_adapter_record_error(esp_err_t *first_error,
        esp_err_t result)
{
    if (*first_error == ESP_OK &&
            !_setup_wifi_adapter_terminal_result(result))
    {
        *first_error = result;
    }
}

static bool _setup_wifi_adapter_context_is_free(
    const setup_wifi_adapter_t *adapter)
{
    return adapter->operation_id == 0U &&
           adapter->operation_kind == SETUP_WIFI_OPERATION_NONE &&
           adapter->status_subscription == EVENT_BUS_SUB_HANDLE_INVALID &&
           adapter->scan_subscription == EVENT_BUS_SUB_HANDLE_INVALID;
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

static bool _setup_wifi_adapter_scan_valid(
    const connectivity_manager_scan_snapshot_t *snapshot)
{
    if (snapshot->generation == 0U ||
            snapshot->record_count > CONNECTIVITY_MANAGER_MAX_SCAN_RECORDS)
    {
        return false;
    }
    for (size_t index = 0U; index < snapshot->record_count; ++index)
    {
        const connectivity_manager_scan_record_t *record =
            &snapshot->records[index];
        if (record->security < CONNECTIVITY_MANAGER_SECURITY_OPEN ||
                record->security > CONNECTIVITY_MANAGER_SECURITY_UNSUPPORTED ||
                memchr(record->ssid, '\0', sizeof(record->ssid)) == NULL ||
                record->ssid[0] == '\0')
        {
            return false;
        }
    }
    return true;
}

static bool _setup_wifi_adapter_status_terminal(
    const connectivity_manager_status_snapshot_t *snapshot)
{
    return snapshot->operation_complete;
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
                                 snapshot->operation_id ==
                                 adapter->operation_id;
    if (snapshot->operation_complete && snapshot->operation_id != 0U &&
            !exact_operation)
    {
        return;
    }
    const setup_wifi_operation_kind_t kind = exact_operation ?
        adapter->operation_kind : SETUP_WIFI_OPERATION_NONE;
    if (exact_operation && _setup_wifi_adapter_status_terminal(snapshot))
    {
        adapter->operation_id = 0U;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }
    adapter->callbacks.status(
        snapshot, exact_operation ? SETUP_WIFI_STATUS_OPERATION :
        SETUP_WIFI_STATUS_GLOBAL, kind, adapter->user_data);
}

static void _setup_wifi_adapter_accept_scan(
    setup_wifi_adapter_t *adapter,
    const connectivity_manager_scan_snapshot_t *snapshot)
{
    if (snapshot->generation <= adapter->last_scan_generation ||
            adapter->operation_id == 0U ||
            adapter->operation_kind != SETUP_WIFI_OPERATION_SCAN ||
            snapshot->operation_id != adapter->operation_id)
    {
        return;
    }
    adapter->last_scan_generation = snapshot->generation;
    if (!snapshot->running)
    {
        adapter->operation_id = 0U;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }
    adapter->callbacks.scan(snapshot, adapter->user_data);
}

static void _setup_wifi_adapter_status_event(
    event_bus_msg_id_t msg_id, uint32_t sub_type,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_wifi_adapter_t *adapter = user_data;
    if (adapter == NULL || msg_id != CONNECTIVITY_MANAGER_MSG ||
            sub_type !=
            CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT ||
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

static void _setup_wifi_adapter_scan_event(
    event_bus_msg_id_t msg_id, uint32_t sub_type,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_wifi_adapter_t *adapter = user_data;
    if (adapter == NULL || msg_id != CONNECTIVITY_MANAGER_MSG ||
            sub_type != CONNECTIVITY_MANAGER_MSG_SUB_TYPE_SCAN_SNAPSHOT ||
            payload == NULL ||
            payload_size != sizeof(connectivity_manager_scan_snapshot_t))
    {
        return;
    }
    connectivity_manager_scan_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    if (_setup_wifi_adapter_scan_valid(&snapshot))
    {
        _setup_wifi_adapter_accept_scan(adapter, &snapshot);
    }
}

static esp_err_t _setup_wifi_adapter_begin_operation(
    setup_wifi_adapter_t *adapter, setup_wifi_operation_kind_t kind,
    connectivity_manager_operation_id_t operation_id)
{
    if (operation_id == 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    adapter->operation_id = operation_id;
    adapter->operation_kind = kind;
    return ESP_OK;
}

static esp_err_t _setup_wifi_adapter_subscribe(setup_wifi_adapter_t *adapter)
{
    esp_err_t result = event_bus_subscribe(
                           CONNECTIVITY_MANAGER_MSG,
                           CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                           _setup_wifi_adapter_status_event, adapter,
                           EVENT_BUS_DISPATCH_UI,
                           &adapter->status_subscription);
    if (result == ESP_OK)
    {
        result = event_bus_subscribe(
                     CONNECTIVITY_MANAGER_MSG,
                     CONNECTIVITY_MANAGER_MSG_SUB_TYPE_SCAN_SNAPSHOT,
                     _setup_wifi_adapter_scan_event, adapter,
                     EVENT_BUS_DISPATCH_UI,
                     &adapter->scan_subscription);
    }
    return result;
}

static esp_err_t _setup_wifi_adapter_load_initial(
    setup_wifi_adapter_t *adapter)
{
    connectivity_manager_status_snapshot_t status;
    esp_err_t result = connectivity_manager_get_status(&status);
    if (result != ESP_OK ||
            !_setup_wifi_adapter_status_bools_valid(&status) ||
            !_setup_wifi_adapter_status_valid(&status))
    {
        return result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE;
    }
    connectivity_manager_scan_snapshot_t scan;
    result = connectivity_manager_get_scan_snapshot(&scan);
    if (result != ESP_OK || !_setup_wifi_adapter_scan_valid(&scan))
    {
        return result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE;
    }
    adapter->last_status_generation = status.generation;
    adapter->last_scan_generation = scan.generation;
    adapter->callbacks.status(&status, SETUP_WIFI_STATUS_GLOBAL,
                              SETUP_WIFI_OPERATION_NONE,
                              adapter->user_data);
    return ESP_OK;
}

esp_err_t setup_wifi_adapter_open(
    setup_wifi_adapter_t *adapter,
    const setup_wifi_adapter_callbacks_t *callbacks,
    void *user_data)
{
    if (adapter == NULL || callbacks == NULL || callbacks->status == NULL ||
            callbacks->scan == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_setup_wifi_adapter_context_is_free(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->callbacks = *callbacks;
    adapter->user_data = user_data;
    esp_err_t result = _setup_wifi_adapter_subscribe(adapter);
    if (result == ESP_OK)
    {
        result = _setup_wifi_adapter_load_initial(adapter);
    }
    if (result != ESP_OK)
    {
        (void)setup_wifi_adapter_close(adapter);
    }
    return result;
}

esp_err_t setup_wifi_adapter_scan(setup_wifi_adapter_t *adapter)
{
    if (!setup_wifi_adapter_is_open(adapter) || adapter->operation_id != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    connectivity_manager_operation_id_t operation_id = 0U;
    esp_err_t result = connectivity_manager_request_scan(&operation_id);
    return result == ESP_OK ? _setup_wifi_adapter_begin_operation(
               adapter, SETUP_WIFI_OPERATION_SCAN, operation_id) : result;
}

esp_err_t setup_wifi_adapter_connect(
    setup_wifi_adapter_t *adapter, const char *ssid, size_t ssid_length,
    connectivity_manager_security_t security,
    uint8_t password[CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES],
    size_t password_length)
{
    if (password == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (setup_wifi_adapter_is_open(adapter) && adapter->operation_id == 0U)
    {
        const connectivity_manager_credentials_t credentials =
        {
            .ssid = ssid,
            .ssid_length = ssid_length,
            .password = (const char *)password,
            .password_length = password_length,
            .security = security,
        };
        connectivity_manager_operation_id_t operation_id = 0U;
        result = connectivity_manager_request_connect(&credentials,
                 &operation_id);
        if (result == ESP_OK)
        {
            result = _setup_wifi_adapter_begin_operation(
                         adapter, SETUP_WIFI_OPERATION_CONNECT, operation_id);
        }
    }
    _setup_wifi_adapter_secure_zero(
        password, CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES);
    return result;
}

static esp_err_t _setup_wifi_adapter_simple_operation(
    setup_wifi_adapter_t *adapter, setup_wifi_operation_kind_t kind,
    esp_err_t (*request)(connectivity_manager_operation_id_t *))
{
    if (!setup_wifi_adapter_is_open(adapter) || adapter->operation_id != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    connectivity_manager_operation_id_t operation_id = 0U;
    esp_err_t result = request(&operation_id);
    return result == ESP_OK ? _setup_wifi_adapter_begin_operation(
               adapter, kind, operation_id) : result;
}

esp_err_t setup_wifi_adapter_disconnect(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_simple_operation(
               adapter, SETUP_WIFI_OPERATION_DISCONNECT,
               connectivity_manager_request_disconnect);
}

esp_err_t setup_wifi_adapter_reconnect_saved(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_simple_operation(
               adapter, SETUP_WIFI_OPERATION_CONNECT,
               connectivity_manager_request_reconnect_saved);
}

esp_err_t setup_wifi_adapter_forget(setup_wifi_adapter_t *adapter)
{
    return _setup_wifi_adapter_simple_operation(
               adapter, SETUP_WIFI_OPERATION_DISCONNECT,
               connectivity_manager_request_forget);
}

esp_err_t setup_wifi_adapter_set_auto_connect(setup_wifi_adapter_t *adapter,
        bool enabled)
{
    if (!setup_wifi_adapter_is_open(adapter) || adapter->operation_id != 0U)
    {
        return ESP_ERR_INVALID_STATE;
    }
    connectivity_manager_operation_id_t operation_id = 0U;
    esp_err_t result = connectivity_manager_set_auto_connect(
                           enabled, &operation_id);
    return result == ESP_OK ? _setup_wifi_adapter_begin_operation(
               adapter, SETUP_WIFI_OPERATION_POLICY, operation_id) : result;
}

esp_err_t setup_wifi_adapter_cancel(setup_wifi_adapter_t *adapter)
{
    if (!setup_wifi_adapter_is_open(adapter))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!setup_wifi_adapter_has_operation(adapter))
    {
        return ESP_ERR_NOT_FOUND;
    }
    return connectivity_manager_cancel(adapter->operation_id);
}

esp_err_t setup_wifi_adapter_close(setup_wifi_adapter_t *adapter)
{
    if (adapter == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t first_error = ESP_OK;
    if (adapter->status_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        const esp_err_t result = event_bus_unsubscribe(
                                     adapter->status_subscription);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_result(result))
        {
            adapter->status_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    if (adapter->scan_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        const esp_err_t result = event_bus_unsubscribe(
                                     adapter->scan_subscription);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_result(result))
        {
            adapter->scan_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    if (adapter->operation_id != 0U)
    {
        const esp_err_t result = connectivity_manager_cancel(
                                     adapter->operation_id);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_result(result))
        {
            adapter->operation_id = 0U;
            adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
        }
    }
    if (_setup_wifi_adapter_context_is_free(adapter))
    {
        memset(adapter, 0, sizeof(*adapter));
    }
    return first_error;
}

bool setup_wifi_adapter_is_open(const setup_wifi_adapter_t *adapter)
{
    return adapter != NULL &&
           adapter->status_subscription != EVENT_BUS_SUB_HANDLE_INVALID &&
           adapter->scan_subscription != EVENT_BUS_SUB_HANDLE_INVALID;
}

bool setup_wifi_adapter_has_operation(const setup_wifi_adapter_t *adapter)
{
    return setup_wifi_adapter_is_open(adapter) &&
           adapter->operation_id != 0U &&
           adapter->operation_kind != SETUP_WIFI_OPERATION_NONE;
}
