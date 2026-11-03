#include "settings_wifi_settle.h"

settings_wifi_settle_result_t settings_wifi_settle_result(
    bool pending, bool terminal_matches, bool terminal_ok,
    bool deadline_reached)
{
    if (!pending)
    {
        return SETTINGS_WIFI_SETTLE_NONE;
    }
    if (terminal_matches)
    {
        return terminal_ok ? SETTINGS_WIFI_SETTLE_APPLIED :
               SETTINGS_WIFI_SETTLE_FAILED;
    }
    if (deadline_reached)
    {
        return SETTINGS_WIFI_SETTLE_TIMEOUT;
    }
    return SETTINGS_WIFI_SETTLE_WAIT;
}

bool settings_wifi_idle_timeout_notice(bool radio_idle_timeout,
                                       bool wifi_enabled, bool pending)
{
    return radio_idle_timeout && !wifi_enabled && !pending;
}
