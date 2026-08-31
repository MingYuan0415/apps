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

_Static_assert(sizeof(weather_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Weather root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_service_event_t) <= 256U,
               "Weather event exceeds the Event Bus payload limit");

static lv_obj_t *_weather_root_metric(lv_obj_t *parent, const char *name,
                                      const char *value)
{
    lv_obj_t *cell = weather_ui_surface(parent, 58);
    lv_obj_set_width(cell, LV_PCT(48));
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cell, 4, 0);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_t *name_label = weather_ui_text_label(cell, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(name_label, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    lv_label_set_text(name_label, name);
    lv_obj_t *value_label = weather_ui_text_label(cell, APP_THEME_FONT_SMALL);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(value_label, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    lv_label_set_text(value_label, value);
    return cell;
}

static void _weather_root_append_status(char *text, size_t text_size,
                                        const char *fragment)
{
    size_t used = strlen(text);
    if (fragment[0] == '\0' || used >= text_size)
    {
        return;
    }
    (void)snprintf(text + used, text_size - used, "%s%s",
                   used > 0U ? " · " : "", fragment);
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
        lv_obj_t *empty = weather_ui_text_label(state->metrics,
                                                APP_THEME_FONT_BODY);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        lv_label_set_text(empty, "暂无实况指标");
        return;
    }
    const weather_service_current_t *current = &state->snapshot->current;
    char humidity[32];
    char precipitation[32];
    char wind[32];
    char visibility[32];
    (void)snprintf(humidity, sizeof(humidity), "%u%%",
                   (unsigned)current->humidity_percent);
    (void)snprintf(precipitation, sizeof(precipitation), "%.1f mm",
                   current->precipitation_tenths_mm / 10.0);
    (void)snprintf(wind, sizeof(wind), "%.0f km/h",
                   current->wind_speed_tenths_kmh / 10.0);
    (void)snprintf(visibility, sizeof(visibility), "%.1f km",
                   current->visibility_tenths_km / 10.0);
    (void)_weather_root_metric(state->metrics, "湿度", humidity);
    (void)_weather_root_metric(state->metrics, "降水", precipitation);
    (void)_weather_root_metric(state->metrics, "风速", wind);
    (void)_weather_root_metric(state->metrics, "能见度", visibility);
}

static void _weather_root_render_hourly(weather_root_state_t *state)
{
    lv_obj_clean(state->hourly_row);
    if (state->snapshot == NULL || !state->snapshot->hourly.meta.available ||
            state->snapshot->hourly.count == 0U)
    {
        lv_obj_t *empty = weather_ui_text_label(state->hourly_row,
                                                APP_THEME_FONT_BODY);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        lv_label_set_text(empty, "暂无逐小时预报");
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
        lv_obj_t *item = weather_ui_container(state->hourly_row, 88,
                                              LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(item, LV_PCT(24));
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        char text[24];
        weather_ui_format_time(&hour->forecast_at, "%H:%M", text,
                               sizeof(text));
        lv_obj_t *time = weather_ui_text_label(item, APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(time, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        lv_label_set_text(time, text[0] != '\0' ? text : "--:--");
        (void)weather_ui_small_icon(item, hour->condition_code);
        (void)snprintf(text, sizeof(text), "%.0f°",
                       hour->temperature_tenths_c / 10.0);
        lv_obj_t *temperature = weather_ui_text_label(item,
                                APP_THEME_FONT_SMALL);
        lv_obj_set_style_text_color(temperature,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
        lv_label_set_text(temperature, text);
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
    app_ui_status_t color = weather_ui_state_color(status.state);
    const weather_service_dataset_meta_t *meta = NULL;
    bool location_reused = false;
    if (state->snapshot != NULL)
    {
        if (state->snapshot->current.meta.available)
        {
            meta = &state->snapshot->current.meta;
        }
        location_reused = state->snapshot->location.available &&
                          state->snapshot->location.reused;
    }
    if (status.state == WEATHER_SERVICE_STATE_READY && meta != NULL &&
            !meta->stale && !meta->expired && !location_reused)
    {
        char updated[32];
        char status_text[48];
        weather_ui_format_dataset_time(meta, updated, sizeof(updated));
        if (updated[0] != '\0')
        {
            (void)snprintf(status_text, sizeof(status_text), "%s 更新",
                           updated);
            app_ui_set_status_text(state->status_label, status_text, color);
            return;
        }
    }
    char text[96] = "";
    _weather_root_append_status(text, sizeof(text),
                                weather_ui_state_short_text(status.state));
    if (meta != NULL && meta->expired)
    {
        _weather_root_append_status(text, sizeof(text), "实况过期");
        if (status.state == WEATHER_SERVICE_STATE_READY)
        {
            color = APP_UI_STATUS_WARNING;
        }
    }
    else if (meta != NULL && meta->stale)
    {
        _weather_root_append_status(text, sizeof(text), "缓存实况");
        if (status.state == WEATHER_SERVICE_STATE_READY)
        {
            color = APP_UI_STATUS_WARNING;
        }
    }
    if (location_reused)
    {
        _weather_root_append_status(text, sizeof(text), "沿用位置");
        if (status.state == WEATHER_SERVICE_STATE_READY)
        {
            color = APP_UI_STATUS_WARNING;
        }
    }
    if (text[0] == '\0')
    {
        (void)snprintf(text, sizeof(text), "%s",
                       meta == NULL ? "暂无实况数据" : "已更新");
    }
    app_ui_set_status_text(state->status_label, text, color);
}

static void _weather_root_render(weather_root_state_t *state)
{
    _weather_root_render_status(state);
    if (state->snapshot != NULL && state->snapshot->location.available &&
            state->snapshot->location.city[0] != '\0')
    {
        char title[WEATHER_SERVICE_CITY_BYTES + WEATHER_SERVICE_DISTRICT_BYTES +
                   4U];
        if (state->snapshot->location.district[0] != '\0')
        {
            (void)snprintf(title, sizeof(title), "%s·%s",
                           state->snapshot->location.city,
                           state->snapshot->location.district);
        }
        else
        {
            (void)snprintf(title, sizeof(title), "%s",
                           state->snapshot->location.city);
        }
        lv_label_set_text(state->city_label, title);
    }
    else
    {
        lv_label_set_text(state->city_label, "天气");
    }
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        lv_label_set_text(state->temperature_label, "--°");
        lv_label_set_text(state->condition_label, "暂无实时天气");
        lv_label_set_text(state->range_label, "体感--°  高--°  低--°");
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
                           "体感%.0f°  高%.0f°  低%.0f°",
                           snapshot->current.feels_like_tenths_c / 10.0,
                           today->maximum_temperature_tenths_c / 10.0,
                           today->minimum_temperature_tenths_c / 10.0);
        }
        else
        {
            (void)snprintf(text, sizeof(text), "体感%.0f°  高--°  低--°",
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
        char text[64];
        (void)snprintf(text, sizeof(text), "共 %u 条气象预警%s",
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
    lv_obj_t *icon = weather_ui_symbol_label(button);
    lv_obj_center(icon);
    lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    return button;
}

static void _weather_root_build(weather_root_state_t *state)
{
    app_ui_page_create(&state->page, "天气", true);
    app_ui_page_set_subtitle(&state->page, "当前位置");
    lv_obj_set_style_pad_row(state->page.content, 6, 0);
    state->city_label = state->page.title;
    lv_obj_set_height(state->city_label, 32);
    lv_label_set_long_mode(state->city_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    (void)_weather_root_header_button(state);

    lv_obj_t *hero = weather_ui_container(state->page.content, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *summary = weather_ui_container(hero, LV_SIZE_CONTENT,
                        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(summary, 0);
    lv_obj_set_flex_grow(summary, 1);
    lv_obj_set_style_pad_row(summary, 1, 0);
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    state->temperature_label = weather_ui_text_label(summary,
                               APP_THEME_FONT_TITLE);
    lv_obj_set_style_text_color(state->temperature_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    state->condition_label = weather_ui_text_label(summary,
                             APP_THEME_FONT_HEAD);
    lv_obj_set_width(state->condition_label, LV_PCT(100));
    lv_label_set_long_mode(state->condition_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->condition_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    state->range_label = weather_ui_text_label(summary, APP_THEME_FONT_BODY);
    lv_obj_set_width(state->range_label, LV_PCT(100));
    lv_label_set_long_mode(state->range_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->range_label,
                                lv_color_hex(WEATHER_COLOR_MUTED), 0);

    lv_obj_t *visual = weather_ui_container(hero, 112, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(visual, 112);
    state->main_image = lv_image_create(visual);
    lv_obj_set_size(state->main_image, 112, 112);
    state->image_fallback = weather_ui_symbol_label(visual);
    lv_obj_set_style_text_color(state->image_fallback,
                                lv_color_hex(WEATHER_COLOR_SUN), 0);
    lv_obj_center(state->image_fallback);
    lv_label_set_text(state->image_fallback, LV_SYMBOL_IMAGE);

    state->status_label = weather_ui_text_label(state->page.content,
                          APP_THEME_FONT_BODY);
    lv_obj_set_size(state->status_label, LV_PCT(100), 22);
    lv_label_set_long_mode(state->status_label,
                           LV_LABEL_LONG_SCROLL_CIRCULAR);

    state->alert_button = lv_button_create(state->page.content);
    lv_obj_set_size(state->alert_button, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(state->alert_button, 6, 0);
    lv_obj_set_style_bg_color(state->alert_button,
                              lv_color_hex(WEATHER_COLOR_WARNING_BG), 0);
    lv_obj_set_style_shadow_width(state->alert_button, 0, 0);
    lv_obj_set_style_pad_all(state->alert_button, 10, 0);
    lv_obj_add_event_cb(state->alert_button, _weather_root_open_alerts,
                        LV_EVENT_CLICKED, NULL);
    state->alert_label = weather_ui_text_label(state->alert_button,
                         APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->alert_label, LV_PCT(100));
    lv_label_set_long_mode(state->alert_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->alert_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_obj_center(state->alert_label);

    state->metrics = weather_ui_container(state->page.content, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(state->metrics, 8, 0);
    lv_obj_set_style_pad_row(state->metrics, 6, 0);
    lv_obj_set_flex_align(state->metrics, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    state->hourly_row = weather_ui_container(state->page.content, 88,
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
    lv_obj_t *detail_text = weather_ui_text_label(detail,
                            APP_THEME_FONT_SMALL);
    lv_obj_set_style_text_color(detail_text, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    lv_label_set_text(detail_text, "查看详细预报");
    lv_obj_t *chevron = weather_ui_symbol_label(detail);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);

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
    return result;
}

static void _weather_root_start(
    const app_manager_page_context_t *context)
{
    weather_root_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static void _weather_root_mount(
    const app_manager_page_context_t *context)
{
    _weather_root_build(context->state);
}

static void _weather_root_resume_op(
    const app_manager_page_context_t *context)
{
    _weather_root_resume(context->state);
}

static esp_err_t _weather_root_pause_op(
    const app_manager_page_context_t *context)
{
    return _weather_root_pause(context->state);
}

static void _weather_root_unmount(
    const app_manager_page_context_t *context)
{
    weather_root_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->city_label = NULL;
    state->status_label = NULL;
    state->temperature_label = NULL;
    state->condition_label = NULL;
    state->range_label = NULL;
    state->main_image = NULL;
    state->image_fallback = NULL;
    state->alert_button = NULL;
    state->alert_label = NULL;
    state->metrics = NULL;
    state->hourly_row = NULL;
}

static const app_manager_page_ops_t s_weather_root_ops =
{
    .start = _weather_root_start,
    .mount = _weather_root_mount,
    .resume = _weather_root_resume_op,
    .pause = _weather_root_pause_op,
    .unmount = _weather_root_unmount,
};

const app_manager_page_definition_t weather_root_page_definition =
{
    .ops = &s_weather_root_ops,
    .memory_size = sizeof(weather_root_state_t),
};
