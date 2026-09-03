#define DBG_TAG "home_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "app_weather_ui.h"
#include "connectivity_manager.h"
#include "device_link_service.h"
#include "event_bus.h"
#include "power_service.h"
#include "time_service.h"
#include "timer_service.h"
#include "weather_service.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOME_PI              3.14159265358979f

#define HOME_DIAL_SIZE       120
#define HOME_DIAL_CENTER     (HOME_DIAL_SIZE / 2)
#define HOME_HAND_HOUR_LEN   32.0f
#define HOME_HAND_MINUTE_LEN 46.0f
#define HOME_HAND_SECOND_LEN 50.0f
#define HOME_TICK_RADIUS     54

#define HOME_TILE_SIZE       80
#define HOME_TILE_RING       48
#define HOME_TILE_GLYPH      52
#define HOME_WEATHER_HEIGHT  84

typedef struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *wifi_arcs[3];
    lv_obj_t *wifi_dot;
    lv_obj_t *bluetooth_status;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_status;
    lv_obj_t *hand_hour;
    lv_obj_t *hand_minute;
    lv_obj_t *hand_second;
    lv_point_precise_t hand_hour_points[2];
    lv_point_precise_t hand_minute_points[2];
    lv_point_precise_t hand_second_points[2];
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_label;
    lv_obj_t *weather_panel;
    lv_obj_t *weather_image;
    lv_obj_t *weather_fallback;
    lv_obj_t *weather_city;
    lv_obj_t *weather_value;
    lv_obj_t *weather_condition;
    lv_obj_t *weather_sub;
    lv_obj_t *clock_tile_ring;
    lv_obj_t *clock_tile_caption;
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
        lv_obj_add_flag(state->weather_fallback, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(state->weather_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->weather_fallback, LV_OBJ_FLAG_HIDDEN);
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

static void _home_hand_point(lv_point_precise_t *points, float degrees,
                             float length)
{
    const float radians = degrees * HOME_PI / 180.0f;
    points[0].x = HOME_DIAL_CENTER;
    points[0].y = HOME_DIAL_CENTER;
    points[1].x = HOME_DIAL_CENTER + length * sinf(radians);
    points[1].y = HOME_DIAL_CENTER - length * cosf(radians);
}

static lv_obj_t *_home_hand(lv_obj_t *parent, lv_point_precise_t *points,
                            int32_t width, uint32_t color)
{
    /* Park both endpoints on the pivot so a zero-length hand never renders
     * at the object's top-left corner before the first valid time. */
    points[0].x = HOME_DIAL_CENTER;
    points[0].y = HOME_DIAL_CENTER;
    points[1].x = HOME_DIAL_CENTER;
    points[1].y = HOME_DIAL_CENTER;
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_size(line, HOME_DIAL_SIZE, HOME_DIAL_SIZE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_line_set_points_mutable(line, points, 2);
    app_ui_make_passive(line, false);
    return line;
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
        lv_obj_add_flag(state->hand_hour, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->hand_minute, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->hand_second, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(state->hand_hour, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(state->hand_minute, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(state->hand_second, LV_OBJ_FLAG_HIDDEN);
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
    _home_hand_point(state->hand_hour_points,
                     ((float)(local_time.tm_hour % 12) +
                      local_time.tm_min / 60.0f) * 30.0f,
                     HOME_HAND_HOUR_LEN);
    _home_hand_point(state->hand_minute_points,
                     ((float)local_time.tm_min + local_time.tm_sec / 60.0f) * 6.0f,
                     HOME_HAND_MINUTE_LEN);
    _home_hand_point(state->hand_second_points, (float)local_time.tm_sec * 6.0f,
                     HOME_HAND_SECOND_LEN);
    lv_obj_invalidate(state->hand_hour);
    lv_obj_invalidate(state->hand_minute);
    lv_obj_invalidate(state->hand_second);
}

static void _home_render_power(home_page_state_t *state)
{
    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        lv_label_set_text(state->battery_status, "--");
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
        (void)snprintf(text, sizeof(text), "%umV", (unsigned)mv);
    }
    lv_label_set_text(state->battery_status, text);
    const uint32_t fill_color = snapshot.info.is_charging ? APP_UI_COLOR_RAIN :
                                (percent <= 15 ? APP_UI_COLOR_WARNING :
                                 APP_UI_COLOR_TEXT);
    lv_obj_set_style_bg_color(state->battery_fill, lv_color_hex(fill_color), 0);
    lv_obj_set_width(state->battery_fill, percent * 24 / 100);
    _home_set_color(state->battery_status,
                    snapshot.info.is_charging ? APP_UI_COLOR_RAIN :
                    APP_UI_COLOR_MUTED);
}

static void _home_render_wifi(home_page_state_t *state)
{
    uint32_t color = APP_UI_COLOR_MUTED;
    connectivity_manager_status_snapshot_t snapshot;
    if (connectivity_manager_get_status(&snapshot) != ESP_OK)
    {
        color = APP_UI_COLOR_WARNING;
    }
    else if (!snapshot.available)
    {
        color = state->wifi_initialization_elapsed ? APP_UI_COLOR_WARNING :
                APP_UI_COLOR_SUN;
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        color = APP_UI_COLOR_RAIN;
    }
    else if (snapshot.state == CONNECTIVITY_MANAGER_STATE_CONNECTING ||
             snapshot.state == CONNECTIVITY_MANAGER_STATE_WAITING_IP)
    {
        color = APP_UI_COLOR_SUN;
    }
    for (size_t index = 0; index < 3U; index++)
    {
        lv_obj_set_style_arc_color(state->wifi_arcs[index], lv_color_hex(color),
                                   LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(state->wifi_dot, lv_color_hex(color), 0);
}

static void _home_render_bluetooth(home_page_state_t *state)
{
    device_link_service_status_t status;
    if (device_link_service_get_status(&status) != ESP_OK || !status.available)
    {
        _home_set_color(state->bluetooth_status, APP_UI_COLOR_WARNING);
    }
    else if (status.pending_confirmation || status.active)
    {
        _home_set_color(state->bluetooth_status, APP_UI_COLOR_SUN);
    }
    else
    {
        _home_set_color(state->bluetooth_status,
                        status.bound ? APP_UI_COLOR_RAIN : APP_UI_COLOR_MUTED);
    }
}

static void _home_render_weather(home_page_state_t *state)
{
    const weather_service_snapshot_t *snapshot = NULL;
    if (weather_service_snapshot_acquire(&snapshot) != ESP_OK || snapshot == NULL)
    {
        _home_set_weather_image(state, 0U, false);
        lv_label_set_text(state->weather_city, "天气");
        lv_label_set_text(state->weather_value, "--");
        lv_label_set_text(state->weather_condition, "服务不可用");
        lv_label_set_text(state->weather_sub, "");
        _home_set_color(state->weather_value, APP_UI_COLOR_WARNING);
        return;
    }
    lv_label_set_text(state->weather_city,
                      snapshot->location.city[0] != '\0' ?
                      snapshot->location.city : "天气");
    char sub[48];
    sub[0] = '\0';
    if ((snapshot->available_mask & WEATHER_SERVICE_DATA_DAILY) != 0U &&
            snapshot->daily.count > 0U)
    {
        const weather_service_day_t *today = &snapshot->daily.items[0];
        (void)snprintf(sub, sizeof(sub), "今%ld°/%ld°",
                       (long)lroundf(today->maximum_temperature_tenths_c / 10.0f),
                       (long)lroundf(today->minimum_temperature_tenths_c / 10.0f));
    }
    if ((snapshot->available_mask & WEATHER_SERVICE_DATA_CURRENT) != 0U)
    {
        char temp[24];
        (void)snprintf(temp, sizeof(temp), "%.1f°",
                       snapshot->current.temperature_tenths_c / 10.0f);
        lv_label_set_text(state->weather_value, temp);
        lv_label_set_text(state->weather_condition,
                          snapshot->current.condition_text[0] != '\0' ?
                          snapshot->current.condition_text : "天气已更新");
        _home_set_color(state->weather_value, APP_UI_COLOR_SUN);
        _home_set_weather_image(state, snapshot->current.condition_code, true);
        if (sub[0] != '\0')
        {
            char humidity[24];
            (void)snprintf(humidity, sizeof(humidity), " 湿%u%%",
                           (unsigned)snapshot->current.humidity_percent);
            strncat(sub, humidity, sizeof(sub) - strlen(sub) - 1U);
        }
    }
    else
    {
        lv_label_set_text(state->weather_value, "--");
        weather_service_status_snapshot_t status;
        const char *condition = weather_service_get_status(&status) == ESP_OK &&
                                !status.configured ? "未配置" : "等待天气";
        lv_label_set_text(state->weather_condition, condition);
        _home_set_color(state->weather_value, APP_UI_COLOR_MUTED);
        _home_set_weather_image(state, 0U, false);
    }
    lv_label_set_text(state->weather_sub, sub);
    weather_service_snapshot_release(snapshot);
}

static void _home_render_timer_tile(home_page_state_t *state)
{
    timer_service_snapshot_t snapshot;
    uint32_t accent = APP_UI_COLOR_RAIN;
    bool active = false;
    uint32_t remaining_ms = 0U;
    uint32_t total_ms = 0U;
    if (timer_service_get_snapshot(&snapshot) == ESP_OK)
    {
        if (snapshot.countdown_state == TIMER_SERVICE_RUNNING ||
                snapshot.countdown_state == TIMER_SERVICE_PAUSED)
        {
            active = true;
            remaining_ms = snapshot.countdown_remaining_ms;
            total_ms = snapshot.countdown_duration_ms;
            accent = snapshot.countdown_state == TIMER_SERVICE_PAUSED ?
                     APP_UI_COLOR_MUTED : APP_UI_COLOR_RAIN;
        }
        else if (snapshot.focus_state == TIMER_SERVICE_RUNNING ||
                 snapshot.focus_state == TIMER_SERVICE_PAUSED)
        {
            active = true;
            remaining_ms = snapshot.focus_remaining_ms;
            accent = snapshot.focus_state == TIMER_SERVICE_PAUSED ?
                     APP_UI_COLOR_MUTED : APP_UI_COLOR_SUN;
        }
    }
    /* The caption stays time-only: mode semantics ride on the sweep color,
     * and "标题 mm:ss" clipped against the tile width at 99:59. */
    char text[16];
    if (active && remaining_ms > 0U)
    {
        (void)snprintf(text, sizeof(text), "%u:%02u",
                       (unsigned)(remaining_ms / 60000U),
                       (unsigned)((remaining_ms / 1000U) % 60U));
    }
    else
    {
        (void)snprintf(text, sizeof(text), "时钟");
    }
    lv_label_set_text(state->clock_tile_caption, text);
    _home_set_color(state->clock_tile_caption,
                    active ? accent : APP_UI_COLOR_MUTED);
    lv_obj_set_style_arc_color(state->clock_tile_ring, lv_color_hex(accent),
                               LV_PART_INDICATOR);
    if (!active)
    {
        lv_arc_set_angles(state->clock_tile_ring, 0, 0);
        lv_obj_set_style_arc_opa(state->clock_tile_ring, LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        return;
    }
    lv_obj_set_style_arc_opa(state->clock_tile_ring, LV_OPA_COVER,
                             LV_PART_INDICATOR);
    /* Focus snapshots expose no phase total, so a missing duration renders a
     * full-strength ring instead of a misleading sweep. */
    const uint32_t span = (total_ms > 0U && remaining_ms <= total_ms) ?
                          360U * remaining_ms / total_ms : 360U;
    lv_arc_set_angles(state->clock_tile_ring, 0, (lv_value_precise_t)span);
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
    _home_render_timer_tile(state);
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
        LOG_D("home tile click: %s", app_id);
        app_ui_request_run(app_id);
    }
}

static void _home_build_status(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *status = lv_obj_create(content);
    lv_obj_remove_style_all(status);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, 22);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 10, 0);
    app_ui_make_passive(status, false);

    lv_obj_t *wifi = lv_obj_create(status);
    lv_obj_remove_style_all(wifi);
    lv_obj_set_size(wifi, 28, 22);
    app_ui_make_passive(wifi, false);
    lv_obj_add_flag(wifi, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
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

    state->bluetooth_status = _home_symbol_label(status, LV_SYMBOL_BLUETOOTH);

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
    lv_obj_add_flag(battery, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    state->battery_status = _home_label(status, "--", APP_THEME_FONT_BODY,
                                        APP_UI_COLOR_MUTED);
    lv_obj_set_width(state->battery_status, 48);
    lv_obj_set_style_text_align(state->battery_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(state->battery_status, LV_LABEL_LONG_CLIP);
}

static void _home_build_dial(home_page_state_t *state, lv_obj_t *clock)
{
    lv_obj_t *dial = lv_obj_create(clock);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, HOME_DIAL_SIZE, HOME_DIAL_SIZE);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial, 2, 0);
    lv_obj_set_style_border_color(dial, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_opa(dial, LV_OPA_80, 0);
    app_ui_make_passive(dial, false);
    lv_obj_add_flag(dial, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *glow = lv_obj_create(dial);
    lv_obj_set_size(glow, HOME_DIAL_SIZE + 10, HOME_DIAL_SIZE + 10);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glow, 6, 0);
    lv_obj_set_style_border_color(glow, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_10, 0);
    lv_obj_center(glow);
    app_ui_make_passive(glow, false);

    for (int index = 0; index < 12; index++)
    {
        const float radians = index * 30.0f * HOME_PI / 180.0f;
        const bool quarter = index % 3 == 0;
        const int32_t size = quarter ? 5 : 3;
        lv_obj_t *tick = lv_obj_create(dial);
        lv_obj_remove_style_all(tick);
        lv_obj_set_size(tick, size, size);
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(tick, quarter ? LV_OPA_COVER : LV_OPA_60, 0);
        lv_obj_set_style_bg_color(tick, lv_color_hex(APP_UI_COLOR_TEXT), 0);
        lv_obj_set_pos(tick,
                       HOME_DIAL_CENTER + (int32_t)lroundf(HOME_TICK_RADIUS * sinf(radians)) -
                       size / 2,
                       HOME_DIAL_CENTER - (int32_t)lroundf(HOME_TICK_RADIUS * cosf(radians)) -
                       size / 2);
        app_ui_make_passive(tick, false);
    }

    state->hand_hour = _home_hand(dial, state->hand_hour_points, 6,
                                  APP_UI_COLOR_TEXT);
    state->hand_minute = _home_hand(dial, state->hand_minute_points, 4,
                                    APP_UI_COLOR_TEXT);
    state->hand_second = _home_hand(dial, state->hand_second_points, 2,
                                    APP_UI_COLOR_RAIN);
    lv_obj_t *pivot = lv_obj_create(dial);
    lv_obj_remove_style_all(pivot);
    lv_obj_set_size(pivot, 7, 7);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(pivot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pivot, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_center(pivot);
    app_ui_make_passive(pivot, false);
}

static void _home_build_weather(home_page_state_t *state, lv_obj_t *content)
{
    state->weather_panel = lv_button_create(content);
    app_ui_click_only(state->weather_panel);
    lv_obj_set_width(state->weather_panel, LV_PCT(100));
    lv_obj_set_height(state->weather_panel, HOME_WEATHER_HEIGHT);
    lv_obj_set_style_radius(state->weather_panel, 12, 0);
    lv_obj_set_style_bg_color(state->weather_panel,
                              lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(state->weather_panel,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(state->weather_panel, 0, 0);
    lv_obj_set_style_pad_left(state->weather_panel, 12, 0);
    lv_obj_set_style_pad_right(state->weather_panel, 12, 0);
    lv_obj_set_style_pad_top(state->weather_panel, 8, 0);
    lv_obj_set_style_pad_bottom(state->weather_panel, 8, 0);
    lv_obj_set_style_pad_column(state->weather_panel, 10, 0);
    lv_obj_set_flex_flow(state->weather_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->weather_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(state->weather_panel, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_WEATHER);

    state->weather_image = lv_image_create(state->weather_panel);
    lv_obj_set_size(state->weather_image, 40, 40);
    app_ui_make_passive(state->weather_image, false);
    state->weather_fallback = _home_symbol_label(state->weather_panel,
                              LV_SYMBOL_IMAGE);
    lv_obj_set_size(state->weather_fallback, 40, 40);
    lv_obj_set_style_pad_top(state->weather_fallback, 9, 0);

    lv_obj_t *info = lv_obj_create(state->weather_panel);
    lv_obj_remove_style_all(info);
    lv_obj_set_width(info, 0);
    lv_obj_set_height(info, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 0, 0);
    app_ui_make_passive(info, false);
    state->weather_city = _home_label(info, "天气", APP_THEME_FONT_BODY,
                                      APP_UI_COLOR_RAIN);
    state->weather_condition = _home_label(info, "等待天气数据",
                                           APP_THEME_FONT_BODY,
                                           APP_UI_COLOR_TEXT);
    state->weather_sub = _home_label(info, "", APP_THEME_FONT_BODY,
                                     APP_UI_COLOR_MUTED);
    lv_label_set_long_mode(state->weather_city, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(state->weather_condition,
                           LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_long_mode(state->weather_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(state->weather_city, LV_PCT(100));
    lv_obj_set_width(state->weather_condition, LV_PCT(100));
    lv_obj_set_width(state->weather_sub, LV_PCT(100));

    state->weather_value = _home_label(state->weather_panel, "--",
                                       APP_THEME_FONT_HEAD, APP_UI_COLOR_SUN);
    /* Stable width from the worst-case temperature ("-88.8°") measured with
     * the live theme font; a fixed 76 px clipped leading digits at 26.0°. */
    lv_point_t value_size;
    lv_text_get_size(&value_size, "-88.8°", app_ui_font(APP_THEME_FONT_HEAD),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(state->weather_value, value_size.x + 8);
    lv_obj_set_style_text_align(state->weather_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(state->weather_value, LV_LABEL_LONG_CLIP);
}

/* One flat dark tile button: ring border, column of glyph + caption. */
static lv_obj_t *_home_tile_button(lv_obj_t *parent)
{
    lv_obj_t *button = lv_button_create(parent);
    app_ui_click_only(button);
    lv_obj_remove_style_all(button);
    lv_obj_set_width(button, 0);
    lv_obj_set_height(button, HOME_TILE_SIZE);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(button, HOME_TILE_SIZE, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button,
                                  lv_color_hex(APP_UI_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_SURFACE),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    return button;
}

static lv_obj_t *_home_tile_caption(lv_obj_t *tile, const char *text)
{
    lv_obj_t *caption = _home_label(tile, text, APP_THEME_FONT_BODY,
                                    APP_UI_COLOR_MUTED);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    return caption;
}

static void _home_build_tile_glyph(lv_obj_t *tile, uint32_t image_id,
                                   const char *fallback_symbol)
{
    const lv_image_dsc_t *descriptor = NULL;
    if (image_id == 0U ||
            app_manager_get_image(image_id, &descriptor) != ESP_OK ||
            descriptor == NULL)
    {
        lv_obj_t *fallback = _home_symbol_label(tile, fallback_symbol);
        lv_obj_set_size(fallback, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        return;
    }
    lv_obj_t *image = lv_image_create(tile);
    lv_image_set_src(image, descriptor);
    app_ui_make_passive(image, false);
}

static void _home_build_tiles(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, HOME_TILE_SIZE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    app_ui_make_passive(row, false);

    lv_obj_t *clock_tile = _home_tile_button(row);
    state->clock_tile_ring = _home_ring_arc(clock_tile, HOME_TILE_RING, 3,
                                            APP_UI_COLOR_SURFACE_HI, true);
    lv_arc_set_rotation(state->clock_tile_ring, 270);
    /* Mini clock pose inside the ring: minute hand to 12, hour hand to 2. */
    static lv_point_precise_t mini_hour_points[2];
    static lv_point_precise_t mini_minute_points[2];
    mini_hour_points[0].x = HOME_TILE_RING / 2;
    mini_hour_points[0].y = HOME_TILE_RING / 2;
    mini_hour_points[1].x = HOME_TILE_RING / 2 + 10;
    mini_hour_points[1].y = HOME_TILE_RING / 2 - 6;
    mini_minute_points[0].x = HOME_TILE_RING / 2;
    mini_minute_points[0].y = HOME_TILE_RING / 2;
    mini_minute_points[1].x = HOME_TILE_RING / 2;
    mini_minute_points[1].y = 8;
    lv_obj_t *mini_hour = lv_line_create(state->clock_tile_ring);
    lv_obj_t *mini_minute = lv_line_create(state->clock_tile_ring);
    lv_obj_set_size(mini_hour, HOME_TILE_RING, HOME_TILE_RING);
    lv_obj_set_size(mini_minute, HOME_TILE_RING, HOME_TILE_RING);
    lv_obj_set_style_line_width(mini_hour, 4, 0);
    lv_obj_set_style_line_width(mini_minute, 3, 0);
    lv_obj_set_style_line_color(mini_hour, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_line_color(mini_minute, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_line_set_points(mini_hour, mini_hour_points, 2);
    lv_line_set_points(mini_minute, mini_minute_points, 2);
    app_ui_make_passive(mini_hour, false);
    app_ui_make_passive(mini_minute, false);
    lv_obj_t *mini_pivot = lv_obj_create(state->clock_tile_ring);
    lv_obj_remove_style_all(mini_pivot);
    lv_obj_set_size(mini_pivot, 4, 4);
    lv_obj_set_style_radius(mini_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mini_pivot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mini_pivot, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_align(mini_pivot, LV_ALIGN_CENTER, 0, 0);
    app_ui_make_passive(mini_pivot, false);
    state->clock_tile_caption = _home_tile_caption(clock_tile, "时钟");
    lv_obj_add_event_cb(clock_tile, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_CLOCK);

    lv_obj_t *recorder_tile = _home_tile_button(row);
    _home_build_tile_glyph(recorder_tile, APP_IMAGE_HOME_RECORDER,
                           LV_SYMBOL_AUDIO);
    (void)_home_tile_caption(recorder_tile, "录音");
    lv_obj_add_event_cb(recorder_tile, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_RECORDER);

    lv_obj_t *level_tile = _home_tile_button(row);
    _home_build_tile_glyph(level_tile, APP_IMAGE_HOME_LEVEL, LV_SYMBOL_SHUFFLE);
    (void)_home_tile_caption(level_tile, "水平仪");
    lv_obj_add_event_cb(level_tile, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_LEVEL);

    lv_obj_t *settings_tile = _home_tile_button(row);
    _home_build_tile_glyph(settings_tile, APP_IMAGE_HOME_SETTINGS,
                           LV_SYMBOL_SETTINGS);
    (void)_home_tile_caption(settings_tile, "设置");
    lv_obj_add_event_cb(settings_tile, _home_open_app, LV_EVENT_CLICKED,
                        (void *)APP_MANAGER_ID_SETTINGS);
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create_home(&state->page);
    lv_obj_t *content = state->page.content;
    lv_obj_set_style_pad_row(content, 8, 0);
    _home_build_status(state, content);

    lv_obj_t *clock = lv_obj_create(content);
    lv_obj_remove_style_all(clock);
    lv_obj_set_width(clock, LV_PCT(100));
    lv_obj_set_height(clock, 0);
    lv_obj_set_flex_grow(clock, 1);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(clock, 0, 0);
    app_ui_make_passive(clock, false);
    _home_build_dial(state, clock);
    state->time_label = _home_label(clock, "--:--", APP_THEME_FONT_HEAD,
                                    APP_UI_COLOR_TEXT);
    lv_obj_set_style_text_align(state->time_label, LV_TEXT_ALIGN_CENTER, 0);
    state->date_label = _home_label(clock, "等待有效时间", APP_THEME_FONT_BODY,
                                    APP_UI_COLOR_MUTED);
    lv_obj_set_style_text_align(state->date_label, LV_TEXT_ALIGN_CENTER, 0);
    state->quality_label = _home_label(clock, "时间未校准", APP_THEME_FONT_BODY,
                                       APP_UI_COLOR_SUN);
    lv_obj_set_style_text_align(state->quality_label, LV_TEXT_ALIGN_CENTER, 0);

    _home_build_weather(state, content);
    _home_build_tiles(state, content);
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
    for (size_t index = 0; index < 3U; index++)
    {
        state->wifi_arcs[index] = NULL;
    }
    state->wifi_dot = NULL;
    state->bluetooth_status = NULL;
    state->battery_fill = NULL;
    state->battery_status = NULL;
    state->hand_hour = NULL;
    state->hand_minute = NULL;
    state->hand_second = NULL;
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_label = NULL;
    state->weather_panel = NULL;
    state->weather_image = NULL;
    state->weather_fallback = NULL;
    state->weather_city = NULL;
    state->weather_value = NULL;
    state->weather_condition = NULL;
    state->weather_sub = NULL;
    state->clock_tile_ring = NULL;
    state->clock_tile_caption = NULL;
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
