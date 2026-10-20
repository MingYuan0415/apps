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
#include "power_service.h"
#include "recorder_service.h"
#include "time_service.h"
#include "timer_service.h"
#include "weather_service.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOME_STATUS_HEIGHT   24
#define HOME_CARD_HEIGHT     112
#define HOME_CLOCK_RING      44

typedef struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *wifi_arcs[3];
    lv_obj_t *wifi_dot;
    lv_obj_t *bluetooth_status;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_status;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_pill;
    lv_obj_t *weather_image;
    lv_obj_t *weather_fallback;
    lv_obj_t *weather_city;
    lv_obj_t *weather_value;
    lv_obj_t *weather_condition;
    lv_obj_t *clock_ring;
    lv_obj_t *clock_title;
    lv_obj_t *clock_value;
    lv_obj_t *clock_caption;
    lv_obj_t *recorder_value;
    lv_obj_t *recorder_caption;
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t power_subscription;
    event_bus_sub_handle_t wifi_subscription;
    event_bus_sub_handle_t weather_subscription;
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

/* Place a small app glyph at the center of a progress ring when available. */
static void _home_ring_glyph(lv_obj_t *ring, uint32_t image_id,
                             const char *fallback_symbol)
{
    const lv_image_dsc_t *descriptor = NULL;
    if (image_id != 0U &&
            app_manager_get_image(image_id, &descriptor) == ESP_OK &&
            descriptor != NULL)
    {
        lv_obj_t *image = lv_image_create(ring);
        lv_obj_set_size(image, 28, 28);
        lv_image_set_src(image, descriptor);
        app_ui_make_passive(image, false);
        lv_obj_center(image);
        return;
    }
    lv_obj_t *symbol = lv_label_create(ring);
    lv_obj_set_style_text_font(symbol, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(symbol, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_label_set_text(symbol, fallback_symbol != NULL ? fallback_symbol :
                      LV_SYMBOL_IMAGE);
    app_ui_make_passive(symbol, false);
    lv_obj_center(symbol);
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
    uint32_t color = APP_UI_COLOR_MUTED;
    connectivity_manager_status_snapshot_t snapshot;
    if (connectivity_manager_get_status(&snapshot) != ESP_OK)
    {
        color = APP_UI_COLOR_WARNING;
    }
    else if (!snapshot.available)
    {
        if (s_wifi_grace_ticks < HOME_WIFI_GRACE_TICKS)
        {
            s_wifi_grace_ticks++;
            color = APP_UI_COLOR_SUN;
        }
        else
        {
            color = APP_UI_COLOR_WARNING;
        }
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
        _home_set_weather_image(state, 0U, true);
        app_ui_label_set_text_if(state->weather_city, "天气");
        app_ui_label_set_text_if(state->weather_value, "--");
        app_ui_label_set_text_if(state->weather_condition, "服务不可用");
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

static void _home_render_clock_card(home_page_state_t *state)
{
    timer_service_snapshot_t snapshot;
    const char *title = "时钟";
    const char *value = "计时";
    const char *caption = "倒计时 · 专注";
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
    char text[16];
    if (active)
    {
        (void)snprintf(text, sizeof(text), "%u:%02u",
                       (unsigned)(remaining_ms / 60000U),
                       (unsigned)((remaining_ms / 1000U) % 60U));
        value = text;
        caption = paused ? "已暂停" : "进行中";
    }
    if (paused)
    {
        accent = APP_UI_COLOR_MUTED;
    }
    app_ui_label_set_text_if(state->clock_title, title);
    app_ui_label_set_text_if(state->clock_value, value);
    app_ui_label_set_text_if(state->clock_caption, caption);
    _home_set_color(state->clock_title, active ? accent : APP_UI_COLOR_MUTED);
    _home_set_color(state->clock_value,
                    active ? APP_UI_COLOR_TEXT : APP_UI_COLOR_MUTED);
    lv_obj_set_style_arc_color(state->clock_ring, lv_color_hex(accent),
                               LV_PART_INDICATOR);
    if (!active)
    {
        lv_arc_set_angles(state->clock_ring, 0, 0);
        lv_obj_set_style_arc_opa(state->clock_ring, LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        return;
    }
    lv_obj_set_style_arc_opa(state->clock_ring, LV_OPA_COVER,
                             LV_PART_INDICATOR);
    /* Focus snapshots expose no phase total, so a missing duration renders a
     * full-strength ring instead of a misleading sweep. 64-bit product: the
     * countdown can run up to 779 minutes. */
    const uint32_t span = (total_ms > 0U && remaining_ms <= total_ms) ?
                          (uint32_t)(((uint64_t)remaining_ms * 360U) /
                                     total_ms) : 360U;
    lv_arc_set_angles(state->clock_ring, 0, (lv_value_precise_t)span);
}

static void _home_render_recorder(home_page_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    const char *value = "就绪";
    const char *caption = "语音备忘";
    uint32_t color = APP_UI_COLOR_MUTED;
    char text[16];
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        value = "不可用";
        caption = "检查存储卡";
    }
    else if (snapshot.state == RECORDER_SERVICE_RECORDING ||
             snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        const unsigned seconds = (unsigned)(snapshot.duration_ms / 1000U);
        (void)snprintf(text, sizeof(text), "%u:%02u", seconds / 60U,
                       seconds % 60U);
        value = text;
        const bool recording = snapshot.state == RECORDER_SERVICE_RECORDING;
        caption = recording ? "录音中" : "已暂停";
        color = recording ? APP_UI_COLOR_WARNING : APP_UI_COLOR_TEXT;
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        value = "播放中";
        caption = "点按查看";
        color = APP_UI_COLOR_RAIN;
    }
    else if (snapshot.state == RECORDER_SERVICE_ERROR)
    {
        value = "异常";
        caption = "点按查看";
        color = APP_UI_COLOR_WARNING;
    }
    app_ui_label_set_text_if(state->recorder_value, value);
    app_ui_label_set_text_if(state->recorder_caption, caption);
    _home_set_color(state->recorder_value, color);
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
    _home_render_clock_card(state);
    _home_render_recorder(state);
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
    lv_obj_set_height(status, HOME_STATUS_HEIGHT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 10, 0);
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
    lv_obj_set_overflow_visible(battery, true);

    state->battery_status = _home_label(status, "--", APP_THEME_FONT_SMALL,
                                        APP_UI_COLOR_MUTED);
    /* Stable width from the longest reading ("100%") measured with the live
     * theme font; a fixed 48 px clipped the leading digit at 100%. */
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

/* One complication: caption label, a value row with a trailing glyph slot,
 * and a faint detail line. Returns the value row for the glyph. */
static lv_obj_t *_home_card(lv_obj_t *grid, const char *app_id,
                            const char *title, lv_obj_t **title_out,
                            lv_obj_t **value_out, lv_obj_t **caption_out)
{
    lv_obj_t *card = lv_button_create(grid);
    app_ui_click_only(card);
    lv_obj_set_size(card, LV_PCT(48), HOME_CARD_HEIGHT);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_left(card, 14, 0);
    lv_obj_set_style_pad_right(card, 10, 0);
    lv_obj_set_style_pad_top(card, 10, 0);
    lv_obj_set_style_pad_bottom(card, 10, 0);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(card, _home_open_app, LV_EVENT_CLICKED,
                        (void *)app_id);

    lv_obj_t *label = _home_label(card, title, APP_THEME_FONT_SMALL,
                                  APP_UI_COLOR_MUTED);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    if (title_out != NULL)
    {
        *title_out = label;
    }

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    app_ui_make_passive(row, false);
    lv_obj_t *value = _home_label(row, "--", APP_THEME_FONT_HEAD,
                                  APP_UI_COLOR_TEXT);
    lv_obj_set_width(value, 0);
    lv_obj_set_flex_grow(value, 1);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    *value_out = value;

    lv_obj_t *caption = _home_label(card, "", APP_THEME_FONT_SMALL,
                                    APP_UI_COLOR_FAINT);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    if (caption_out != NULL)
    {
        *caption_out = caption;
    }
    return row;
}

static void _home_card_glyph(lv_obj_t *row, uint32_t image_id,
                             const char *fallback_symbol)
{
    lv_obj_t *glyph = app_ui_image(row, image_id, fallback_symbol, 40);
    if (glyph != NULL)
    {
        app_ui_icon_set_color(glyph, APP_UI_COLOR_MUTED);
    }
}

static void _home_build_cards(home_page_state_t *state, lv_obj_t *content)
{
    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, APP_UI_GAP, 0);
    app_ui_make_passive(grid, false);

    lv_obj_t *row = _home_card(grid, APP_MANAGER_ID_WEATHER, "天气",
                               &state->weather_city, &state->weather_value,
                               &state->weather_condition);
    state->weather_image = lv_image_create(row);
    lv_obj_set_size(state->weather_image, 40, 40);
    app_ui_make_passive(state->weather_image, false);
    state->weather_fallback = _home_symbol_label(row, LV_SYMBOL_IMAGE);
    _home_set_color(state->weather_fallback, APP_UI_COLOR_MUTED);

    row = _home_card(grid, APP_MANAGER_ID_CLOCK, "时钟", &state->clock_title,
                     &state->clock_value, &state->clock_caption);
    state->clock_ring = _home_ring_arc(row, HOME_CLOCK_RING, 4,
                                       APP_UI_COLOR_TRACK, true);
    lv_arc_set_rotation(state->clock_ring, 270);
    lv_obj_set_style_arc_rounded(state->clock_ring, true, LV_PART_INDICATOR);
    _home_ring_glyph(state->clock_ring, APP_IMAGE_HOME_RING_CLOCK,
                     LV_SYMBOL_BELL);

    row = _home_card(grid, APP_MANAGER_ID_RECORDER, "录音", NULL,
                     &state->recorder_value, &state->recorder_caption);
    _home_card_glyph(row, APP_IMAGE_HOME_RECORDER, LV_SYMBOL_AUDIO);

    lv_obj_t *level_value = NULL;
    lv_obj_t *level_caption = NULL;
    row = _home_card(grid, APP_MANAGER_ID_LEVEL, "水平仪", NULL, &level_value,
                     &level_caption);
    lv_label_set_text(level_value, "倾角");
    _home_set_color(level_value, APP_UI_COLOR_MUTED);
    lv_label_set_text(level_caption, "气泡水平仪");
    _home_card_glyph(row, APP_IMAGE_HOME_LEVEL, LV_SYMBOL_SHUFFLE);
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create_home(&state->page);
    lv_obj_t *content = state->page.content;
    _home_build_status(state, content);
    _home_build_hero(state, content);
    _home_build_cards(state, content);
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
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_pill = NULL;
    state->weather_image = NULL;
    state->weather_fallback = NULL;
    state->weather_city = NULL;
    state->weather_value = NULL;
    state->weather_condition = NULL;
    state->clock_ring = NULL;
    state->clock_title = NULL;
    state->clock_value = NULL;
    state->clock_caption = NULL;
    state->recorder_value = NULL;
    state->recorder_caption = NULL;
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