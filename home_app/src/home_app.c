#define DBG_TAG "home_app"
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "app_weather_ui.h"
#include "connectivity_manager.h"
#include "device_link_service.h"
#include "event_bus.h"
#include "home_shortcuts.h"
#include "home_shortcuts_page.h"
#include "power_service.h"
#include "time_service.h"
#include "timer_service.h"
#include "weather_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOME_STATUS_HEIGHT       28
#define HOME_BAND_HEIGHT         56
#define HOME_TIMER_RING          36
#define HOME_SHORTCUT_WELL       64
#define HOME_SHORTCUT_IMAGE      56

typedef struct home_page_state home_page_state_t;

typedef struct home_shortcut_view_ctx
{
    home_page_state_t *state;
    const char *app_id;
} home_shortcut_view_ctx_t;

struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *wifi_arcs[3];
    lv_obj_t *wifi_dot;
    lv_obj_t *wifi_status;
    lv_obj_t *bluetooth_icon;
    lv_obj_t *bluetooth_status;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_status;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_pill;
    lv_obj_t *weather_band;
    lv_obj_t *weather_image;
    lv_obj_t *weather_fallback;
    lv_obj_t *weather_city;
    lv_obj_t *weather_value;
    lv_obj_t *weather_condition;
    lv_obj_t *timer_band;
    lv_obj_t *clock_ring;
    lv_obj_t *clock_title;
    lv_obj_t *clock_value;
    lv_obj_t *clock_caption;
    lv_obj_t *shortcut_dock;
    home_shortcut_view_ctx_t shortcut_ctx[HOME_SHORTCUT_SLOTS];
    home_shortcut_view_ctx_t app_ctx;
    uint32_t shortcut_generation;
    bool shortcut_long_pressed;
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t power_subscription;
    event_bus_sub_handle_t wifi_subscription;
    event_bus_sub_handle_t weather_subscription;
};

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

static lv_obj_t *_home_inline_label(lv_obj_t *parent, const char *text,
                                    app_theme_font_id_t font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static lv_obj_t *_home_symbol_label(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, 20);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    app_ui_make_passive(label, false);
    lv_label_set_text(label, symbol != NULL ? symbol : "");
    return label;
}

static void _home_set_color(lv_obj_t *label, uint32_t color)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

static void _home_set_weather_image(home_page_state_t *state,
                                    uint16_t condition_code, bool available)
{
    const bool ready = available && app_weather_ui_set_image(
                           state->weather_image, condition_code, true);
    if (ready)
    {
        lv_obj_set_hidden(state->weather_fallback, true);
    }
    else
    {
        lv_obj_set_hidden(state->weather_image, true);
        lv_obj_set_hidden(state->weather_fallback, false);
    }
}

/* A passive ring-sized arc. Its MAIN part draws the background track and its
 * INDICATOR part is reserved for a progress sweep. */
static lv_obj_t *_home_ring_arc(lv_obj_t *parent, int32_t size, int32_t width,
                                uint32_t track_color, bool hide_indicator)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(track_color), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, hide_indicator ? LV_OPA_TRANSP : LV_OPA_COVER,
                             LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    app_ui_make_passive(arc, false);
    return arc;
}

static const char *_home_time_quality_text(time_service_quality_t quality)
{
    switch (quality)
    {
    case TIME_SERVICE_QUALITY_RTC:
        return "RTC 时间";
    case TIME_SERVICE_QUALITY_MANUAL:
        return "手动时间";
    case TIME_SERVICE_QUALITY_NTP:
        return "网络时间";
    default:
        return "时间未校准";
    }
}

