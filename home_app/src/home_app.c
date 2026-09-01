#define DBG_TAG "home_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_weather_ui.h"
#include "connectivity_manager.h"
#include "device_link_service.h"
#include "event_bus.h"
#include "power_service.h"
#include "time_service.h"
#include "timer_service.h"
#include "weather_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *wifi_status;
    lv_obj_t *bluetooth_status;
    lv_obj_t *battery_status;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_label;
    lv_obj_t *weather_panel;
    lv_obj_t *weather_image;
    lv_obj_t *weather_city;
    lv_obj_t *weather_value;
    lv_obj_t *weather_condition;
    lv_obj_t *timer_panel;
    lv_obj_t *timer_value;
    lv_obj_t *timer_detail;
    lv_obj_t *shortcut_bar;
    lv_obj_t *shortcut_buttons[5];
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t power_subscription;
    event_bus_sub_handle_t wifi_subscription;
    event_bus_sub_handle_t weather_subscription;
    bool wifi_initialization_elapsed;
} home_page_state_t;

_Static_assert(sizeof(home_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Home page state exceeds the lifecycle arena slot");

static lv_obj_t *_home_label(lv_obj_t *parent, const char *text,
                             app_theme_font_id_t font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static const char *_home_time_quality_text(time_service_quality_t quality)
{
    switch (quality)
    {
    case TIME_SERVICE_QUALITY_RTC: return "RTC 时间";
    case TIME_SERVICE_QUALITY_MANUAL: return "手动时间";
    case TIME_SERVICE_QUALITY_NTP: return "网络时间";
    default: return "时间未校准";
    }
}

static void _home_render_clock(home_page_state_t *state)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        lv_label_set_text(state->time_label, "--:--");
        lv_label_set_text(state->date_label, "等待有效时间");
        app_ui_set_status_text(state->quality_label, "时间不可用",
                               APP_UI_STATUS_WARNING);
        return;
    }
    char text[48];
    (void)strftime(text, sizeof(text), "%H:%M", &local_time);
    lv_label_set_text(state->time_label, text);
    static const char *const weekdays[] = { "日", "一", "二", "三", "四", "五", "六" };
    const int weekday = local_time.tm_wday >= 0 && local_time.tm_wday < 7 ?
                        local_time.tm_wday : 0;
    (void)snprintf(text, sizeof(text), "%d月%d日  周%s",
                   local_time.tm_mon + 1, local_time.tm_mday,
                   weekdays[weekday]);
    lv_label_set_text(state->date_label, text);
    const time_service_quality_t quality = time_service_get_quality();
    app_ui_set_status_text(state->quality_label,
                           _home_time_quality_text(quality),
                           quality == TIME_SERVICE_QUALITY_INVALID ?
                           APP_UI_STATUS_WARNING : APP_UI_STATUS_ACCENT);
}

static void _home_render_power(home_page_state_t *state)
{
    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        app_ui_set_status_text(state->battery_status, "电量不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    char text[24];
    if (snapshot.info.battery_percent >= 0)
    {
        (void)snprintf(text, sizeof(text), "%d%%",
                       snapshot.info.battery_percent);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%umV",
                       (unsigned)snapshot.info.battery_voltage_mv);
    }
    app_ui_set_status_text(state->battery_status, text,
                           snapshot.info.is_charging ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_NEUTRAL);
}

static void _home_render_wifi(home_page_state_t *state)
{
    connectivity_manager_status_snapshot_t snapshot;
    if (connectivity_manager_get_status(&snapshot) != ESP_OK)
    {
        app_ui_set_status_text(state->wifi_status, "Wi-Fi 不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    const char *text = "未连接";
    app_ui_status_t status = APP_UI_STATUS_NEUTRAL;
    if (!snapshot.available)
    {
        text = state->wifi_initialization_elapsed ? "不可用" : "初始化中";
        status = state->wifi_initialization_elapsed ? APP_UI_STATUS_ERROR :
                 APP_UI_STATUS_ACCENT;
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        text = "已连接";
        status = APP_UI_STATUS_SUCCESS;
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_CONNECTING ||
             snapshot.state == CONNECTIVITY_MANAGER_STATE_WAITING_IP)
    {
        text = "连接中";
        status = APP_UI_STATUS_ACCENT;
    }
    app_ui_set_status_text(state->wifi_status, text, status);
}

static void _home_render_bluetooth(home_page_state_t *state)
{
    device_link_service_status_t status;
    if (device_link_service_get_status(&status) != ESP_OK || !status.available)
    {
        app_ui_set_status_text(state->bluetooth_status, "不可用",
                               APP_UI_STATUS_ERROR);
    }
    else if (status.pending_confirmation || status.active)
    {
        app_ui_set_status_text(state->bluetooth_status, "绑定中",
                               APP_UI_STATUS_ACCENT);
    }
    else
    {
        app_ui_set_status_text(state->bluetooth_status,
                               status.bound ? "已绑定" : "待连接",
                               status.bound ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_NEUTRAL);
    }
}

static void _home_render_weather(home_page_state_t *state)
{
    const weather_service_snapshot_t *snapshot = NULL;
    if (weather_service_snapshot_acquire(&snapshot) != ESP_OK || snapshot == NULL)
    {
        lv_label_set_text(state->weather_city, "天气");
        lv_label_set_text(state->weather_value, "暂不可用");
        lv_label_set_text(state->weather_condition, "请在设置中完成网络配置");
        return;
    }
    lv_label_set_text(state->weather_city,
                      snapshot->location.city[0] != '\0' ?
                      snapshot->location.city : "天气");
    if ((snapshot->available_mask & WEATHER_SERVICE_DATA_CURRENT) != 0U)
    {
        char temp[24];
        (void)snprintf(temp, sizeof(temp), "%.1f°",
                       snapshot->current.temperature_tenths_c / 10.0f);
        lv_label_set_text(state->weather_value, temp);
        lv_label_set_text(state->weather_condition,
                          snapshot->current.condition_text[0] != '\0' ?
                          snapshot->current.condition_text : "天气已更新");
        (void)app_weather_ui_set_image(state->weather_image,
                                       snapshot->current.condition_code, true);
    }
    else
    {
        lv_label_set_text(state->weather_value, "--");
        lv_label_set_text(state->weather_condition, "等待天气数据");
    }
    weather_service_snapshot_release(snapshot);
}

static void _home_render_timer(home_page_state_t *state)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        lv_label_set_text(state->timer_value, "计时不可用");
        lv_label_set_text(state->timer_detail, "打开时钟重试");
        return;
    }
    uint32_t remaining = 0U;
    const char *detail = "无活动计时";
    if (snapshot.countdown_state == TIMER_SERVICE_RUNNING ||
            snapshot.countdown_state == TIMER_SERVICE_PAUSED)
    {
        remaining = snapshot.countdown_remaining_ms;
        detail = snapshot.countdown_state == TIMER_SERVICE_PAUSED ?
                 "倒计时已暂停" : "倒计时";
    }
    else if (snapshot.focus_state == TIMER_SERVICE_RUNNING ||
             snapshot.focus_state == TIMER_SERVICE_PAUSED)
    {
        remaining = snapshot.focus_remaining_ms;
        detail = snapshot.focus_phase == TIMER_SERVICE_FOCUS_BREAK ?
                 "休息阶段" : "专注阶段";
    }
    if (remaining == 0U)
    {
        lv_label_set_text(state->timer_value, "无活动计时");
    }
    else
    {
        char text[16];
        (void)snprintf(text, sizeof(text), "%02u:%02u",
                       (unsigned)(remaining / 60000U),
                       (unsigned)((remaining / 1000U) % 60U));
        lv_label_set_text(state->timer_value, text);
    }
    lv_label_set_text(state->timer_detail, detail);
}

static void _home_refresh(home_page_state_t *state)
{
    if (state->page.root == NULL)
    {
        return;
    }
    state->wifi_initialization_elapsed = true;
    _home_render_clock(state);
    _home_render_power(state);
    _home_render_wifi(state);
    _home_render_bluetooth(state);
    _home_render_weather(state);
    _home_render_timer(state);
}

static void _home_refresh_timer(lv_timer_t *timer)
{
    _home_refresh(lv_timer_get_user_data(timer));
}

static void _home_service_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                const void *payload, size_t payload_size,
                                void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    home_page_state_t *state = user_data;
    if (state != NULL)
    {
        _home_refresh(state);
    }
}

