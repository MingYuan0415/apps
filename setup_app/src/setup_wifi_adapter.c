#include "setup_wifi_adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(bool) == 1U, "snapshot boolean validation requires bytes");
_Static_assert(sizeof(wifi_service_status_snapshot_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "WiFi status snapshot exceeds the event-bus envelope");
_Static_assert(sizeof(wifi_service_scan_snapshot_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "WiFi scan snapshot exceeds the event-bus envelope");

static bool _setup_wifi_adapter_terminal_teardown_result(esp_err_t result)
{
    return result == ESP_OK || result == ESP_ERR_NOT_FOUND ||
           result == ESP_ERR_INVALID_STATE || result == ESP_ERR_INVALID_ARG;
}

static void _setup_wifi_adapter_record_error(esp_err_t *first_error,
        esp_err_t result)
{
    if (*first_error == ESP_OK &&
            !_setup_wifi_adapter_terminal_teardown_result(result))
    {
        *first_error = result;
    }
}

static bool _setup_wifi_adapter_context_is_free(
    const setup_wifi_adapter_t *adapter)
{
    return adapter->session_id == 0 && adapter->operation_id == 0 &&
           adapter->operation_kind == SETUP_WIFI_OPERATION_NONE &&
           adapter->status_subscription == EVENT_BUS_SUB_HANDLE_INVALID &&
           adapter->scan_subscription == EVENT_BUS_SUB_HANDLE_INVALID;
}

static bool _setup_wifi_adapter_status_bools_valid(const void *payload)
{
    const uint8_t *bytes = payload;
    return bytes[offsetof(wifi_service_status_snapshot_t, available)] <= 1U &&
           bytes[offsetof(wifi_service_status_snapshot_t,
                          desired_connected)] <= 1U;
}

static bool _setup_wifi_adapter_status_valid(
    const wifi_service_status_snapshot_t *snapshot)
{
    return snapshot->generation != 0 &&
           snapshot->state >= WIFI_SERVICE_STATE_OFFLINE &&
           snapshot->state <= WIFI_SERVICE_STATE_SUSPENDED &&
           ((snapshot->session_id == 0) ==
            (snapshot->operation_id == 0)) &&
           memchr(snapshot->ssid, '\0', sizeof(snapshot->ssid)) != NULL;
}

static bool _setup_wifi_adapter_scan_bools_valid(const void *payload)
{
    const uint8_t *bytes = payload;
    return bytes[offsetof(wifi_service_scan_snapshot_t, truncated)] <= 1U;
}

static bool _setup_wifi_adapter_scan_valid(
    const wifi_service_scan_snapshot_t *snapshot)
{
    if (snapshot->generation == 0 ||
            snapshot->state < WIFI_SERVICE_SCAN_IDLE ||
            snapshot->state > WIFI_SERVICE_SCAN_FAILED ||
            snapshot->record_count > WIFI_SERVICE_MAX_SCAN_RECORDS ||
            ((snapshot->session_id == 0) !=
             (snapshot->operation_id == 0)))
    {
        return false;
    }

    for (size_t index = 0; index < snapshot->record_count; ++index)
    {
        const wifi_service_scan_record_t *record = &snapshot->records[index];
        if (record->security < WIFI_SERVICE_SECURITY_OPEN ||
                record->security > WIFI_SERVICE_SECURITY_UNSUPPORTED ||
                memchr(record->ssid, '\0', sizeof(record->ssid)) == NULL ||
                record->ssid[0] == '\0')
        {
            return false;
        }
    }

    return true;
}

static bool _setup_wifi_adapter_global_state(wifi_service_state_t state)
{
    return state == WIFI_SERVICE_STATE_OFFLINE ||
           state == WIFI_SERVICE_STATE_IDLE ||
           state == WIFI_SERVICE_STATE_SUSPENDED ||
           state == WIFI_SERVICE_STATE_IP_READY;
}

static bool _setup_wifi_adapter_status_terminal(
    setup_wifi_operation_kind_t kind, wifi_service_state_t state)
{
    bool terminal = false;
    if (kind == SETUP_WIFI_OPERATION_CONNECT)
    {
        terminal = state == WIFI_SERVICE_STATE_IP_READY ||
                   state == WIFI_SERVICE_STATE_IDLE ||
                   state == WIFI_SERVICE_STATE_OFFLINE;
    }
    else if (kind == SETUP_WIFI_OPERATION_DISCONNECT)
    {
        terminal = state == WIFI_SERVICE_STATE_IDLE ||
                   state == WIFI_SERVICE_STATE_OFFLINE ||
                   state == WIFI_SERVICE_STATE_IP_READY;
    }
    return terminal;
}

static bool _setup_wifi_adapter_scan_terminal(wifi_service_scan_state_t state)
{
    return state == WIFI_SERVICE_SCAN_RESULTS ||
           state == WIFI_SERVICE_SCAN_CANCELED ||
           state == WIFI_SERVICE_SCAN_FAILED;
}

static void _setup_wifi_adapter_accept_status(
    setup_wifi_adapter_t *adapter,
    const wifi_service_status_snapshot_t *snapshot)
{
    if (snapshot->generation <= adapter->last_status_generation)
    {
        return;
    }

    bool exact_operation = adapter->session_id != 0 &&
                           adapter->operation_id != 0 &&
                           (adapter->operation_kind ==
                            SETUP_WIFI_OPERATION_CONNECT ||
                            adapter->operation_kind ==
                            SETUP_WIFI_OPERATION_DISCONNECT) &&
                           snapshot->session_id == adapter->session_id &&
                           snapshot->operation_id == adapter->operation_id;
    if (!exact_operation && !_setup_wifi_adapter_global_state(snapshot->state))
    {
        return;
    }

    adapter->last_status_generation = snapshot->generation;
    setup_wifi_operation_kind_t kind = exact_operation ?
                                       adapter->operation_kind :
                                       SETUP_WIFI_OPERATION_NONE;
    if (exact_operation &&
            _setup_wifi_adapter_status_terminal(kind, snapshot->state))
    {
        adapter->operation_id = 0;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }

    adapter->callbacks.status(
        snapshot,
        exact_operation ? SETUP_WIFI_STATUS_OPERATION :
        SETUP_WIFI_STATUS_GLOBAL,
        kind, adapter->user_data);
}

static void _setup_wifi_adapter_accept_scan(
    setup_wifi_adapter_t *adapter,
    const wifi_service_scan_snapshot_t *snapshot)
{
    if (snapshot->generation <= adapter->last_scan_generation ||
            adapter->session_id == 0 || adapter->operation_id == 0 ||
            adapter->operation_kind != SETUP_WIFI_OPERATION_SCAN ||
            snapshot->session_id != adapter->session_id ||
            snapshot->operation_id != adapter->operation_id)
    {
        return;
    }

    adapter->last_scan_generation = snapshot->generation;
    if (_setup_wifi_adapter_scan_terminal(snapshot->state))
    {
        adapter->operation_id = 0;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }
    adapter->callbacks.scan(snapshot, adapter->user_data);
}

static void _setup_wifi_adapter_status_event(
    event_bus_msg_id_t msg_id, uint32_t sub_type,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_wifi_adapter_t *adapter = user_data;
    if (adapter == NULL || msg_id != WIFI_SERVICE_MSG ||
            sub_type != WIFI_SERVICE_MSG_SUB_TYPE_STATUS_SNAPSHOT ||
            payload == NULL ||
            payload_size != sizeof(wifi_service_status_snapshot_t) ||
            !_setup_wifi_adapter_status_bools_valid(payload))
    {
        return;
    }

    wifi_service_status_snapshot_t snapshot;
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
    if (adapter == NULL || msg_id != WIFI_SERVICE_MSG ||
            sub_type != WIFI_SERVICE_MSG_SUB_TYPE_SCAN_SNAPSHOT ||
            payload == NULL ||
            payload_size != sizeof(wifi_service_scan_snapshot_t) ||
            !_setup_wifi_adapter_scan_bools_valid(payload))
    {
        return;
    }

    wifi_service_scan_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    if (_setup_wifi_adapter_scan_valid(&snapshot))
    {
        _setup_wifi_adapter_accept_scan(adapter, &snapshot);
    }
}

static esp_err_t _setup_wifi_adapter_begin_operation(
    setup_wifi_adapter_t *adapter, setup_wifi_operation_kind_t kind,
    wifi_service_operation_id_t operation_id)
{
    if (operation_id == 0)
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
                           WIFI_SERVICE_MSG,
                           WIFI_SERVICE_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                           _setup_wifi_adapter_status_event, adapter,
                           EVENT_BUS_DISPATCH_UI,
                           &adapter->status_subscription);
    if (result != ESP_OK)
    {
        return result;
    }
    result = event_bus_subscribe(
                 WIFI_SERVICE_MSG,
                 WIFI_SERVICE_MSG_SUB_TYPE_SCAN_SNAPSHOT,
                 _setup_wifi_adapter_scan_event, adapter,
                 EVENT_BUS_DISPATCH_UI,
                 &adapter->scan_subscription);
    return result;
}

static esp_err_t _setup_wifi_adapter_load_initial(
    setup_wifi_adapter_t *adapter)
{
    wifi_service_status_snapshot_t status;
    esp_err_t result = wifi_service_get_status(&status);
    if (result != ESP_OK ||
            !_setup_wifi_adapter_status_bools_valid(&status) ||
            !_setup_wifi_adapter_status_valid(&status))
    {
        result = result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE;
        return result;
    }
    wifi_service_scan_snapshot_t scan;
    result = wifi_service_get_scan_snapshot(&scan);
    if (result != ESP_OK ||
            !_setup_wifi_adapter_scan_bools_valid(&scan) ||
            !_setup_wifi_adapter_scan_valid(&scan))
    {
        result = result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE;
        return result;
    }

    adapter->last_status_generation = status.generation;
    adapter->last_scan_generation = scan.generation;
    if (_setup_wifi_adapter_global_state(status.state))
    {
        adapter->callbacks.status(&status, SETUP_WIFI_STATUS_GLOBAL,
                                  SETUP_WIFI_OPERATION_NONE,
                                  adapter->user_data);
    }
    return result;
}

esp_err_t setup_wifi_adapter_open(
    setup_wifi_adapter_t *adapter,
    const setup_wifi_adapter_callbacks_t *callbacks,
    void *user_data)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (adapter == NULL || callbacks == NULL || callbacks->status == NULL ||
            callbacks->scan == NULL)
    {
        return result;
    }
    if (!_setup_wifi_adapter_context_is_free(adapter))
    {
        result = ESP_ERR_INVALID_STATE;
        return result;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->callbacks = *callbacks;
    adapter->user_data = user_data;

    result = _setup_wifi_adapter_subscribe(adapter);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    result = wifi_service_session_open(&adapter->session_id);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    result = _setup_wifi_adapter_load_initial(adapter);
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    return result;

cleanup:
    (void)setup_wifi_adapter_close(adapter);
    return result;
}

esp_err_t setup_wifi_adapter_scan(setup_wifi_adapter_t *adapter)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (adapter == NULL || adapter->session_id == 0 ||
            adapter->operation_id != 0)
    {
        return result;
    }
    wifi_service_operation_id_t operation_id = 0;
    result = wifi_service_request_scan(adapter->session_id, &operation_id);
    if (result == ESP_OK)
    {
        result = _setup_wifi_adapter_begin_operation(
                     adapter, SETUP_WIFI_OPERATION_SCAN, operation_id);
    }
    return result;
}

