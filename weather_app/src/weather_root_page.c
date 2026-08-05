#define DBG_TAG "weather_root"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "weather_app_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct weather_root_state
{
    app_ui_page_t page;
    lv_obj_t *city_label;
    lv_obj_t *status_label;
    lv_obj_t *temperature_label;
    lv_obj_t *condition_label;
    lv_obj_t *range_label;
    lv_obj_t *main_image;
    lv_obj_t *image_fallback;
    lv_obj_t *alert_button;
    lv_obj_t *alert_label;
    lv_obj_t *metrics;
    lv_obj_t *hourly_row;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_root_state_t;

_Static_assert(sizeof(weather_root_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_service_event_t) <= 256U,
               "Weather event exceeds the Event Bus payload limit");

static lv_obj_t *_weather_root_metric(lv_obj_t *parent, const char *name,
                                      const char *value)
{
    lv_obj_t *cell = weather_ui_surface(parent, 64);
    lv_obj_set_width(cell, LV_PCT(48));
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_t *name_label = weather_ui_text_label(
                               cell, name, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(name_label, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    lv_obj_t *value_label = weather_ui_text_label(
                                cell, value, APP_THEME_FONT_SMALL);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(value_label, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    return cell;
}

static void _weather_root_metric_pair(lv_obj_t *parent,
                                      const char *left_name,
                                      const char *left_value,
                                      const char *right_name,
                                      const char *right_value)
{
    lv_obj_t *row = weather_ui_container(parent, 64, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    (void)_weather_root_metric(row, left_name, left_value);
    (void)_weather_root_metric(row, right_name, right_value);
}

static const weather_service_day_t *_weather_root_today(
    const weather_service_snapshot_t *snapshot)
{
    if (!snapshot->daily.meta.available || snapshot->daily.count == 0U)
    {
        return NULL;
    }
    char date[16];
    weather_ui_format_time(&snapshot->current.observed_at, "%Y-%m-%d", date,
                           sizeof(date));
    if (date[0] == '\0')
    {
        return NULL;
    }
    for (uint8_t index = 0U; index < snapshot->daily.count; ++index)
    {
        if (strcmp(snapshot->daily.items[index].date, date) == 0)
        {
            return &snapshot->daily.items[index];
        }
    }
    return NULL;
}

static void _weather_root_render_metrics(weather_root_state_t *state)
{
    lv_obj_clean(state->metrics);
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        lv_obj_t *empty = weather_ui_text_label(
                              state->metrics, "暂无实况指标",
                              APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        return;
    }
    const weather_service_current_t *current = &state->snapshot->current;
    char text[48];
    (void)snprintf(text, sizeof(text), "%u%%",
                   (unsigned)current->humidity_percent);
    char left[48];
    (void)snprintf(left, sizeof(left), "%s", text);
    (void)snprintf(text, sizeof(text), "%.1f mm",
                   current->precipitation_tenths_mm / 10.0);
    _weather_root_metric_pair(state->metrics, "湿度", left, "降水", text);
    (void)snprintf(text, sizeof(text), "%.16s %.0f km/h",
                   current->wind_direction,
                   current->wind_speed_tenths_kmh / 10.0);
    (void)snprintf(left, sizeof(left), "%s", text);
    (void)snprintf(text, sizeof(text), "%.1f km",
                   current->visibility_tenths_km / 10.0);
    _weather_root_metric_pair(state->metrics, "风", left, "能见度", text);
}

static void _weather_root_render_hourly(weather_root_state_t *state)
{
    lv_obj_clean(state->hourly_row);
    if (state->snapshot == NULL || !state->snapshot->hourly.meta.available ||
            state->snapshot->hourly.count == 0U)
    {
        lv_obj_t *empty = weather_ui_text_label(
                              state->hourly_row, "暂无逐小时预报",
                              APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        return;
    }
    uint8_t count = state->snapshot->hourly.count;
    if (count > 4U)
    {
        count = 4U;
    }
    for (uint8_t index = 0U; index < count; ++index)
    {
        const weather_service_hour_t *hour =
            &state->snapshot->hourly.items[index];
        lv_obj_t *item = weather_ui_container(state->hourly_row, 72,
                                              LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(item, LV_PCT(24));
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        char text[24];
        weather_ui_format_time(&hour->forecast_at, "%H:%M", text,
                               sizeof(text));
        lv_obj_t *time = weather_ui_text_label(
                             item, text[0] != '\0' ? text : "--:--",
                             APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(time, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        (void)weather_ui_small_icon(item, hour->condition_code);
        (void)snprintf(text, sizeof(text), "%.0f°",
                       hour->temperature_tenths_c / 10.0);
        lv_obj_t *temperature = weather_ui_text_label(
                                    item, text, APP_THEME_FONT_SMALL);
        lv_obj_set_style_text_color(temperature,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
    }
}

static void _weather_root_render_status(weather_root_state_t *state)
{
    weather_service_status_snapshot_t status = {0};
    if (weather_service_get_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "天气服务不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    const char *text = weather_ui_state_text(status.state);
    app_ui_status_t color = weather_ui_state_color(status.state);
    if (state->snapshot != NULL && state->snapshot->current.meta.expired)
    {
        text = "实况已过期";
        color = APP_UI_STATUS_WARNING;
    }
    else if (state->snapshot != NULL && state->snapshot->current.meta.stale)
    {
        text = "显示缓存实况";
        color = APP_UI_STATUS_WARNING;
    }
    else if (state->snapshot != NULL && state->snapshot->location.reused)
    {
        text = "沿用上次位置";
        color = APP_UI_STATUS_WARNING;
    }
    if (status.state == WEATHER_SERVICE_STATE_READY &&
            state->snapshot != NULL &&
            state->snapshot->current.meta.available &&
            !state->snapshot->current.meta.stale &&
            !state->snapshot->current.meta.expired &&
            !state->snapshot->location.reused)
    {
        char updated[32];
        char status_text[48];
        weather_ui_format_dataset_time(&state->snapshot->current.meta,
                                       updated, sizeof(updated));
        if (updated[0] != '\0')
        {
            (void)snprintf(status_text, sizeof(status_text), "%s 更新",
                           updated);
            app_ui_set_status_text(state->status_label, status_text, color);
            return;
        }
    }
    app_ui_set_status_text(state->status_label, text, color);
}

static void _weather_root_render(weather_root_state_t *state)
{
    _weather_root_render_status(state);
    if (state->snapshot != NULL && state->snapshot->location.available &&
            state->snapshot->location.city[0] != '\0')
    {
        lv_label_set_text(state->city_label, state->snapshot->location.city);
    }
    else
    {
        lv_label_set_text(state->city_label, "天气");
    }
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        lv_label_set_text(state->temperature_label, "--°");
        lv_label_set_text(state->condition_label, "暂无实时天气");
        lv_label_set_text(state->range_label, "体感 --°  ·  --° / --°");
        lv_obj_add_flag(state->main_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        const weather_service_snapshot_t *snapshot = state->snapshot;
        char text[128];
        (void)snprintf(text, sizeof(text), "%.0f°",
                       snapshot->current.temperature_tenths_c / 10.0);
        lv_label_set_text(state->temperature_label, text);
        lv_label_set_text(state->condition_label,
                          snapshot->current.condition_text[0] != '\0' ?
                          snapshot->current.condition_text : "天气未知");
        const weather_service_day_t *today = _weather_root_today(snapshot);
        if (today != NULL)
        {
            (void)snprintf(text, sizeof(text),
                           "体感 %.0f°  ·  %.0f° / %.0f°",
                           snapshot->current.feels_like_tenths_c / 10.0,
                           today->maximum_temperature_tenths_c / 10.0,
                           today->minimum_temperature_tenths_c / 10.0);
        }
        else
        {
            (void)snprintf(text, sizeof(text), "体感 %.0f°  ·  --° / --°",
                           snapshot->current.feels_like_tenths_c / 10.0);
        }
        lv_label_set_text(state->range_label, text);
        if (weather_ui_set_image(state->main_image,
                                 snapshot->current.condition_code, false))
        {
            lv_obj_add_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_remove_flag(state->image_fallback, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (state->snapshot != NULL && state->snapshot->alerts.meta.available &&
            state->snapshot->alerts.count > 0U)
    {
        char text[128];
        (void)snprintf(text, sizeof(text), "%.72s · %.15s · 共 %u 条%s",
                       state->snapshot->alerts.items[0].title,
                       state->snapshot->alerts.items[0].severity,
                       (unsigned)state->snapshot->alerts.count,
                       state->snapshot->alerts.meta.expired ? " · 已过期" :
                       (state->snapshot->alerts.meta.stale ? " · 缓存" : ""));
        lv_label_set_text(state->alert_label, text);
        lv_obj_remove_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(state->alert_button, LV_OBJ_FLAG_HIDDEN);
    }
    _weather_root_render_metrics(state);
    _weather_root_render_hourly(state);
}

static void _weather_root_refresh_snapshot(weather_root_state_t *state)
{
    weather_ui_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_root_render(state);
}

static void _weather_root_refresh_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    weather_root_state_t *state = lv_event_get_user_data(event);
    esp_err_t result = weather_service_request_refresh();
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "已请求更新",
                               APP_UI_STATUS_ACCENT);
    }
    else if (result == ESP_ERR_TIMEOUT)
    {
        weather_service_status_snapshot_t status = {0};
        char text[48];
        if (weather_service_get_status(&status) == ESP_OK &&
                status.retry_after_seconds > 0U)
        {
            (void)snprintf(text, sizeof(text), "%u 秒后可刷新",
                           (unsigned)status.retry_after_seconds);
        }
        else
        {
            (void)snprintf(text, sizeof(text), "请稍后刷新");
        }
        app_ui_set_status_text(state->status_label, text,
                               APP_UI_STATUS_WARNING);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "刷新请求失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _weather_root_open_forecast(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_WEATHER,
                                 WEATHER_PAGE_FORECAST);
    }
}

static void _weather_root_open_alerts(lv_event_t *event)
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
    if (weather_ui_is_snapshot_event(msg_id, sub_type, payload,
                                     payload_size) &&
            state->page.root != NULL)
    {
        _weather_root_refresh_snapshot(state);
    }
}

static lv_obj_t *_weather_root_header_button(weather_root_state_t *state)
{
    lv_obj_t *button = lv_button_create(state->page.header);
    lv_obj_set_size(button, 44, 44);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, _weather_root_refresh_event, LV_EVENT_CLICKED,
                        state);
    lv_obj_t *icon = weather_ui_symbol_label(button, LV_SYMBOL_REFRESH);
    lv_obj_center(icon);
    return button;
}

static void _weather_root_build(weather_root_state_t *state)
{
    app_ui_page_create(&state->page, "天气", true);
    state->city_label = state->page.title;
    (void)_weather_root_header_button(state);

    lv_obj_t *hero = weather_ui_container(state->page.content, 112,
                                          LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *summary = weather_ui_container(hero, LV_SIZE_CONTENT,
                        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(summary, 190);
    lv_obj_set_style_pad_row(summary, 1, 0);
    state->temperature_label = weather_ui_text_label(
                                   summary, "--°", APP_THEME_FONT_TITLE);
    lv_obj_set_style_text_color(state->temperature_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    state->condition_label = weather_ui_text_label(
                                 summary, "暂无实时天气",
                                 APP_THEME_FONT_HEAD);
    lv_obj_set_width(state->condition_label, LV_PCT(100));
    lv_label_set_long_mode(state->condition_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state->condition_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    state->range_label = weather_ui_text_label(
                             summary, "体感 --°  ·  --° / --°",
                             APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(state->range_label,
                                lv_color_hex(WEATHER_COLOR_MUTED), 0);
    state->status_label = weather_ui_text_label(
                              summary, "读取天气", APP_THEME_FONT_BODY);

    lv_obj_t *visual = weather_ui_container(hero, 112, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(visual, 112);
    state->main_image = lv_image_create(visual);
    lv_obj_set_size(state->main_image, 112, 112);
    state->image_fallback = weather_ui_symbol_label(visual, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(state->image_fallback,
                                lv_color_hex(WEATHER_COLOR_SUN), 0);
    lv_obj_center(state->image_fallback);

    state->alert_button = lv_button_create(state->page.content);
    lv_obj_set_size(state->alert_button, LV_PCT(100), 46);
    lv_obj_set_style_radius(state->alert_button, 6, 0);
    lv_obj_set_style_bg_color(state->alert_button,
                              lv_color_hex(WEATHER_COLOR_WARNING_BG), 0);
    lv_obj_set_style_shadow_width(state->alert_button, 0, 0);
    lv_obj_add_event_cb(state->alert_button, _weather_root_open_alerts,
                        LV_EVENT_CLICKED, NULL);
    state->alert_label = weather_ui_text_label(
                             state->alert_button, "气象预警",
                             APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->alert_label, LV_PCT(100));
    lv_label_set_long_mode(state->alert_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state->alert_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_obj_center(state->alert_label);

    state->metrics = weather_ui_container(state->page.content, 136,
                                          LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->metrics, 8, 0);
    lv_obj_set_style_pad_column(state->metrics, 8, 0);
    lv_obj_set_flex_align(state->metrics, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    state->hourly_row = weather_ui_container(state->page.content, 72,
                        LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->hourly_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *detail = lv_button_create(state->page.content);
    lv_obj_set_size(detail, LV_PCT(100), 44);
    lv_obj_set_style_radius(detail, 6, 0);
    lv_obj_set_style_bg_color(detail, lv_color_hex(WEATHER_COLOR_SURFACE_HI),
                              0);
    lv_obj_set_style_shadow_width(detail, 0, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detail, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(detail, _weather_root_open_forecast,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *detail_text = weather_ui_text_label(
                                detail, "查看详细预报",
                                APP_THEME_FONT_SMALL);
    lv_obj_set_style_text_color(detail_text, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    lv_obj_t *chevron = weather_ui_symbol_label(detail, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_MUTED), 0);

    _weather_root_render(state);
}

static void _weather_root_resume(weather_root_state_t *state)
{
    _weather_root_refresh_snapshot(state);
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return;
    }
    esp_err_t result = event_bus_subscribe(
                           WEATHER_SERVICE_MSG,
                           WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                           _weather_root_event, state, EVENT_BUS_DISPATCH_UI,
                           &state->subscription);
    if (result != ESP_OK)
    {
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_W("weather subscription failed: %s", esp_err_to_name(result));
    }
}

static esp_err_t _weather_root_pause(weather_root_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    weather_ui_release_snapshot(&state->snapshot);
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

const app_manager_page_definition_t weather_root_page_definition =
{
    .handler = _weather_root_handler,
    .memory_size = sizeof(weather_root_state_t),
};
