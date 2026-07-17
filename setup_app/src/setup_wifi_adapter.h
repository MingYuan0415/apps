#ifndef __SETUP_WIFI_ADAPTER_H__
#define __SETUP_WIFI_ADAPTER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_bus.h"
#include "wifi_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Wi-Fi operation currently owned by the setup page. */
typedef enum
{
    SETUP_WIFI_OPERATION_NONE = 0, /**< No operation is owned. */
    SETUP_WIFI_OPERATION_SCAN,     /**< Scan operation is active. */
    SETUP_WIFI_OPERATION_CONNECT,  /**< Connect operation is active. */
    SETUP_WIFI_OPERATION_DISCONNECT, /**< Disconnect operation is active. */
} setup_wifi_operation_kind_t;

/** @brief Scope of a delivered Wi-Fi status snapshot. */
typedef enum
{
    SETUP_WIFI_STATUS_GLOBAL = 0, /**< Global link-state update. */
    SETUP_WIFI_STATUS_OPERATION,  /**< Exact owned-operation update. */
} setup_wifi_status_scope_t;

/**
 * @brief Receive a validated Wi-Fi status snapshot.
 *
 * @note The snapshot pointer is valid only during the callback.
 *
 * @param snapshot is the validated immutable status snapshot.
 * @param scope identifies a global or exact-operation update.
 * @param operation_kind identifies the exact operation or NONE.
 * @param user_data is the adapter callback context.
 */
typedef void (*setup_wifi_status_cb_t)(
    const wifi_service_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind,
    void *user_data);

/**
 * @brief Receive a validated snapshot for the adapter's exact scan operation.
 *
 * @note The snapshot pointer is valid only during the callback.
 *
 * @param snapshot is the validated immutable scan snapshot.
 * @param user_data is the adapter callback context.
 */
typedef void (*setup_wifi_scan_cb_t)(
    const wifi_service_scan_snapshot_t *snapshot,
    void *user_data);

/** @brief Setup-page callbacks invoked on the LVGL worker. */
typedef struct setup_wifi_adapter_callbacks
{
    setup_wifi_status_cb_t status; /**< Status snapshot consumer. */
    setup_wifi_scan_cb_t scan;     /**< Scan snapshot consumer. */
} setup_wifi_adapter_callbacks_t;

/**
 * @brief Page-private ownership state for one Wi-Fi setup session.
 *
 * @note A zeroed context owns no resources. Every API is LVGL-worker-only.
 */
typedef struct setup_wifi_adapter
{
    wifi_service_session_id_t session_id;     /**< Owned service session. */
    wifi_service_operation_id_t operation_id; /**< Active exact operation. */
    setup_wifi_operation_kind_t operation_kind; /**< Active operation class. */
    event_bus_sub_handle_t status_subscription; /**< Status subscription. */
    event_bus_sub_handle_t scan_subscription; /**< Scan subscription. */
    uint64_t last_status_generation; /**< Last accepted status generation. */
    uint64_t last_scan_generation;   /**< Last accepted scan generation. */
    setup_wifi_adapter_callbacks_t callbacks; /**< Page callback table. */
    void *user_data; /**< Opaque callback context. */
} setup_wifi_adapter_t;

/**
 * @brief Subscribe to snapshots and open one Wi-Fi service session.
 *
 * @param adapter is a zeroed page-private adapter context.
 * @param callbacks contains required LVGL-worker consumers.
 * @param user_data is retained as callback context.
 *
 * @return ESP_OK when ready, otherwise an ESP-IDF error after rollback.
 */
esp_err_t setup_wifi_adapter_open(
    setup_wifi_adapter_t *adapter,
    const setup_wifi_adapter_callbacks_t *callbacks,
    void *user_data);

/**
 * @brief Request one non-blocking Wi-Fi scan.
 * @param adapter owns the open Wi-Fi session.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_scan(setup_wifi_adapter_t *adapter);
/**
 * @brief Request one connection and scrub the supplied password before return.
 * @param adapter owns the open Wi-Fi session.
 * @param ssid points to the selected SSID bytes.
 * @param ssid_length is the SSID byte count.
 * @param security is the selected network security class.
 * @param password is the fixed-capacity secret buffer scrubbed before return.
 * @param password_length is the secret byte count.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_connect(
    setup_wifi_adapter_t *adapter,
    const char *ssid,
    size_t ssid_length,
    wifi_service_security_t security,
    uint8_t password[WIFI_SERVICE_PASSWORD_MAX_BYTES],
    size_t password_length);
/**
 * @brief Request disconnection of the current global Wi-Fi link.
 * @param adapter owns the open Wi-Fi session.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_disconnect(setup_wifi_adapter_t *adapter);
/**
 * @brief Cancel the exact operation owned by the adapter.
 * @param adapter owns the operation to cancel.
 * @return ESP_OK when cancellation is marked; ESP_ERR_NOT_FOUND when already
 *         terminal; otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_cancel(setup_wifi_adapter_t *adapter);

/**
 * @brief Release subscriptions, the exact operation, and the service session.
 *
 * @note Closing never disconnects an established global link. Unresolved
 *       ownership remains in the context so a later close can retry it.
 *
 * @param adapter owns the resources to release.
 *
 * @return First non-terminal cleanup error, or ESP_OK.
 */
esp_err_t setup_wifi_adapter_close(setup_wifi_adapter_t *adapter);

/**
 * @brief Report whether the adapter owns an open service session.
 * @param adapter is the page-private adapter context.
 * @return true when open; false otherwise.
 */
bool setup_wifi_adapter_is_open(const setup_wifi_adapter_t *adapter);
/**
 * @brief Report whether the adapter owns an exact in-flight operation.
 * @param adapter is the page-private adapter context.
 * @return true when an operation is owned; false otherwise.
 */
bool setup_wifi_adapter_has_operation(const setup_wifi_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* __SETUP_WIFI_ADAPTER_H__ */