esp_err_t setup_wifi_adapter_connect(
    setup_wifi_adapter_t *adapter,
    const char *ssid,
    size_t ssid_length,
    wifi_service_security_t security,
    uint8_t password[WIFI_SERVICE_PASSWORD_MAX_BYTES],
    size_t password_length)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (password == NULL)
    {
        return result;
    }

    result = ESP_ERR_INVALID_STATE;
    if (adapter != NULL && adapter->session_id != 0 &&
            adapter->operation_id == 0)
    {
        const wifi_service_credentials_t credentials =
        {
            .ssid = ssid,
            .ssid_length = ssid_length,
            .password = (const char *)password,
            .password_length = password_length,
            .security = security,
        };
        wifi_service_operation_id_t operation_id = 0;
        result = wifi_service_request_connect(adapter->session_id,
                                              &credentials, &operation_id);
        if (result == ESP_OK)
        {
            result = _setup_wifi_adapter_begin_operation(
                         adapter, SETUP_WIFI_OPERATION_CONNECT, operation_id);
        }
    }
    wifi_service_secure_zero(password, WIFI_SERVICE_PASSWORD_MAX_BYTES);
    return result;
}

esp_err_t setup_wifi_adapter_disconnect(setup_wifi_adapter_t *adapter)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (adapter == NULL || adapter->session_id == 0 ||
            adapter->operation_id != 0)
    {
        return result;
    }
    wifi_service_operation_id_t operation_id = 0;
    result = wifi_service_request_disconnect(adapter->session_id,
             &operation_id);
    if (result == ESP_OK)
    {
        result = _setup_wifi_adapter_begin_operation(
                     adapter, SETUP_WIFI_OPERATION_DISCONNECT, operation_id);
    }
    return result;
}

