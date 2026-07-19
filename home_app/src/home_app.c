#define DBG_TAG "home_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "event_bus.h"
#include "power_service.h"
#include "time_service.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_label;
    lv_obj_t *battery_value;
    lv_timer_t *clock_timer;
    event_bus_sub_handle_t power_subscription;
} home_page_state_t;

static const char *_home_time_quality_text(time_service_quality_t quality)
{
    const char *text = "Time unavailable";
    switch (quality)
    {
    case TIME_SERVICE_QUALITY_RTC:
        text = "RTC time";
        break;
    case TIME_SERVICE_QUALITY_MANUAL:
        text = "Manual time";
        break;
    case TIME_SERVICE_QUALITY_NTP:
        text = "Network time";
        break;
    case TIME_SERVICE_QUALITY_INVALID:
    default:
        break;
    }
    return text;
}

static void _home_update_clock(home_page_state_t *state)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        lv_label_set_text(state->time_label, "--:--");
        lv_label_set_text(state->date_label, "Waiting for a valid clock");
        lv_label_set_text(state->quality_label, "Time unavailable");
        goto exit;
    }

    char buffer[48];
    if (strftime(buffer, sizeof(buffer), "%H:%M", &local_time) > 0)
    {
        lv_label_set_text(state->time_label, buffer);
    }
    if (strftime(buffer, sizeof(buffer), "%a, %b %d", &local_time) > 0)
    {
        lv_label_set_text(state->date_label, buffer);
    }
    lv_label_set_text(state->quality_label,
                      _home_time_quality_text(time_service_get_quality()));

exit:
    return;
}

static void _home_clock_timer(lv_timer_t *timer)
{
    home_page_state_t *state = lv_timer_get_user_data(timer);
    if (state->page.root != NULL)
    {
        _home_update_clock(state);
    }
}

static void _home_render_power(home_page_state_t *state,
                               const power_service_snapshot_t *snapshot)
{
    if (!snapshot->valid)
    {
        lv_label_set_text(state->battery_value, "Unavailable");
        goto exit;
    }

    char text[48];
    if (snapshot->info.battery_percent >= 0)
    {
        snprintf(text, sizeof(text), "%d%% | %s",
                 snapshot->info.battery_percent,
                 snapshot->info.is_charging ? "Charging" : "On battery");
    }
    else
    {
        snprintf(text, sizeof(text), "%u mV | %s",
                 (unsigned)snapshot->info.battery_voltage_mv,
                 snapshot->info.is_vbus_connected ? "USB connected" : "Battery");
    }
    lv_label_set_text(state->battery_value, text);

exit:
    return;
}

static void _home_power_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                              const void *payload, size_t payload_size,
                              void *user_data)
{
    home_page_state_t *state = user_data;
    if (msg_id != POWER_SERVICE_MSG ||
            sub_type != POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE ||
            payload_size != sizeof(power_service_snapshot_t) ||
            state->page.root == NULL)
    {
        goto exit;
    }

    power_service_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    _home_render_power(state, &snapshot);

exit:
    return;
}

static void _home_open_app(lv_event_t *event)
{
    const char *app_id = lv_event_get_user_data(event);
    app_ui_request_run(app_id);
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create(&state->page, "MicroTech", false);

    lv_obj_t *clock = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(clock);
    lv_obj_set_width(clock, LV_PCT(100));
    lv_obj_set_height(clock, 138);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(clock, LV_OBJ_FLAG_SCROLLABLE);

    state->time_label = lv_label_create(clock);
    lv_label_set_text(state->time_label, "--:--");
    lv_obj_set_style_text_color(state->time_label, lv_color_hex(0xF4F7F8), 0);
    lv_obj_set_style_text_font(state->time_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);

    state->date_label = lv_label_create(clock);
    lv_label_set_text(state->date_label, "Waiting for a valid clock");
    lv_obj_set_style_text_color(state->date_label, lv_color_hex(0xAAB5BA), 0);
    lv_obj_set_style_text_font(state->date_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    state->quality_label = lv_label_create(clock);
    lv_label_set_text(state->quality_label, "Time unavailable");
    lv_obj_set_style_text_color(state->quality_label, lv_color_hex(0x39C6C8), 0);
    lv_obj_set_style_text_font(state->quality_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);

    app_ui_add_section(state->page.content, "DEVICE");
    app_ui_add_value_row(state->page.content, "Power", "Reading...",
                         &state->battery_value);

    app_ui_add_section(state->page.content, "QUICK ACCESS");
    app_ui_add_action(state->page.content, LV_SYMBOL_LIST, "Applications",
                      "Open the app menu", _home_open_app,
                      (void *)APP_MANAGER_ID_MENU);
    app_ui_add_action(state->page.content, LV_SYMBOL_SETTINGS, "Settings",
                      "Display, power and device info", _home_open_app,
                      (void *)APP_MANAGER_ID_SETTINGS);

    _home_update_clock(state);
    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) == ESP_OK)
    {
        _home_render_power(state, &snapshot);
    }
}

static void _home_page_resume(home_page_state_t *state)
{
    _home_update_clock(state);
    if (state->clock_timer == NULL)
    {
        state->clock_timer = lv_timer_create(_home_clock_timer, 1000, state);
        if (state->clock_timer == NULL)
        {
            LOG_W("clock timer unavailable");
        }
    }

    if (state->power_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_subscribe(
                               POWER_SERVICE_MSG,
                               POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE,
                               _home_power_event, state, EVENT_BUS_DISPATCH_UI,
                               &state->power_subscription);
        if (result != ESP_OK)
        {
            state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            LOG_W("power subscription failed: %s", esp_err_to_name(result));
        }
    }

    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) == ESP_OK)
    {
        _home_render_power(state, &snapshot);
    }
}

static esp_err_t _home_page_pause(home_page_state_t *state)
{
    esp_err_t result = ESP_OK;

    if (state->power_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        result = event_bus_unsubscribe(state->power_subscription);
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            result = ESP_OK;
        }
        else
        {
            LOG_W("power unsubscribe failed: %s", esp_err_to_name(result));
        }
    }
    if (state->clock_timer != NULL)
    {
        lv_timer_delete(state->clock_timer);
        state->clock_timer = NULL;
    }
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
    }
    return result;
}

static void _home_page_unmount(home_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_label = NULL;
    state->battery_value = NULL;
}

static void _home_page_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    home_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _home_page_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _home_page_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_home_page_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _home_page_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        if (_home_page_pause(state) == ESP_OK)
        {
            LOG_I("stopped");
        }
        break;
    default:
        break;
    }
}

APP_MANAGER_APP_EXPORT(home, NULL, APP_MANAGER_ID_HOME, "root",
                       APP_MANAGER_APP_FLAG_PINNED);
APP_MANAGER_PAGE_EXPORT(home_root, APP_MANAGER_ID_HOME, "root",
                        _home_page_handler, NULL, sizeof(home_page_state_t));
