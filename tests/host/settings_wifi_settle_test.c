#include "settings_wifi_settle.h"

#include <assert.h>
#include <stdio.h>

static void test_settle_decision(void)
{
    assert(settings_wifi_settle_result(false, false, false, false) ==
           SETTINGS_WIFI_SETTLE_NONE);
    assert(settings_wifi_settle_result(false, true, true, true) ==
           SETTINGS_WIFI_SETTLE_NONE);
    assert(settings_wifi_settle_result(true, false, false, false) ==
           SETTINGS_WIFI_SETTLE_WAIT);
    assert(settings_wifi_settle_result(true, false, true, false) ==
           SETTINGS_WIFI_SETTLE_WAIT);
    assert(settings_wifi_settle_result(true, true, true, false) ==
           SETTINGS_WIFI_SETTLE_APPLIED);
    /* A successful terminal wins over the deadline: the manager may publish
     * the terminal just after the UI deadline expired. */
    assert(settings_wifi_settle_result(true, true, true, true) ==
           SETTINGS_WIFI_SETTLE_APPLIED);
    assert(settings_wifi_settle_result(true, true, false, false) ==
           SETTINGS_WIFI_SETTLE_FAILED);
    assert(settings_wifi_settle_result(true, false, false, true) ==
           SETTINGS_WIFI_SETTLE_TIMEOUT);
}

static void test_idle_timeout_notice(void)
{
    assert(settings_wifi_idle_timeout_notice(true, false, false));
    assert(!settings_wifi_idle_timeout_notice(false, false, false));
    assert(!settings_wifi_idle_timeout_notice(true, true, false));
    assert(!settings_wifi_idle_timeout_notice(true, false, true));
}

int main(void)
{
    test_settle_decision();
    test_idle_timeout_notice();
    puts("Wi-Fi settle decision and timeout notice tests passed");
    return 0;
}