esp_err_t setup_wifi_adapter_cancel(setup_wifi_adapter_t *adapter)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (adapter == NULL || adapter->session_id == 0 ||
            adapter->operation_id == 0 ||
            adapter->operation_kind == SETUP_WIFI_OPERATION_NONE)
    {
        return result;
    }
    result = wifi_service_cancel(adapter->session_id, adapter->operation_id);
    if (result == ESP_ERR_NOT_FOUND)
    {
        adapter->operation_id = 0;
        adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
    }
    return result;
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
        if (_setup_wifi_adapter_terminal_teardown_result(result))
        {
            adapter->status_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    if (adapter->scan_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        const esp_err_t result = event_bus_unsubscribe(
                                     adapter->scan_subscription);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_teardown_result(result))
        {
            adapter->scan_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    if (adapter->session_id != 0 && adapter->operation_id != 0 &&
            adapter->operation_kind != SETUP_WIFI_OPERATION_NONE)
    {
        const esp_err_t result = wifi_service_cancel(
                                     adapter->session_id,
                                     adapter->operation_id);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_teardown_result(result))
        {
            adapter->operation_id = 0;
            adapter->operation_kind = SETUP_WIFI_OPERATION_NONE;
        }
    }
    if (adapter->session_id != 0)
    {
        const esp_err_t result = wifi_service_session_close(
                                     adapter->session_id);
        _setup_wifi_adapter_record_error(&first_error, result);
        if (_setup_wifi_adapter_terminal_teardown_result(result))
        {
            adapter->session_id = 0;
            adapter->operation_id = 0;
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
    return adapter != NULL && adapter->session_id != 0;
}

bool setup_wifi_adapter_has_operation(const setup_wifi_adapter_t *adapter)
{
    return adapter != NULL && adapter->session_id != 0 &&
           adapter->operation_id != 0 &&
           adapter->operation_kind != SETUP_WIFI_OPERATION_NONE;
}