static void _home_open_app(lv_event_t *event)
{
    const char *app_id = lv_event_get_user_data(event);
    if (app_id != NULL)
    {
        LOG_D("shortcut click: %s", app_id);
        app_ui_request_run(app_id);
    }
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create_home(&state->page);
    lv_obj_t *content = state->page.content;
    lv_obj_t *status = lv_obj_create(content);
    lv_obj_remove_style_all(status);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, 28);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(status, 8, 0);
    app_ui_make_passive(status, false);
    state->wifi_status = _home_label(status, LV_SYMBOL_WIFI " 未连接",
                                     APP_THEME_FONT_SMALL, 0x93A0A6);
    state->bluetooth_status = _home_label(status,
                                          LV_SYMBOL_BLUETOOTH " 待连接",
                                          APP_THEME_FONT_SMALL, 0x93A0A6);
    state->battery_status = _home_label(status,
                                         LV_SYMBOL_BATTERY_FULL " --",
                                         APP_THEME_FONT_SMALL, 0x93A0A6);
    lv_obj_set_flex_grow(state->wifi_status, 1);
    lv_obj_set_flex_grow(state->bluetooth_status, 1);
    lv_obj_set_flex_grow(state->battery_status, 1);
    lv_obj_set_width(state->wifi_status, 0);
    lv_obj_set_width(state->bluetooth_status, 0);
    lv_obj_set_width(state->battery_status, 0);

    lv_obj_t *clock = lv_obj_create(content);
    lv_obj_remove_style_all(clock);
    lv_obj_set_width(clock, LV_PCT(100));
    lv_obj_set_height(clock, 0);
    lv_obj_set_flex_grow(clock, 1);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(clock, false);
    state->time_label = _home_label(clock, "--:--", APP_THEME_FONT_TITLE, 0xF2F5F6);
    lv_obj_set_style_text_align(state->time_label, LV_TEXT_ALIGN_CENTER, 0);
    state->date_label = _home_label(clock, "等待有效时间", APP_THEME_FONT_BODY, 0xAAB5BA);
    lv_obj_set_style_text_align(state->date_label, LV_TEXT_ALIGN_CENTER, 0);
    state->quality_label = _home_label(clock, "时间未校准", APP_THEME_FONT_SMALL, 0xF5C451);
    lv_obj_set_style_text_align(state->quality_label, LV_TEXT_ALIGN_CENTER, 0);

    state->weather_panel = lv_button_create(content);
    lv_obj_set_width(state->weather_panel, LV_PCT(100));
    lv_obj_set_height(state->weather_panel, 66);
    lv_obj_set_style_radius(state->weather_panel, 8, 0);
    lv_obj_set_style_bg_color(state->weather_panel, lv_color_hex(0x151B1F), 0);
    lv_obj_set_style_pad_all(state->weather_panel, 8, 0);
    lv_obj_add_event_cb(state->weather_panel, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_WEATHER);
    lv_obj_t *weather_row = lv_obj_create(state->weather_panel);
    lv_obj_remove_style_all(weather_row);
    lv_obj_set_size(weather_row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);
    app_ui_make_passive(weather_row, false);
    state->weather_image = lv_image_create(weather_row);
    lv_obj_set_size(state->weather_image, 40, 40);
    app_ui_make_passive(state->weather_image, false);
    state->weather_city = _home_label(weather_row, "天气", APP_THEME_FONT_SMALL, 0x93A0A6);
    state->weather_value = _home_label(weather_row, "--", APP_THEME_FONT_BODY, 0xF2F5F6);
    state->weather_condition = _home_label(weather_row, "等待天气数据", APP_THEME_FONT_SMALL, 0x93A0A6);
    lv_label_set_long_mode(state->weather_city, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(state->weather_city, 1);
    lv_obj_set_flex_grow(state->weather_value, 1);
    lv_obj_set_flex_grow(state->weather_condition, 2);
    lv_obj_set_width(state->weather_city, 0);
    lv_obj_set_width(state->weather_value, 0);
    lv_obj_set_width(state->weather_condition, 0);

    state->timer_panel = lv_button_create(content);
    lv_obj_set_width(state->timer_panel, LV_PCT(100));
    lv_obj_set_height(state->timer_panel, 52);
    lv_obj_set_style_radius(state->timer_panel, 8, 0);
    lv_obj_set_style_bg_color(state->timer_panel, lv_color_hex(0x151B1F), 0);
    lv_obj_add_event_cb(state->timer_panel, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_CLOCK);
    state->timer_value = _home_label(state->timer_panel, "无活动计时",
                                     APP_THEME_FONT_BODY, 0xF2F5F6);
    state->timer_detail = _home_label(state->timer_panel, "无活动计时",
                                      APP_THEME_FONT_SMALL, 0x93A0A6);
    lv_obj_set_flex_grow(state->timer_value, 1);
    lv_obj_set_flex_grow(state->timer_detail, 1);
    lv_obj_set_width(state->timer_value, 0);
    lv_obj_set_width(state->timer_detail, 0);

    state->shortcut_bar = lv_obj_create(content);
    lv_obj_remove_style_all(state->shortcut_bar);
    lv_obj_set_width(state->shortcut_bar, LV_PCT(100));
    lv_obj_set_height(state->shortcut_bar, 60);
    lv_obj_set_flex_flow(state->shortcut_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(state->shortcut_bar, 4, 0);
    app_ui_make_passive(state->shortcut_bar, false);
    const uint32_t ids[] = { APP_IMAGE_WEATHER_APP, APP_IMAGE_CLOCK_ICON,
                             APP_IMAGE_RECORDER_ICON, APP_IMAGE_SETTINGS_ICON,
                             APP_IMAGE_MENU_ICON };
    const char *symbols[] = { LV_SYMBOL_GPS, LV_SYMBOL_BELL, LV_SYMBOL_AUDIO,
                              LV_SYMBOL_SETTINGS, LV_SYMBOL_LIST };
    const char *apps[] = { APP_MANAGER_ID_WEATHER, APP_MANAGER_ID_CLOCK,
                           APP_MANAGER_ID_RECORDER, APP_MANAGER_ID_SETTINGS,
                           APP_MANAGER_ID_MENU };
    for (size_t index = 0; index < 5U; index++)
    {
        state->shortcut_buttons[index] = app_ui_add_icon_button(
                                              state->shortcut_bar, ids[index], symbols[index],
                                              _home_open_app, (void *)apps[index]);
    }
    _home_refresh(state);
}

static void _home_page_resume(const app_manager_page_context_t *context)
{
    home_page_state_t *state = context->state;
    _home_refresh(state);
    if (state->refresh_timer == NULL)
    {
        state->refresh_timer = lv_timer_create(_home_refresh_timer, 1000, state);
    }
    if (state->power_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_subscribe(POWER_SERVICE_MSG,
                                  POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE,
                                  _home_service_event, state,
                                  EVENT_BUS_DISPATCH_UI,
                                  &state->power_subscription);
    }
    if (state->wifi_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_subscribe(CONNECTIVITY_MANAGER_MSG,
                                  CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                                  _home_service_event, state,
                                  EVENT_BUS_DISPATCH_UI,
                                  &state->wifi_subscription);
    }
    if (state->weather_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_subscribe(WEATHER_SERVICE_MSG,
                                  WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                                  _home_service_event, state,
                                  EVENT_BUS_DISPATCH_UI,
                                  &state->weather_subscription);
    }
}

static esp_err_t _home_page_pause(const app_manager_page_context_t *context)
{
    home_page_state_t *state = context->state;
    esp_err_t first = ESP_OK;
    event_bus_sub_handle_t *subscriptions[] =
    {
        &state->weather_subscription,
        &state->wifi_subscription,
        &state->power_subscription,
    };
    for (size_t index = 0; index < 3U; index++)
    {
        if (*subscriptions[index] != EVENT_BUS_SUB_HANDLE_INVALID)
        {
            esp_err_t result = event_bus_unsubscribe(*subscriptions[index]);
            if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
            {
                *subscriptions[index] = EVENT_BUS_SUB_HANDLE_INVALID;
            }
            else if (first == ESP_OK)
            {
                first = result;
            }
        }
    }
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    return first;
}

static void _home_mount(const app_manager_page_context_t *context)
{
    _home_page_build(context->state);
}

static void _home_page_unmount(const app_manager_page_context_t *context)
{
    home_page_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->wifi_status = NULL;
    state->bluetooth_status = NULL;
    state->battery_status = NULL;
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_label = NULL;
    state->weather_panel = NULL;
    state->weather_image = NULL;
    state->weather_city = NULL;
    state->weather_value = NULL;
    state->weather_condition = NULL;
    state->timer_panel = NULL;
    state->timer_value = NULL;
    state->timer_detail = NULL;
    state->shortcut_bar = NULL;
    memset(state->shortcut_buttons, 0, sizeof(state->shortcut_buttons));
}

static const app_manager_page_ops_t s_home_ops =
{
    .mount = _home_mount,
    .resume = _home_page_resume,
    .pause = _home_page_pause,
    .unmount = _home_page_unmount,
};

static const app_manager_page_definition_t s_home_root_definition =
{
    .ops = &s_home_ops,
    .memory_size = sizeof(home_page_state_t),
};

static const app_manager_page_route_t s_home_routes[] =
{
    { .page_id = APP_MANAGER_ID_HOME_ROOT, .definition = &s_home_root_definition },
};

APP_MANAGER_APP_EXPORT_META(home, APP_IMAGE_HOME_ICON, "主页", APP_MANAGER_ID_HOME,
                            "root", APP_MANAGER_APP_FLAG_PINNED, s_home_routes,
                            1U, "今日概览");