static void _home_render_clock(home_page_state_t *state)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        app_ui_label_set_text_if(state->time_label, "--:--");
        app_ui_label_set_text_if(state->date_label, "等待有效时间");
        app_ui_pill_set(state->quality_pill, "时间不可用",
                        APP_UI_STATUS_WARNING);
        return;
    }
    char text[48];
    if (strftime(text, sizeof(text), "%H:%M", &local_time) == 0U)
    {
        (void)snprintf(text, sizeof(text), "--:--");
    }
    app_ui_label_set_text_if(state->time_label, text);
    static const char *const weekdays[] = { "日", "一", "二", "三", "四", "五", "六" };
    const int weekday = local_time.tm_wday >= 0 && local_time.tm_wday < 7 ?
                        local_time.tm_wday : 0;
    (void)snprintf(text, sizeof(text), "%d月%d日  周%s",
                   local_time.tm_mon + 1, local_time.tm_mday,
                   weekdays[weekday]);
    app_ui_label_set_text_if(state->date_label, text);
    const time_service_quality_t quality = time_service_get_quality();
    const bool valid = quality != TIME_SERVICE_QUALITY_INVALID;
    app_ui_pill_set(state->quality_pill, _home_time_quality_text(quality),
                    quality == TIME_SERVICE_QUALITY_NTP ?
                    APP_UI_STATUS_SUCCESS : valid ? APP_UI_STATUS_ACCENT :
                    APP_UI_STATUS_WARNING);
}

static void _home_render_power(home_page_state_t *state)
{
    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        app_ui_label_set_text_if(state->battery_status, "--");
        lv_obj_set_width(state->battery_fill, 0);
        lv_obj_set_style_bg_color(state->battery_fill,
                                  lv_color_hex(APP_UI_COLOR_WARNING), 0);
        _home_set_color(state->battery_status, APP_UI_COLOR_WARNING);
        return;
    }
    char text[24];
    int percent = snapshot.info.battery_percent;
    if (percent >= 0)
    {
        (void)snprintf(text, sizeof(text), "%d%%", percent);
    }
    else
    {
        /* Voltage-only telemetry maps the 3.30-4.20 V window onto the fill. */
        const int mv = snapshot.info.battery_voltage_mv;
        percent = (mv - 3300) * 100 / 900;
        if (percent < 0)
        {
            percent = 0;
        }
        else if (percent > 100)
        {
            percent = 100;
        }
        (void)snprintf(text, sizeof(text), "%.1fV", (double)mv / 1000.0);
    }
    app_ui_label_set_text_if(state->battery_status, text);
    const uint32_t fill_color = snapshot.info.is_charging ? APP_UI_COLOR_RAIN :
                                (percent <= 15 ? APP_UI_COLOR_WARNING :
                                 APP_UI_COLOR_TEXT);
    lv_obj_set_style_bg_color(state->battery_fill, lv_color_hex(fill_color), 0);
    lv_obj_set_width(state->battery_fill, percent * 24 / 100);
    _home_set_color(state->battery_status,
                    snapshot.info.is_charging ? APP_UI_COLOR_RAIN :
                    APP_UI_COLOR_MUTED);
}

/* The connectivity manager takes a moment to initialize after boot; a
 * not-yet-available manager renders as waiting, and only a stuck one as a
 * warning. Counted across mounts at the 1 Hz refresh cadence. */
#define HOME_WIFI_GRACE_TICKS 30U
static uint32_t s_wifi_grace_ticks;

