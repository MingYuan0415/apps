#define DBG_TAG "weather_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_manager_image_ids.h"
#include "app_ui.h"
#include "event_bus.h"
#include "weather_service.h"

#include "esp_heap_caps.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define WEATHER_PAGE_SLOT_BYTES 2728U
#define WEATHER_PAGE_ALERTS     "alerts"
#define WEATHER_PAGE_DETAIL     "alert-detail"

#define WEATHER_COLOR_BACKGROUND 0x090D0F
#define WEATHER_COLOR_SURFACE    0x151B1F
#define WEATHER_COLOR_SURFACE_HI 0x20282D
#define WEATHER_COLOR_TEXT       0xF2F5F6
#define WEATHER_COLOR_MUTED      0x93A0A6
#define WEATHER_COLOR_SUN        0xF5C451
#define WEATHER_COLOR_RAIN       0x4FC4D8
#define WEATHER_COLOR_WARNING    0xFF756C
#define WEATHER_DETAIL_TEXT_BYTES 2048U

typedef struct weather_root_state
{
    app_ui_page_t page;
    lv_obj_t *city_label;
    lv_obj_t *status_label;
    lv_obj_t *temperature_label;
    lv_obj_t *condition_label;
    lv_obj_t *main_image;
    lv_obj_t *image_fallback;
    lv_obj_t *alert_button;
    lv_obj_t *alert_label;
    lv_obj_t *metrics;
    lv_obj_t *hourly_row;
    lv_obj_t *daily_list;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_root_state_t;

typedef struct weather_alerts_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *list;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_alerts_state_t;

typedef struct weather_detail_state
{
    app_ui_page_t page;
    lv_obj_t *content_label;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_detail_state_t;

_Static_assert(sizeof(weather_root_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_alerts_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather alerts state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_detail_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather detail state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_service_event_t) <= 256U,
               "Weather event exceeds the Event Bus payload limit");

static atomic_uint_fast64_t s_selected_alert_key;

static void _weather_release_snapshot(
    const weather_service_snapshot_t **snapshot)
{
    if (*snapshot != NULL)
    {
        weather_service_snapshot_release(*snapshot);
        *snapshot = NULL;
    }
}

static uint32_t _weather_image_id(uint16_t code, bool small)
{
    uint32_t offset = APP_IMAGE_WEATHER_UNKNOWN_MAIN -
                      APP_IMAGE_WEATHER_CLEAR_DAY_MAIN;
    if (code == 100U)
    {
        offset = 0U;
    }
    else if (code == 150U)
    {
        offset = 1U;
    }
    else if (code >= 101U && code <= 103U)
    {
        offset = 2U;
    }
    else if (code >= 151U && code <= 153U)
    {
        offset = 3U;
    }
    else if (code == 104U)
    {
        offset = 5U;
    }
    else if (code == 305U || code == 309U)
    {
        offset = 6U;
    }
    else if ((code >= 300U && code <= 301U) || code == 306U ||
             code == 313U || (code >= 314U && code <= 316U) || code == 350U ||
             code == 351U || code == 399U)
    {
        offset = 7U;
    }
    else if (code == 307U || code == 308U ||
             (code >= 310U && code <= 312U) ||
             (code >= 317U && code <= 318U))
    {
        offset = 8U;
    }
    else if (code == 304U)
    {
        offset = 10U;
    }
    else if (code >= 302U && code <= 303U)
    {
        offset = 9U;
    }
    else if (code == 404U || code == 405U || code == 406U)
    {
        offset = 14U;
    }
    else if (code == 400U || code == 407U)
    {
        offset = 12U;
    }
    else if (code >= 401U && code <= 403U)
    {
        offset = 13U;
    }
    else if (code == 500U || code == 501U)
    {
        offset = 15U;
    }
    else if (code >= 502U && code <= 515U)
    {
        offset = 16U;
    }
    else if (code == 900U)
    {
        offset = 17U;
    }
    else if (code == 901U)
    {
        offset = 18U;
    }
    return (small ? APP_IMAGE_WEATHER_CLEAR_DAY_SMALL :
            APP_IMAGE_WEATHER_CLEAR_DAY_MAIN) + offset;
}

static bool _weather_set_image(lv_obj_t *image, uint32_t semantic_id)
{
    const lv_image_dsc_t *descriptor = NULL;
    if (app_manager_get_image(semantic_id, &descriptor) != ESP_OK)
    {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    lv_image_set_src(image, descriptor);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void _weather_format_time(const weather_service_time_t *source,
                                 const char *format, char *output,
                                 size_t output_size)
{
    time_t adjusted = (time_t)(source->epoch_seconds +
                               (int64_t)source->offset_minutes * 60);
    struct tm value;
    if (source->epoch_seconds <= 0 || gmtime_r(&adjusted, &value) == NULL ||
            strftime(output, output_size, format, &value) == 0U)
    {
        if (output_size > 0U)
        {
            output[0] = '\0';
        }
    }
}

static lv_obj_t *_weather_create_surface(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *surface = lv_obj_create(parent);
    lv_obj_remove_style_all(surface);
    lv_obj_set_width(surface, LV_PCT(100));
    lv_obj_set_height(surface, height);
    lv_obj_set_style_bg_color(surface,
                              lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(surface, 6, 0);
    lv_obj_set_style_pad_all(surface, 12, 0);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    return surface;
}

static void _weather_add_small_icon(lv_obj_t *parent, uint16_t code)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_obj_set_size(image, 40, 40);
    if (!_weather_set_image(image, _weather_image_id(code, true)))
    {
        lv_obj_delete(image);
        lv_obj_t *fallback = lv_label_create(parent);
        lv_label_set_text(fallback, LV_SYMBOL_IMAGE);
        lv_obj_set_width(fallback, 40);
        lv_obj_set_style_text_align(fallback, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(fallback,
                                    lv_color_hex(WEATHER_COLOR_RAIN), 0);
    }
}

static const char *_weather_state_text(weather_service_state_t state)
{
    switch (state)
    {
    case WEATHER_SERVICE_STATE_UNCONFIGURED:
        return "服务未配置";
    case WEATHER_SERVICE_STATE_WAITING_NETWORK:
        return "等待网络";
    case WEATHER_SERVICE_STATE_LOCATING:
        return "正在定位";
    case WEATHER_SERVICE_STATE_UPDATING:
        return "正在更新";
    case WEATHER_SERVICE_STATE_READY:
        return "已更新";
    case WEATHER_SERVICE_STATE_DEGRADED:
        return "使用缓存数据";
    case WEATHER_SERVICE_STATE_AUTH_ERROR:
        return "服务认证失败";
    case WEATHER_SERVICE_STATE_RATE_LIMITED:
        return "请求受限";
    case WEATHER_SERVICE_STATE_SUSPENDED:
        return "服务已暂停";
    case WEATHER_SERVICE_STATE_ERROR:
    default:
        return "天气暂不可用";
    }
}

static app_ui_status_t _weather_state_color(weather_service_state_t state)
{
    if (state == WEATHER_SERVICE_STATE_READY)
    {
        return APP_UI_STATUS_SUCCESS;
    }
    if (state == WEATHER_SERVICE_STATE_LOCATING ||
            state == WEATHER_SERVICE_STATE_UPDATING ||
            state == WEATHER_SERVICE_STATE_WAITING_NETWORK)
    {
        return APP_UI_STATUS_ACCENT;
    }
    if (state == WEATHER_SERVICE_STATE_DEGRADED ||
            state == WEATHER_SERVICE_STATE_RATE_LIMITED ||
            state == WEATHER_SERVICE_STATE_SUSPENDED)
    {
        return APP_UI_STATUS_WARNING;
    }
    return APP_UI_STATUS_ERROR;
}

static void _weather_root_render_metrics(weather_root_state_t *state)
{
    lv_obj_clean(state->metrics);
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        app_ui_add_body_label(state->metrics, "暂无天气指标");
        return;
    }
    const weather_service_current_t *current = &state->snapshot->current;
    char text[48];
    (void)snprintf(text, sizeof(text), "%u%%",
                   (unsigned)current->humidity_percent);
    app_ui_add_value_row(state->metrics, "湿度", text, NULL);
    (void)snprintf(text, sizeof(text), "%.1f km/h · %s",
                   current->wind_speed_tenths_kmh / 10.0,
                   current->wind_direction);
    app_ui_add_value_row(state->metrics, "风", text, NULL);
    (void)snprintf(text, sizeof(text), "%u hPa · %.1f km",
                   (unsigned)current->pressure_hpa,
                   current->visibility_tenths_km / 10.0);
    app_ui_add_value_row(state->metrics, "气压 / 能见度", text, NULL);
}

static void _weather_root_render_hourly(weather_root_state_t *state)
{
    lv_obj_clean(state->hourly_row);
    if (state->snapshot == NULL || !state->snapshot->hourly.meta.available)
    {
        app_ui_add_body_label(state->hourly_row, "暂无逐小时预报");
        return;
    }
    const weather_service_hourly_t *hourly = &state->snapshot->hourly;
    for (uint8_t index = 0U; index < hourly->count; ++index)
    {
        const weather_service_hour_t *hour = &hourly->items[index];
        lv_obj_t *item = _weather_create_surface(state->hourly_row, 118);
        lv_obj_set_width(item, 88);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        char text[24];
        _weather_format_time(&hour->forecast_at, "%H:%M", text,
                             sizeof(text));
        lv_obj_t *time_label = lv_label_create(item);
        lv_label_set_text(time_label, text[0] != '\0' ? text : "--:--");
        lv_obj_set_style_text_color(time_label,
                                    lv_color_hex(WEATHER_COLOR_MUTED), 0);
        _weather_add_small_icon(item, hour->condition_code);
        (void)snprintf(text, sizeof(text), "%.0f°",
                       hour->temperature_tenths_c / 10.0);
        lv_obj_t *temperature = lv_label_create(item);
        lv_label_set_text(temperature, text);
        lv_obj_set_style_text_color(temperature,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(temperature,
                                   app_ui_font(APP_THEME_FONT_BODY), 0);
    }
}

static void _weather_root_render_daily(weather_root_state_t *state)
{
    lv_obj_clean(state->daily_list);
    if (state->snapshot == NULL || !state->snapshot->daily.meta.available)
    {
        app_ui_add_body_label(state->daily_list, "暂无 7 日预报");
        return;
    }
    const weather_service_daily_t *daily = &state->snapshot->daily;
    for (uint8_t index = 0U; index < daily->count; ++index)
    {
        const weather_service_day_t *day = &daily->items[index];
        lv_obj_t *row = _weather_create_surface(state->daily_list, 64);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_t *date = lv_label_create(row);
        lv_label_set_text(date, day->date + 5);
        lv_obj_set_width(date, 56);
        lv_obj_set_style_text_color(date, lv_color_hex(WEATHER_COLOR_MUTED), 0);
        _weather_add_small_icon(row, day->day_condition_code);
        lv_obj_t *condition = lv_label_create(row);
        lv_label_set_text(condition, day->day_condition_text);
        lv_obj_set_flex_grow(condition, 1);
        lv_label_set_long_mode(condition, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(condition,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
        char text[32];
        (void)snprintf(text, sizeof(text), "%.0f° / %.0f°",
                       day->maximum_temperature_tenths_c / 10.0,
                       day->minimum_temperature_tenths_c / 10.0);
        lv_obj_t *range = lv_label_create(row);
        lv_label_set_text(range, text);
        lv_obj_set_style_text_color(range,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
    }
}

static void _weather_root_render(weather_root_state_t *state)
{
    weather_service_status_snapshot_t status = {0};
    bool have_status = weather_service_get_status(&status) == ESP_OK;
    app_ui_set_status_text(state->status_label,
                           have_status ? _weather_state_text(status.state) :
                           "天气服务不可用",
                           have_status ? _weather_state_color(status.state) :
                           APP_UI_STATUS_ERROR);
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        lv_label_set_text(state->city_label, "等待位置与天气数据");
        lv_label_set_text(state->temperature_label, "--°");
        lv_label_set_text(state->condition_label, "暂无实时天气");
        lv_obj_add_flag(state->main_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);
        _weather_root_render_metrics(state);
        _weather_root_render_hourly(state);
        _weather_root_render_daily(state);
        return;
    }

    const weather_service_snapshot_t *snapshot = state->snapshot;
    lv_label_set_text(state->city_label,
                      snapshot->location.city[0] != '\0' ?
                      snapshot->location.city : "设备提供位置");
    char text[96];
    (void)snprintf(text, sizeof(text), "%.0f°",
                   snapshot->current.temperature_tenths_c / 10.0);
    lv_label_set_text(state->temperature_label, text);
    (void)snprintf(text, sizeof(text), "%s · 体感 %.0f°",
                   snapshot->current.condition_text,
                   snapshot->current.feels_like_tenths_c / 10.0);
    lv_label_set_text(state->condition_label, text);
    bool image_ready = _weather_set_image(
                           state->main_image,
                           _weather_image_id(snapshot->current.condition_code,
                               false));
    if (image_ready)
    {
        lv_obj_add_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_remove_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
    }
    if (snapshot->alerts.meta.available && snapshot->alerts.count > 0U)
    {
        (void)snprintf(text, sizeof(text), "%.64s  ·  共 %u 条",
                       snapshot->alerts.items[0].title,
                       (unsigned)snapshot->alerts.count);
        lv_label_set_text(state->alert_label, text);
        lv_obj_remove_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (snapshot->current.meta.expired)
    {
        app_ui_set_status_text(state->status_label, "数据已过期",
                               APP_UI_STATUS_WARNING);
    }
    else if (snapshot->location.reused)
    {
        app_ui_set_status_text(state->status_label, "沿用上次位置",
                               APP_UI_STATUS_WARNING);
    }
    _weather_root_render_metrics(state);
    _weather_root_render_hourly(state);
    _weather_root_render_daily(state);
}

static void _weather_root_refresh_snapshot(weather_root_state_t *state)
{
    _weather_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_root_render(state);
}

static void _weather_refresh_event(lv_event_t *event)
{
    weather_root_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    esp_err_t result = weather_service_request_refresh();
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "已请求更新",
                               APP_UI_STATUS_ACCENT);
    }
    else if (result == ESP_ERR_TIMEOUT)
    {
        app_ui_set_status_text(state->status_label, "刷新间隔至少 60 秒",
                               APP_UI_STATUS_WARNING);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "刷新请求失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _weather_open_alerts(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_WEATHER, WEATHER_PAGE_ALERTS);
    }
}

static void _weather_root_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                const void *payload, size_t payload_size,
                                void *user_data)
{
    weather_root_state_t *state = user_data;
    if (msg_id == WEATHER_SERVICE_MSG &&
            sub_type == WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT &&
            payload != NULL && payload_size == sizeof(weather_service_event_t) &&
            state->page.root != NULL)
    {
        _weather_root_refresh_snapshot(state);
    }
}

static void _weather_root_build(weather_root_state_t *state)
{
    app_ui_page_create(&state->page, "天气", true);
    lv_obj_t *refresh = lv_button_create(state->page.header);
    lv_obj_set_size(refresh, 44, 44);
    lv_obj_set_style_radius(refresh, 6, 0);
    lv_obj_set_style_bg_color(refresh,
                              lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_shadow_width(refresh, 0, 0);
    lv_obj_add_event_cb(refresh, _weather_refresh_event, LV_EVENT_CLICKED,
                        state);
    lv_obj_t *refresh_icon = lv_label_create(refresh);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_icon);

    lv_obj_t *headline = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(headline);
    lv_obj_set_width(headline, LV_PCT(100));
    lv_obj_set_height(headline, 160);
    lv_obj_set_flex_flow(headline, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headline, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(headline, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *summary = lv_obj_create(headline);
    lv_obj_remove_style_all(summary);
    lv_obj_set_width(summary, 190);
    lv_obj_set_height(summary, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(summary, 3, 0);
    lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    state->city_label = lv_label_create(summary);
    lv_label_set_text(state->city_label, "等待位置与天气数据");
    lv_label_set_long_mode(state->city_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state->city_label, LV_PCT(100));
    lv_obj_set_style_text_color(state->city_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->city_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    state->temperature_label = lv_label_create(summary);
    lv_label_set_text(state->temperature_label, "--°");
    lv_obj_set_style_text_color(state->temperature_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->temperature_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    state->condition_label = lv_label_create(summary);
    lv_label_set_text(state->condition_label, "暂无实时天气");
    lv_obj_set_width(state->condition_label, LV_PCT(100));
    lv_label_set_long_mode(state->condition_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->condition_label,
                                lv_color_hex(WEATHER_COLOR_MUTED), 0);
    state->status_label = lv_label_create(summary);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);

    lv_obj_t *visual = lv_obj_create(headline);
    lv_obj_remove_style_all(visual);
    lv_obj_set_size(visual, 112, 112);
    lv_obj_remove_flag(visual, LV_OBJ_FLAG_SCROLLABLE);
    state->main_image = lv_image_create(visual);
    lv_obj_set_size(state->main_image, 112, 112);
    state->image_fallback = lv_label_create(visual);
    lv_label_set_text(state->image_fallback, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(state->image_fallback,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    lv_obj_set_style_text_color(state->image_fallback,
                                lv_color_hex(WEATHER_COLOR_SUN), 0);
    lv_obj_center(state->image_fallback);

    state->alert_button = lv_button_create(state->page.content);
    lv_obj_set_width(state->alert_button, LV_PCT(100));
    lv_obj_set_height(state->alert_button, 58);
    lv_obj_set_style_radius(state->alert_button, 6, 0);
    lv_obj_set_style_bg_color(state->alert_button,
                              lv_color_hex(0x3B2021), 0);
    lv_obj_set_style_shadow_width(state->alert_button, 0, 0);
    lv_obj_add_event_cb(state->alert_button, _weather_open_alerts,
                        LV_EVENT_CLICKED, NULL);
    state->alert_label = lv_label_create(state->alert_button);
    lv_label_set_text(state->alert_label, "气象预警");
    lv_obj_set_width(state->alert_label, LV_PCT(100));
    lv_label_set_long_mode(state->alert_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state->alert_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_obj_center(state->alert_label);
    lv_obj_add_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);

    app_ui_add_section(state->page.content, "未来 24 小时");
    state->hourly_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->hourly_row);
    lv_obj_set_width(state->hourly_row, LV_PCT(100));
    lv_obj_set_height(state->hourly_row, 124);
    lv_obj_set_flex_flow(state->hourly_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(state->hourly_row, 8, 0);
    lv_obj_set_scroll_dir(state->hourly_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(state->hourly_row, LV_SCROLLBAR_MODE_OFF);

    app_ui_add_section(state->page.content, "天气指标");
    state->metrics = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->metrics);
    lv_obj_set_width(state->metrics, LV_PCT(100));
    lv_obj_set_height(state->metrics, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->metrics, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->metrics, 8, 0);
    lv_obj_remove_flag(state->metrics, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_section(state->page.content, "未来 7 天");
    state->daily_list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->daily_list);
    lv_obj_set_width(state->daily_list, LV_PCT(100));
    lv_obj_set_height(state->daily_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->daily_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->daily_list, 8, 0);
    lv_obj_remove_flag(state->daily_list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *attribution = app_ui_add_body_label(
                                state->page.content,
                                "天气数据：和风天气/QWeather · 定位：ipapi.is");
    lv_obj_set_style_text_font(attribution,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    _weather_root_render(state);
}

static esp_err_t _weather_unsubscribe(event_bus_sub_handle_t *subscription)
{
    if (*subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return ESP_OK;
    }
    esp_err_t result = event_bus_unsubscribe(*subscription);
    if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
    {
        *subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        return ESP_OK;
    }
    return result;
}

static void _weather_root_resume(weather_root_state_t *state)
{
    _weather_root_refresh_snapshot(state);
    if (state->subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_subscribe(
                               WEATHER_SERVICE_MSG,
                               WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                               _weather_root_event, state,
                               EVENT_BUS_DISPATCH_UI, &state->subscription);
        if (result != ESP_OK)
        {
            state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            LOG_W("weather subscription failed: %s", esp_err_to_name(result));
        }
    }
}

static esp_err_t _weather_root_pause(weather_root_state_t *state)
{
    esp_err_t result = _weather_unsubscribe(&state->subscription);
    _weather_release_snapshot(&state->snapshot);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
    }
    return result;
}

static void _weather_root_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    weather_root_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _weather_root_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _weather_root_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_weather_root_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        (void)_weather_root_pause(state);
        break;
    default:
        break;
    }
}

static void _weather_alert_open(lv_event_t *event)
{
    weather_alerts_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->snapshot == NULL)
    {
        return;
    }
    uint32_t index = (uint32_t)(uintptr_t)lv_obj_get_user_data(
                         lv_event_get_target_obj(event));
    if (index == 0U || index > state->snapshot->alerts.count)
    {
        return;
    }
    atomic_store_explicit(&s_selected_alert_key,
                          state->snapshot->alerts.items[index - 1U].key,
                          memory_order_release);
    app_ui_request_open_page(APP_MANAGER_ID_WEATHER, WEATHER_PAGE_DETAIL);
}

static void _weather_alerts_render(weather_alerts_state_t *state)
{
    lv_obj_clean(state->list);
    if (state->snapshot == NULL || !state->snapshot->alerts.meta.available)
    {
        app_ui_set_status_text(state->status_label, "预警数据不可用",
                               APP_UI_STATUS_WARNING);
        return;
    }
    if (state->snapshot->alerts.count == 0U)
    {
        app_ui_set_status_text(state->status_label, "当前没有生效预警",
                               APP_UI_STATUS_SUCCESS);
        return;
    }
    char text[48];
    (void)snprintf(text, sizeof(text), "共 %u 条气象预警",
                   (unsigned)state->snapshot->alerts.count);
    app_ui_set_status_text(state->status_label, text, APP_UI_STATUS_WARNING);
    for (uint8_t index = 0U; index < state->snapshot->alerts.count; ++index)
    {
        const weather_service_alert_t *alert =
            &state->snapshot->alerts.items[index];
        lv_obj_t *button = app_ui_add_action(
                               state->list, LV_SYMBOL_WARNING, alert->title,
                               alert->type_name, _weather_alert_open, state);
        lv_obj_set_user_data(button, (void *)(uintptr_t)(index + 1U));
    }
}

static void _weather_alerts_refresh(weather_alerts_state_t *state)
{
    _weather_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_alerts_render(state);
}

static void _weather_alerts_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                  const void *payload, size_t payload_size,
                                  void *user_data)
{
    weather_alerts_state_t *state = user_data;
    if (msg_id == WEATHER_SERVICE_MSG &&
            sub_type == WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT &&
            payload != NULL && payload_size == sizeof(weather_service_event_t) &&
            state->page.root != NULL)
    {
        _weather_alerts_refresh(state);
    }
}

static void _weather_alerts_handler(app_manager_msg_type_t message,
                                    void *param)
{
    (void)param;
    weather_alerts_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            app_ui_page_create(&state->page, "气象预警", true);
            state->status_label = lv_label_create(state->page.content);
            lv_obj_set_width(state->status_label, LV_PCT(100));
            state->list = lv_obj_create(state->page.content);
            lv_obj_remove_style_all(state->list);
            lv_obj_set_width(state->list, LV_PCT(100));
            lv_obj_set_height(state->list, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(state->list, 8, 0);
            lv_obj_remove_flag(state->list, LV_OBJ_FLAG_SCROLLABLE);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _weather_alerts_refresh(state);
        if (state->subscription == EVENT_BUS_SUB_HANDLE_INVALID)
        {
            (void)event_bus_subscribe(
                WEATHER_SERVICE_MSG, WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                _weather_alerts_event, state, EVENT_BUS_DISPATCH_UI,
                &state->subscription);
        }
        break;
    case APP_MANAGER_MSG_ONPAUSE:
    case APP_MANAGER_MSG_ONSTOP:
    {
        esp_err_t result = _weather_unsubscribe(&state->subscription);
        _weather_release_snapshot(&state->snapshot);
        if (result != ESP_OK)
        {
            app_manager_this_page_report_cleanup_error(result);
        }
        break;
    }
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    default:
        break;
    }
}

static const weather_service_alert_t *_weather_find_selected_alert(
    const weather_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return NULL;
    }
    uint64_t key = atomic_load_explicit(&s_selected_alert_key,
                                        memory_order_acquire);
    for (uint8_t index = 0U; index < snapshot->alerts.count; ++index)
    {
        if (snapshot->alerts.items[index].key == key)
        {
            return &snapshot->alerts.items[index];
        }
    }
    return NULL;
}

static void _weather_detail_render(weather_detail_state_t *state)
{
    const weather_service_alert_t *alert =
        _weather_find_selected_alert(state->snapshot);
    if (alert == NULL)
    {
        lv_label_set_text(state->content_label, "该预警已失效或被撤销。");
        return;
    }
    char start[32];
    char end[32];
    _weather_format_time(&alert->starts_at, "%m-%d %H:%M", start,
                         sizeof(start));
    _weather_format_time(&alert->ends_at, "%m-%d %H:%M", end, sizeof(end));
    char *text = heap_caps_malloc(WEATHER_DETAIL_TEXT_BYTES,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (text == NULL)
    {
        lv_label_set_text(state->content_label, "预警详情内存不足。");
        return;
    }
    (void)snprintf(text, WEATHER_DETAIL_TEXT_BYTES,
                   "%s\n\n类型：%s\n级别：%s\n状态：%s\n时段：%s - %s\n\n%s%s%s%s",
                   alert->title, alert->type_name, alert->severity,
                   alert->status, start[0] != '\0' ? start : "未提供",
                   end[0] != '\0' ? end : "未提供", alert->description,
                   alert->instruction[0] != '\0' ? "\n\n建议：" : "",
                   alert->instruction,
                   alert->content_truncated ? "\n\n内容已截断" : "");
    lv_label_set_text(state->content_label, text);
    heap_caps_free(text);
}

static void _weather_detail_refresh(weather_detail_state_t *state)
{
    _weather_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_detail_render(state);
}

static void _weather_detail_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                  const void *payload, size_t payload_size,
                                  void *user_data)
{
    weather_detail_state_t *state = user_data;
    if (msg_id == WEATHER_SERVICE_MSG &&
            sub_type == WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT &&
            payload != NULL && payload_size == sizeof(weather_service_event_t) &&
            state->page.root != NULL)
    {
        _weather_detail_refresh(state);
    }
}

static void _weather_detail_handler(app_manager_msg_type_t message,
                                    void *param)
{
    (void)param;
    weather_detail_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            app_ui_page_create(&state->page, "预警详情", true);
            state->content_label = app_ui_add_body_label(state->page.content,
                                   "读取预警详情");
            lv_obj_set_style_text_color(state->content_label,
                                        lv_color_hex(WEATHER_COLOR_TEXT), 0);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _weather_detail_refresh(state);
        if (state->subscription == EVENT_BUS_SUB_HANDLE_INVALID)
        {
            (void)event_bus_subscribe(
                WEATHER_SERVICE_MSG, WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                _weather_detail_event, state, EVENT_BUS_DISPATCH_UI,
                &state->subscription);
        }
        break;
    case APP_MANAGER_MSG_ONPAUSE:
    case APP_MANAGER_MSG_ONSTOP:
    {
        esp_err_t result = _weather_unsubscribe(&state->subscription);
        _weather_release_snapshot(&state->snapshot);
        if (result != ESP_OK)
        {
            app_manager_this_page_report_cleanup_error(result);
        }
        break;
    }
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    default:
        break;
    }
}

static const app_manager_page_definition_t s_weather_root_definition =
{
    .handler = _weather_root_handler,
    .memory_size = sizeof(weather_root_state_t),
};

static const app_manager_page_definition_t s_weather_alerts_definition =
{
    .handler = _weather_alerts_handler,
    .memory_size = sizeof(weather_alerts_state_t),
};

static const app_manager_page_definition_t s_weather_detail_definition =
{
    .handler = _weather_detail_handler,
    .memory_size = sizeof(weather_detail_state_t),
};

static const app_manager_page_route_t s_weather_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_weather_root_definition,
        .user_data = NULL,
    },
    {
        .page_id = WEATHER_PAGE_ALERTS,
        .definition = &s_weather_alerts_definition,
        .user_data = NULL,
    },
    {
        .page_id = WEATHER_PAGE_DETAIL,
        .definition = &s_weather_detail_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT(weather, NULL, APP_MANAGER_ID_WEATHER, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_weather_routes);
