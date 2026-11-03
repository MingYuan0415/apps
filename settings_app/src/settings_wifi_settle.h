#ifndef __SETTINGS_WIFI_SETTLE_H__
#define __SETTINGS_WIFI_SETTLE_H__

#include <stdbool.h>

/** @brief Outcome of one asynchronous Wi-Fi switch request. */
typedef enum settings_wifi_settle_result
{
    SETTINGS_WIFI_SETTLE_NONE = 0, /**< No request is pending. */
    SETTINGS_WIFI_SETTLE_WAIT,     /**< Keep waiting for its terminal. */
    SETTINGS_WIFI_SETTLE_APPLIED,  /**< Terminal reported success. */
    SETTINGS_WIFI_SETTLE_FAILED,   /**< Terminal reported a failure. */
    SETTINGS_WIFI_SETTLE_TIMEOUT,  /**< The UI deadline expired. */
} settings_wifi_settle_result_t;

/**
 * @brief Decide how the pending switch request settles this refresh.
 *
 * The terminal is authoritative: a successful terminal means the switch was
 * applied even when the manager ends at IDLE (no saved network) rather than
 * IP_READY. The UI deadline only covers a terminal that never arrives.
 *
 * @param pending is true while a request is waiting for its terminal.
 * @param terminal_matches is true when the snapshot carries that request's
 *        terminal.
 * @param terminal_ok is true when the terminal result is ESP_OK.
 * @param deadline_reached is true once the UI deadline has elapsed.
 */
settings_wifi_settle_result_t settings_wifi_settle_result(
    bool pending, bool terminal_matches, bool terminal_ok,
    bool deadline_reached);

/**
 * @brief Whether the page should explain an automatic radio power-off.
 *
 * @param radio_idle_timeout is the manager's no-profile timeout flag.
 * @param wifi_enabled is the current policy bit.
 * @param pending is true while a switch request is in flight.
 */
bool settings_wifi_idle_timeout_notice(bool radio_idle_timeout,
                                       bool wifi_enabled, bool pending);

#endif /* __SETTINGS_WIFI_SETTLE_H__ */
