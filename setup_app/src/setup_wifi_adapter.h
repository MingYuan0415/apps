#ifndef __SETUP_WIFI_ADAPTER_H__
#define __SETUP_WIFI_ADAPTER_H__

#include <stdbool.h>
#include <stdint.h>

#include "connectivity_manager.h"
#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Saved-network operation currently tracked by Setup. */
typedef enum
{
    SETUP_WIFI_OPERATION_NONE = 0, /**< No operation is owned. */
    SETUP_WIFI_OPERATION_RECONNECT, /**< Saved-profile reconnect is active. */
    SETUP_WIFI_OPERATION_DISCONNECT, /**< Disconnect is active. */
    SETUP_WIFI_OPERATION_FORGET, /**< Forget-profile operation is active. */
    SETUP_WIFI_OPERATION_POLICY, /**< Auto-connect policy update is active. */
} setup_wifi_operation_kind_t;

/** @brief Scope of one delivered connectivity snapshot. */
typedef enum
{
    SETUP_WIFI_STATUS_GLOBAL = 0, /**< Global connectivity update. */
    SETUP_WIFI_STATUS_OPERATION, /**< Exact owned-operation update. */
} setup_wifi_status_scope_t;

/**
 * @brief Receive one validated connectivity snapshot on the LVGL worker.
 *
 * @note The snapshot pointer is valid only during the callback.
 *
 * @param snapshot is the immutable manager snapshot.
 * @param scope identifies a global or exact-operation update.
 * @param operation_kind identifies the tracked operation or NONE.
 * @param user_data is the adapter callback context.
 */
typedef void (*setup_wifi_status_cb_t)(
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind,
    void *user_data);

/** @brief Setup-page callbacks invoked on the LVGL worker. */
typedef struct setup_wifi_adapter_callbacks
{
    setup_wifi_status_cb_t status; /**< Connectivity snapshot consumer. */
} setup_wifi_adapter_callbacks_t;

/**
 * @brief Page-private subscription and saved-network operation state.
 *
 * @note A zeroed context owns no resources. Every API is LVGL-worker-only.
 */
typedef struct setup_wifi_adapter
{
    connectivity_manager_operation_id_t operation_id; /**< Active operation. */
    setup_wifi_operation_kind_t operation_kind; /**< Active operation class. */
    event_bus_sub_handle_t status_subscription; /**< Manager subscription. */
    uint64_t last_status_generation; /**< Last accepted generation. */
    setup_wifi_adapter_callbacks_t callbacks; /**< Page callback table. */
    void *user_data; /**< Opaque callback context. */
} setup_wifi_adapter_t;

/**
 * @brief Subscribe to connectivity snapshots.
 *
 * @param adapter is a zeroed page-private adapter context.
 * @param callbacks contains the required LVGL-worker consumer.
 * @param user_data is retained as callback context.
 * @return ESP_OK when ready, otherwise an ESP-IDF error after rollback.
 */
esp_err_t setup_wifi_adapter_open(
    setup_wifi_adapter_t *adapter,
    const setup_wifi_adapter_callbacks_t *callbacks,
    void *user_data);

/**
 * @brief Disconnect the current Wi-Fi link and hold offline for this boot.
 * @param adapter owns the open manager subscription.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_disconnect(setup_wifi_adapter_t *adapter);

/**
 * @brief Reconnect immediately using the saved profile.
 * @param adapter owns the open manager subscription.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_reconnect_saved(setup_wifi_adapter_t *adapter);

/**
 * @brief Forget the saved profile and disconnect the current link.
 * @param adapter owns the open manager subscription.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_forget(setup_wifi_adapter_t *adapter);

/**
 * @brief Persistently update the saved profile's auto-connect policy.
 * @param adapter owns the open manager subscription.
 * @param enabled enables or disables automatic connection.
 * @return ESP_OK when admitted, otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_set_auto_connect(setup_wifi_adapter_t *adapter,
        bool enabled);

/**
 * @brief Cancel the exact operation owned by the adapter.
 * @param adapter owns the operation to cancel.
 * @return ESP_OK when cancellation is marked; ESP_ERR_NOT_FOUND when already
 *         terminal; otherwise an ESP-IDF error.
 */
esp_err_t setup_wifi_adapter_cancel(setup_wifi_adapter_t *adapter);

/**
 * @brief Release the manager subscription and tracked operation.
 *
 * @note Closing never disconnects an established global link. Unresolved
 *       operation ownership remains in the context so a later close can retry.
 *
 * @param adapter owns the resources to release.
 * @return First non-terminal cleanup error, or ESP_OK.
 */
esp_err_t setup_wifi_adapter_close(setup_wifi_adapter_t *adapter);

/**
 * @brief Report whether the adapter owns its manager subscription.
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