static void _home_render_wifi(home_page_state_t *state)
{
    const char *text = "未连接";
    uint32_t color = APP_UI_COLOR_MUTED;
    connectivity_manager_status_snapshot_t snapshot;
    if (connectivity_manager_get_status(&snapshot) != ESP_OK)
    {
        text = "不可用";
        color = APP_UI_COLOR_WARNING;
    }
    else if (!snapshot.available)
    {
        if (s_wifi_grace_ticks < HOME_WIFI_GRACE_TICKS)
        {
            s_wifi_grace_ticks++;
            text = "等待";
            color = APP_UI_COLOR_SUN;
        }
        else
        {
            text = "不可用";
            color = APP_UI_COLOR_WARNING;
        }
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        text = "已连接";
        color = APP_UI_COLOR_SUCCESS;
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_CONNECTING ||
             snapshot.state == CONNECTIVITY_MANAGER_STATE_WAITING_IP)
    {
        text = "连接中";
        color = APP_UI_COLOR_SUN;
    }
    for (size_t index = 0; index < 3U; index++)
    {
        lv_obj_set_style_arc_color(state->wifi_arcs[index], lv_color_hex(color),
                                   LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(state->wifi_dot, lv_color_hex(color), 0);
    if (state->wifi_status != NULL)
    {
        app_ui_label_set_text_if(state->wifi_status, text);
        _home_set_color(state->wifi_status, color);
    }
}

static void _home_render_bluetooth(home_page_state_t *state)
{
    const char *text = "不可用";
    uint32_t color = APP_UI_COLOR_WARNING;
    device_link_service_status_t status;
    if (device_link_service_get_status(&status) == ESP_OK && status.available)
    {
        if (!status.enabled)
        {
            text = "已关闭";
            color = APP_UI_COLOR_MUTED;
        }
        else if (status.transitioning)
        {
            text = "切换中";
            color = APP_UI_COLOR_SUN;
        }
        else if (status.pending_confirmation)
        {
            text = "配对确认";
            color = APP_UI_COLOR_SUN;
        }
        else if (status.active)
        {
            text = "配对中";
            color = APP_UI_COLOR_SUN;
        }
        else if (status.bound)
        {
            text = "已绑定";
            color = APP_UI_COLOR_SUCCESS;
        }
        else
        {
            text = "未绑定";
            color = APP_UI_COLOR_MUTED;
        }
    }
    if (state->bluetooth_icon != NULL)
    {
        _home_set_color(state->bluetooth_icon, color);
    }
    if (state->bluetooth_status != NULL)
    {
        app_ui_label_set_text_if(state->bluetooth_status, text);
        _home_set_color(state->bluetooth_status, color);
    }
}

static void _home_render_weather(home_page_state_t *state)
{
    if (state->weather_band == NULL)
    {
        return;
    }
    const weather_service_snapshot_t *snapshot = NULL;
    if (weather_service_snapshot_acquire(&snapshot) != ESP_OK || snapshot == NULL)
    {
        _home_set_weather_image(state, 0U, true);
        app_ui_label_set_text_if(state->weather_city, "天气");
        app_ui_label_set_text_if(state->weather_value, "--");
        app_ui_label_set_text_if(state->weather_condition, "不可用");
        _home_set_color(state->weather_value, APP_UI_COLOR_WARNING);
        return;
    }
    if (snapshot->location.city[0] != '\0')
    {
        char title[WEATHER_SERVICE_CITY_BYTES +
                   WEATHER_SERVICE_DISTRICT_BYTES + 4U];
        if (snapshot->location.district[0] != '\0')
        {
            (void)snprintf(title, sizeof(title), "%s·%s",
                           snapshot->location.city,
                           snapshot->location.district);
        }
        else
        {
            (void)snprintf(title, sizeof(title), "%s",
                           snapshot->location.city);
        }
        app_ui_label_set_text_if(state->weather_city, title);
    }
    else
    {
        app_ui_label_set_text_if(state->weather_city, "天气");
    }
    if ((snapshot->available_mask & WEATHER_SERVICE_DATA_CURRENT) != 0U)
    {
        char temp[24];
        (void)snprintf(temp, sizeof(temp), "%.1f°",
                       snapshot->current.temperature_tenths_c / 10.0f);
        app_ui_label_set_text_if(state->weather_value, temp);
        app_ui_label_set_text_if(state->weather_condition,
                                 snapshot->current.condition_text[0] != '\0' ?
                                 snapshot->current.condition_text : "天气已更新");
        _home_set_color(state->weather_value, APP_UI_COLOR_SUN);
        _home_set_weather_image(state, snapshot->current.condition_code, true);
    }
    else
    {
        app_ui_label_set_text_if(state->weather_value, "--");
        weather_service_status_snapshot_t status;
        const char *condition = weather_service_get_status(&status) == ESP_OK &&
                                !status.configured ? "未配置" : "等待天气";
        app_ui_label_set_text_if(state->weather_condition, condition);
        _home_set_color(state->weather_value, APP_UI_COLOR_MUTED);
        _home_set_weather_image(state, 0U, true);
    }
    weather_service_snapshot_release(snapshot);
}

static void _home_render_timer_band(home_page_state_t *state)
{
    if (state->timer_band == NULL)
    {
        return;
    }
    timer_service_snapshot_t snapshot;
    const char *title = "倒计时";
    const char *caption = "进行中";
    uint32_t accent = APP_UI_COLOR_RAIN;
    bool active = false;
    bool paused = false;
    uint32_t remaining_ms = 0U;
    uint32_t total_ms = 0U;
    if (timer_service_get_snapshot(&snapshot) == ESP_OK)
    {
        if (snapshot.countdown_state == TIMER_SERVICE_RUNNING ||
                snapshot.countdown_state == TIMER_SERVICE_PAUSED)
        {
            active = true;
            title = "倒计时";
            remaining_ms = snapshot.countdown_remaining_ms;
            total_ms = snapshot.countdown_duration_ms;
            paused = snapshot.countdown_state == TIMER_SERVICE_PAUSED;
        }
        else if (snapshot.focus_state == TIMER_SERVICE_RUNNING ||
                 snapshot.focus_state == TIMER_SERVICE_PAUSED)
        {
            active = true;
            title = "专注";
            remaining_ms = snapshot.focus_remaining_ms;
            accent = APP_UI_COLOR_SUN;
            paused = snapshot.focus_state == TIMER_SERVICE_PAUSED;
        }
    }
    if (!active)
    {
        lv_obj_set_hidden(state->timer_band, true);
        return;
    }
    lv_obj_set_hidden(state->timer_band, false);
    char text[16];
    (void)snprintf(text, sizeof(text), "%u:%02u",
                   (unsigned)(remaining_ms / 60000U),
                   (unsigned)((remaining_ms / 1000U) % 60U));
    if (paused)
    {
        accent = APP_UI_COLOR_MUTED;
        caption = "已暂停";
    }
    app_ui_label_set_text_if(state->clock_title, title);
    app_ui_label_set_text_if(state->clock_value, text);
    app_ui_label_set_text_if(state->clock_caption, caption);
    _home_set_color(state->clock_title, accent);
    _home_set_color(state->clock_value, APP_UI_COLOR_TEXT);
    _home_set_color(state->clock_caption, accent);
    lv_obj_set_style_arc_color(state->clock_ring, lv_color_hex(accent),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(state->clock_ring, LV_OPA_COVER,
                             LV_PART_INDICATOR);
    /* Focus snapshots expose no phase total, so a missing duration renders a
     * full-strength ring instead of a misleading sweep. */
    const uint32_t span = (total_ms > 0U && remaining_ms <= total_ms) ?
                          (uint32_t)(((uint64_t)remaining_ms * 360U) /
                                     total_ms) : 360U;
    lv_arc_set_angles(state->clock_ring, 0, (lv_value_precise_t)span);
}

static void _home_refresh(home_page_state_t *state)
{
    if (state->page.root == NULL)
    {
        return;
    }
    _home_render_clock(state);
    _home_render_power(state);
    _home_render_wifi(state);
    _home_render_bluetooth(state);
    _home_render_weather(state);
    _home_render_timer_band(state);
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
        LOG_D("home action click: %s", app_id);
        app_ui_request_run(app_id);
    }
}

static void _home_build_status(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *status = lv_obj_create(content);
    lv_obj_remove_style_all(status);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, HOME_STATUS_HEIGHT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 4, 0);
    app_ui_make_passive(status, false);

    lv_obj_t *wifi = lv_obj_create(status);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 28, 22);
    app_ui_make_passive(wifi, false);
    lv_obj_set_overflow_visible(wifi, true);
    static const int32_t wifi_arc_sizes[3] = { 28, 20, 12 };
    for (size_t index = 0; index < 3U; index++)
    {
        const int32_t size = wifi_arc_sizes[index];
        lv_obj_t *arc = _home_ring_arc(wifi, size, 2, APP_UI_COLOR_RAIN, true);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_obj_set_pos(arc, 14 - size / 2, 18 - size / 2);
        state->wifi_arcs[index] = arc;
    }
    state->wifi_dot = lv_obj_create(wifi);
    lv_obj_remove_style_all(state->wifi_dot);
    lv_obj_set_size(state->wifi_dot, 4, 4);
    lv_obj_set_style_radius(state->wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(state->wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(state->wifi_dot, lv_color_hex(APP_UI_COLOR_RAIN),
                              0);
    lv_obj_set_pos(state->wifi_dot, 12, 16);
    app_ui_make_passive(state->wifi_dot, false);
    state->wifi_status = _home_inline_label(status, "未连接",
                                            APP_THEME_FONT_SMALL,
                                            APP_UI_COLOR_MUTED);

    state->bluetooth_icon = _home_symbol_label(status, LV_SYMBOL_BLUETOOTH);
    state->bluetooth_status = _home_inline_label(status, "未绑定",
        APP_THEME_FONT_SMALL,
        APP_UI_COLOR_MUTED);

    lv_obj_t *status_spacer = lv_obj_create(status);
    lv_obj_remove_style_all(status_spacer);
    lv_obj_set_width(status_spacer, 0);
    lv_obj_set_height(status_spacer, 1);
    lv_obj_set_flex_grow(status_spacer, 1);
    app_ui_make_passive(status_spacer, false);

    lv_obj_t *battery = lv_obj_create(status);
    lv_obj_remove_style_all(battery);
    lv_obj_set_size(battery, 36, 14);
    lv_obj_set_style_radius(battery, 4, 0);
    lv_obj_set_style_border_width(battery, 2, 0);
    lv_obj_set_style_border_color(battery, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    app_ui_make_passive(battery, false);
    state->battery_fill = lv_obj_create(battery);
    lv_obj_set_size(state->battery_fill, 0, 6);
    lv_obj_set_pos(state->battery_fill, 3, 3);
    lv_obj_set_style_radius(state->battery_fill, 2, 0);
    lv_obj_set_style_bg_opa(state->battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(state->battery_fill,
                              lv_color_hex(APP_UI_COLOR_TEXT), 0);
    app_ui_make_passive(state->battery_fill, false);
    lv_obj_t *battery_nub = lv_obj_create(battery);
    lv_obj_set_size(battery_nub, 3, 6);
    lv_obj_set_pos(battery_nub, 37, 4);
    lv_obj_set_style_radius(battery_nub, 1, 0);
    lv_obj_set_style_bg_opa(battery_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(battery_nub, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    app_ui_make_passive(battery_nub, false);
    lv_obj_set_overflow_visible(battery, true);

    state->battery_status = _home_inline_label(status, "--",
        APP_THEME_FONT_SMALL,
        APP_UI_COLOR_MUTED);
    lv_point_t battery_size;
    lv_text_get_size(&battery_size, "100%",
                     app_ui_font(APP_THEME_FONT_SMALL), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(state->battery_status, battery_size.x + 4);
    lv_obj_set_style_text_align(state->battery_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(state->battery_status, LV_LABEL_LONG_CLIP);
}

static void _home_build_hero(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *hero = lv_obj_create(content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, 0);
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(hero, false);

    state->time_label = _home_label(hero, "--:--", APP_THEME_FONT_DISPLAY,
                                    APP_UI_COLOR_TEXT);
    lv_obj_set_style_text_align(state->time_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *meta = lv_obj_create(hero);
    lv_obj_remove_style_all(meta);
    lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta, 10, 0);
    app_ui_make_passive(meta, false);
    state->date_label = _home_label(meta, "等待有效时间", APP_THEME_FONT_BODY,
                                    APP_UI_COLOR_MUTED);
    lv_obj_set_width(state->date_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(state->date_label, LV_LABEL_LONG_CLIP);
    state->quality_pill = app_ui_add_pill(meta, "时间未校准",
                                          APP_UI_STATUS_WARNING);
    /* The hero reserves 138 px; the shared pill's 4 px inset overshoots by 2. */
    lv_obj_set_style_pad_top(state->quality_pill, 3, 0);
    lv_obj_set_style_pad_bottom(state->quality_pill, 3, 0);
}

static void _home_build_weather(home_page_state_t *state, lv_obj_t *content)
{
    if (!app_manager_builtin_has_app(APP_MANAGER_ID_WEATHER))
    {
        return;
    }
    lv_obj_t *band = lv_button_create(content);
    lv_obj_remove_style_all(band);
    app_ui_click_only(band);
    lv_obj_set_width(band, LV_PCT(100));
    lv_obj_set_height(band, HOME_BAND_HEIGHT);
    lv_obj_set_style_radius(band, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(band, 4, 0);
    lv_obj_set_style_pad_column(band, 4, 0);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(band, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_WEATHER);
    state->weather_band = band;

    /* Identity stays together on the left; readings stay together on the
     * right. The identity group absorbs spare width, so no single label owns
     * an arbitrary gap. */
    lv_obj_t *identity = lv_obj_create(band);
    lv_obj_remove_style_all(identity);
    lv_obj_set_width(identity, 0);
    lv_obj_set_height(identity, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(identity, 1);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(identity, 4, 0);
    app_ui_make_passive(identity, false);

    state->weather_image = lv_image_create(identity);
    lv_obj_set_size(state->weather_image, 40, 40);
    app_ui_make_passive(state->weather_image, false);
    state->weather_fallback = _home_symbol_label(identity, LV_SYMBOL_IMAGE);
    _home_set_color(state->weather_fallback, APP_UI_COLOR_MUTED);

    state->weather_city = _home_inline_label(identity, "",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_FAINT);
    lv_obj_set_width(state->weather_city, 0);
    lv_obj_set_flex_grow(state->weather_city, 1);
    lv_obj_set_height(state->weather_city,
                      lv_font_get_line_height(
                          app_ui_font(APP_THEME_FONT_SMALL)));
    lv_label_set_long_mode(state->weather_city, LV_LABEL_LONG_DOT);

    lv_obj_t *reading = lv_obj_create(band);
    lv_obj_remove_style_all(reading);
    lv_obj_set_size(reading, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(reading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reading, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(reading, 8, 0);
    app_ui_make_passive(reading, false);

    state->weather_condition = _home_inline_label(reading, "天气",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);
    lv_obj_set_width(state->weather_condition, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(state->weather_condition, 60, 0);
    lv_obj_set_height(state->weather_condition,
                      lv_font_get_line_height(
                          app_ui_font(APP_THEME_FONT_SMALL)));
    lv_label_set_long_mode(state->weather_condition, LV_LABEL_LONG_DOT);
    state->weather_value = _home_inline_label(reading, "--",
        APP_THEME_FONT_HEAD, APP_UI_COLOR_SUN);
}

static void _home_build_timer(home_page_state_t *state, lv_obj_t *content)
{
    if (!app_manager_builtin_has_app(APP_MANAGER_ID_CLOCK))
    {
        return;
    }
    lv_obj_t *band = lv_button_create(content);
    lv_obj_remove_style_all(band);
    app_ui_click_only(band);
    lv_obj_set_width(band, LV_PCT(100));
    lv_obj_set_height(band, HOME_BAND_HEIGHT);
    lv_obj_set_style_radius(band, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(band, 4, 0);
    lv_obj_set_style_pad_column(band, 8, 0);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(band, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_CLOCK);
    state->timer_band = band;

    state->clock_ring = _home_ring_arc(band, HOME_TIMER_RING, 4,
                                       APP_UI_COLOR_TRACK, true);
    lv_arc_set_rotation(state->clock_ring, 270);
    lv_obj_set_style_arc_rounded(state->clock_ring, true,
                                 LV_PART_INDICATOR);
    state->clock_title = _home_inline_label(band, "倒计时",
                                            APP_THEME_FONT_SMALL,
                                            APP_UI_COLOR_MUTED);
    state->clock_value = _home_inline_label(band, "00:00",
                                            APP_THEME_FONT_HEAD,
                                            APP_UI_COLOR_TEXT);
    state->clock_caption = _home_inline_label(band, "进行中",
        APP_THEME_FONT_SMALL,
        APP_UI_COLOR_RAIN);
    lv_obj_set_hidden(band, true);
}

static void _home_shortcut_pressed(lv_event_t *event)
{
    home_shortcut_view_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx != NULL && ctx->state != NULL)
    {
        ctx->state->shortcut_long_pressed = false;
    }
}

static void _home_shortcut_press_lost(lv_event_t *event)
{
    home_shortcut_view_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx != NULL && ctx->state != NULL)
    {
        ctx->state->shortcut_long_pressed = false;
    }
}

static void _home_shortcut_long_pressed(lv_event_t *event)
{
    home_shortcut_view_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx == NULL || ctx->state == NULL)
    {
        return;
    }
    ctx->state->shortcut_long_pressed = true;
    app_ui_request_open_page(APP_MANAGER_ID_HOME, HOME_SHORTCUTS_PAGE_ID);
}

static void _home_shortcut_clicked(lv_event_t *event)
{
    home_shortcut_view_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx == NULL || ctx->state == NULL)
    {
        return;
    }
    if (ctx->state->shortcut_long_pressed)
    {
        ctx->state->shortcut_long_pressed = false;
        return;
    }
    app_ui_request_run(ctx->app_id != NULL ? ctx->app_id :
                       APP_MANAGER_ID_MENU);
}

static lv_obj_t *_home_shortcut_tile(lv_obj_t *parent, uint32_t image_id,
                                     const char *fallback_symbol,
                                     const char *label,
                                     home_shortcut_view_ctx_t *ctx)
{
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_remove_style_all(tile);
    app_ui_click_only(tile);
    /* Every dock slot owns an equal share so the four icons read as one row. */
    lv_obj_set_width(tile, 0);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(tile, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_pad_row(tile, 4, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(tile, _home_shortcut_pressed, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(tile, _home_shortcut_press_lost, LV_EVENT_PRESS_LOST,
                        ctx);
    lv_obj_add_event_cb(tile, _home_shortcut_long_pressed,
                        LV_EVENT_LONG_PRESSED, ctx);
    lv_obj_add_event_cb(tile, _home_shortcut_clicked, LV_EVENT_CLICKED, ctx);

    lv_obj_t *well = lv_obj_create(tile);
    lv_obj_remove_style_all(well);
    lv_obj_set_size(well, HOME_SHORTCUT_WELL, HOME_SHORTCUT_WELL);
    lv_obj_set_style_radius(well, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(well, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
    app_ui_make_passive(well, false);

    const lv_image_dsc_t *descriptor = NULL;
    if (image_id != 0U &&
            app_manager_get_image(image_id, &descriptor) == ESP_OK &&
            descriptor != NULL)
    {
        lv_obj_t *image = lv_image_create(well);
        lv_obj_set_size(image, HOME_SHORTCUT_IMAGE, HOME_SHORTCUT_IMAGE);
        lv_image_set_src(image, descriptor);
        lv_image_set_scale(image,
                           (uint32_t)HOME_SHORTCUT_IMAGE * 256U / 72U);
        app_ui_make_passive(image, false);
        lv_obj_center(image);
    }
    else
    {
        lv_obj_t *symbol = lv_label_create(well);
        lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(APP_UI_COLOR_TEXT), 0);
        lv_label_set_text(symbol, fallback_symbol != NULL ? fallback_symbol :
                          LV_SYMBOL_IMAGE);
        app_ui_make_passive(symbol, false);
        lv_obj_center(symbol);
    }

    lv_obj_t *caption = lv_label_create(tile);
    lv_obj_set_width(caption, LV_PCT(100));
    lv_obj_set_height(caption, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(caption, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    /* Four equal 72 px slots on the 320 px panel leave 80 px labels tight;
     * a small negative tracking keeps the longest built-in name on one line. */
    lv_obj_set_style_text_letter_space(caption, -3, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_label_set_text(caption, label != NULL ? label : "");
    app_ui_make_passive(caption, false);
    return tile;
}

static void _home_rebuild_shortcuts(home_page_state_t *state)
{
    if (state->shortcut_dock == NULL)
    {
        return;
    }
    lv_obj_clean(state->shortcut_dock);
    const home_shortcut_prefs_t *prefs = home_shortcuts_get();
    const app_manager_app_desc_t *catalog[HOME_SHORTCUT_CATALOG_MAX];
    const size_t catalog_count = home_shortcut_catalog(catalog,
        HOME_SHORTCUT_CATALOG_MAX);
    const app_manager_app_desc_t *effective[HOME_SHORTCUT_SLOTS];
    const size_t effective_count = home_shortcut_effective(prefs, catalog,
        catalog_count, effective,
        HOME_SHORTCUT_SLOTS);

    for (size_t index = 0U; index < effective_count; ++index)
    {
        const app_manager_app_desc_t *app = effective[index];
        state->shortcut_ctx[index].state = state;
        state->shortcut_ctx[index].app_id = app->id;
        (void)_home_shortcut_tile(state->shortcut_dock, app->icon_id,
                                  LV_SYMBOL_IMAGE,
                                  app->display_name != NULL ?
                                  app->display_name : app->name,
                                  &state->shortcut_ctx[index]);
    }
    state->app_ctx.state = state;
    state->app_ctx.app_id = NULL;
    (void)_home_shortcut_tile(state->shortcut_dock, APP_IMAGE_MENU_ICON,
                              LV_SYMBOL_LIST, "应用", &state->app_ctx);
    state->shortcut_generation = home_shortcuts_generation();
}

static void _home_build_shortcuts(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *dock = lv_obj_create(content);
    lv_obj_remove_style_all(dock);
    lv_obj_set_width(dock, LV_PCT(100));
    lv_obj_set_height(dock, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(dock, 0, 0);
    app_ui_make_passive(dock, false);
    state->shortcut_dock = dock;
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create_home(&state->page);
    lv_obj_t *content = state->page.content;
    _home_build_status(state, content);
    _home_build_hero(state, content);
    _home_build_weather(state, content);
    _home_build_timer(state, content);
    _home_build_shortcuts(state, content);
    _home_refresh(state);
    _home_rebuild_shortcuts(state);
}

static void _home_page_start(const app_manager_page_context_t *context)
{
    (void)context;
    (void)home_shortcuts_ensure_loaded();
}

static void _home_page_resume(const app_manager_page_context_t *context)
{
    home_page_state_t *state = context->state;
    (void)home_shortcuts_ensure_loaded();
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
    if (state->shortcut_generation != home_shortcuts_generation())
    {
        _home_rebuild_shortcuts(state);
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
    for (size_t index = 0; index < 3U; index++)
    {
        state->wifi_arcs[index] = NULL;
    }
    state->wifi_dot = NULL;
    state->wifi_status = NULL;
    state->bluetooth_icon = NULL;
    state->bluetooth_status = NULL;
    state->battery_fill = NULL;
    state->battery_status = NULL;
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_pill = NULL;
    state->weather_band = NULL;
    state->weather_image = NULL;
    state->weather_fallback = NULL;
    state->weather_city = NULL;
    state->weather_value = NULL;
    state->weather_condition = NULL;
    state->timer_band = NULL;
    state->clock_ring = NULL;
    state->clock_title = NULL;
    state->clock_value = NULL;
    state->clock_caption = NULL;
    state->shortcut_dock = NULL;
    state->shortcut_long_pressed = false;
    for (size_t index = 0; index < HOME_SHORTCUT_SLOTS; index++)
    {
        state->shortcut_ctx[index].state = NULL;
        state->shortcut_ctx[index].app_id = NULL;
    }
    state->app_ctx.state = NULL;
    state->app_ctx.app_id = NULL;
}

static const app_manager_page_ops_t s_home_ops =
{
    .start = _home_page_start,
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
    { .page_id = HOME_SHORTCUTS_PAGE_ID, .definition = &home_shortcuts_page_definition },
};

APP_MANAGER_APP_EXPORT_META(home, APP_IMAGE_HOME_ICON, "主页", APP_MANAGER_ID_HOME,
                            "root", APP_MANAGER_APP_FLAG_PINNED, s_home_routes,
                            1U, "今日概览");
