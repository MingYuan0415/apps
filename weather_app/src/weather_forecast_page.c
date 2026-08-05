#define DBG_TAG "weather_forecast"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "weather_app_internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef enum weather_forecast_segment
{
    WEATHER_FORECAST_CURRENT = 0,
    WEATHER_FORECAST_HOURLY,
    WEATHER_FORECAST_DAILY,
    WEATHER_FORECAST_SEGMENT_COUNT,
} weather_forecast_segment_t;

typedef struct weather_forecast_state
{
    app_ui_page_t page;
    lv_obj_t *segment_buttons[WEATHER_FORECAST_SEGMENT_COUNT];
    lv_obj_t *body;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
    weather_forecast_segment_t segment;
} weather_forecast_state_t;

_Static_assert(sizeof(weather_forecast_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Weather forecast state exceeds the lifecycle arena slot");

static void _weather_forecast_render(weather_forecast_state_t *state);

static void _weather_forecast_add_service_status(lv_obj_t *parent)
{
    weather_service_status_snapshot_t status = {0};
    if (weather_service_get_status(&status) != ESP_OK ||
            status.state == WEATHER_SERVICE_STATE_READY)
    {
        return;
    }
    char text[96];
    (void)snprintf(text, sizeof(text), "服务状态：%s",
                   weather_ui_state_text(status.state));
    lv_obj_t *service = weather_ui_text_label(
                            parent, text, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(service,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
}

static lv_obj_t *_weather_forecast_metric(lv_obj_t *parent, const char *name,
        const char *value)
{
    lv_obj_t *cell = weather_ui_surface(parent, 66);
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

static void _weather_forecast_metric_pair(
    lv_obj_t *parent, const char *left_name, const char *left_value,
    const char *right_name, const char *right_value)
{
    lv_obj_t *row = weather_ui_container(parent, 66, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    (void)_weather_forecast_metric(row, left_name, left_value);
    (void)_weather_forecast_metric(row, right_name, right_value);
}

static void _weather_forecast_add_meta(
    lv_obj_t *parent, const weather_service_dataset_meta_t *meta,
    const char *source)
{
    char updated[32];
    char text[96];
    weather_ui_format_dataset_time(meta, updated, sizeof(updated));
    (void)snprintf(text, sizeof(text), "更新时间：%s",
                   updated[0] != '\0' ? updated : "未提供");
    lv_obj_t *time = weather_ui_text_label(parent, text, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(time, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    (void)snprintf(text, sizeof(text), "有效性：%s",
                   weather_ui_dataset_state(meta));
    lv_obj_t *validity = weather_ui_text_label(
                             parent, text, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(validity, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    (void)snprintf(text, sizeof(text), "数据来源：%s", source);
    lv_obj_t *provider = weather_ui_text_label(
                             parent, text, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(provider, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    _weather_forecast_add_service_status(parent);
}

static void _weather_forecast_render_current(weather_forecast_state_t *state)
{
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        lv_obj_t *empty = weather_ui_text_label(
                              state->body, "暂无实况数据",
                              APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        _weather_forecast_add_service_status(state->body);
        return;
    }
    const weather_service_current_t *current = &state->snapshot->current;
    char left[48];
    char right[48];
    (void)snprintf(left, sizeof(left), "%.1f°C",
                   current->feels_like_tenths_c / 10.0);
    (void)snprintf(right, sizeof(right), "%u%%",
                   (unsigned)current->humidity_percent);
    _weather_forecast_metric_pair(state->body, "体感", left, "湿度", right);
    (void)snprintf(left, sizeof(left), "%.1f mm",
                   current->precipitation_tenths_mm / 10.0);
    (void)snprintf(right, sizeof(right), "%.1f km/h",
                   current->wind_speed_tenths_kmh / 10.0);
    _weather_forecast_metric_pair(state->body, "降水", left, "风速", right);
    _weather_forecast_metric_pair(
        state->body, "风向",
        current->wind_direction[0] != '\0' ? current->wind_direction : "--",
        "风力", current->wind_scale[0] != '\0' ? current->wind_scale : "--");
    (void)snprintf(left, sizeof(left), "%u hPa",
                   (unsigned)current->pressure_hpa);
    (void)snprintf(right, sizeof(right), "%.1f km",
                   current->visibility_tenths_km / 10.0);
    _weather_forecast_metric_pair(state->body, "气压", left, "能见度",
                                  right);
    _weather_forecast_add_meta(state->body, &current->meta, "QWeather");
}

static void _weather_forecast_render_chart(weather_forecast_state_t *state)
{
    const weather_service_hourly_t *hourly = &state->snapshot->hourly;
    uint8_t count = hourly->count > 12U ? 12U : hourly->count;
    int32_t minimum = INT32_MAX;
    int32_t maximum = INT32_MIN;
    for (uint8_t index = 0U; index < count; ++index)
    {
        int32_t value = hourly->items[index].temperature_tenths_c;
        if (value < minimum)
        {
            minimum = value;
        }
        if (value > maximum)
        {
            maximum = value;
        }
    }
    if (minimum == maximum)
    {
        minimum -= 10;
        maximum += 10;
    }
    const int32_t axis_minimum = minimum - 10;
    const int32_t axis_maximum = maximum + 10;
    lv_obj_t *chart_row = weather_ui_container(
                              state->body, 116, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chart_row, 6, 0);
    lv_obj_t *chart = lv_chart_create(chart_row);
    lv_obj_set_size(chart, 0, 116);
    lv_obj_set_flex_grow(chart, 1);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, count);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                            axis_minimum, axis_maximum);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 6, 0);
    lv_obj_t *scale = weather_ui_container(
                          chart_row, 116, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(scale, 56);
    lv_obj_set_flex_align(scale, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int32_t scale_values[] =
    {
        axis_maximum,
        axis_minimum + (axis_maximum - axis_minimum) / 2,
        axis_minimum,
    };
    for (size_t index = 0U;
            index < sizeof(scale_values) / sizeof(scale_values[0]); ++index)
    {
        char text[16];
        (void)snprintf(text, sizeof(text), "%.0f°C",
                       scale_values[index] / 10.0);
        lv_obj_t *label = weather_ui_text_label(
                              scale, text, APP_THEME_FONT_BODY);
        lv_obj_set_size(label, 56, 21);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(label,
                                    lv_color_hex(WEATHER_COLOR_MUTED), 0);
    }
    lv_chart_series_t *series = lv_chart_add_series(
                                    chart, lv_color_hex(WEATHER_COLOR_SUN),
                                    LV_CHART_AXIS_PRIMARY_Y);
    if (series == NULL)
    {
        return;
    }
    for (uint8_t index = 0U; index < count; ++index)
    {
        lv_chart_set_next_value(chart, series,
                                hourly->items[index].temperature_tenths_c);
    }
}

static void _weather_forecast_render_hourly(weather_forecast_state_t *state)
{
    if (state->snapshot == NULL || !state->snapshot->hourly.meta.available ||
            state->snapshot->hourly.count == 0U)
    {
        lv_obj_t *empty = weather_ui_text_label(
                              state->body, "暂无逐小时预报",
                              APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        _weather_forecast_add_service_status(state->body);
        return;
    }
    _weather_forecast_render_chart(state);
    const weather_service_hourly_t *hourly = &state->snapshot->hourly;
    for (uint8_t index = 0U; index < hourly->count; ++index)
    {
        const weather_service_hour_t *hour = &hourly->items[index];
        lv_obj_t *row = weather_ui_surface(state->body, 70);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        char text[48];
        weather_ui_format_time(&hour->forecast_at, "%m-%d\n%H:%M", text,
                               sizeof(text));
        lv_obj_t *time = weather_ui_text_label(
                             row, text[0] != '\0' ? text : "--:--",
                             APP_THEME_FONT_BODY);
        lv_obj_set_size(time, 58, 42);
        lv_label_set_long_mode(time, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(time, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        (void)weather_ui_small_icon(row, hour->condition_code);
        lv_obj_t *info = weather_ui_container(
                             row, 50, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(info, 0);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_style_pad_row(info, 2, 0);
        lv_obj_t *top = weather_ui_container(
                            info, 26, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(top, 6, 0);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        (void)snprintf(text, sizeof(text), "%.0f°",
                       hour->temperature_tenths_c / 10.0);
        lv_obj_t *temperature = weather_ui_text_label(
                                    top, text, APP_THEME_FONT_SMALL);
        lv_obj_set_size(temperature, 54, 26);
        lv_label_set_long_mode(temperature, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(temperature,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
        (void)snprintf(text, sizeof(text), "降水 %u%%",
                       (unsigned)hour->precipitation_chance_percent);
        lv_obj_t *precipitation = weather_ui_text_label(
                                      top, text, APP_THEME_FONT_BODY);
        lv_obj_set_size(precipitation, 0, 21);
        lv_obj_set_flex_grow(precipitation, 1);
        lv_label_set_long_mode(precipitation, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(precipitation,
                                    lv_color_hex(WEATHER_COLOR_MUTED), 0);
        (void)snprintf(text, sizeof(text), "湿度 %u%% · %.0f km/h",
                       (unsigned)hour->humidity_percent,
                       hour->wind_speed_tenths_kmh / 10.0);
        lv_obj_t *details = weather_ui_text_label(
                                info, text, APP_THEME_FONT_BODY);
        lv_obj_set_size(details, LV_PCT(100), 21);
        lv_label_set_long_mode(details, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(details,
                                    lv_color_hex(WEATHER_COLOR_MUTED), 0);
    }
    _weather_forecast_add_meta(state->body, &hourly->meta, "QWeather");
}

static unsigned _weather_forecast_weekday(const char *date)
{
    unsigned year = 0U;
    unsigned month = 0U;
    unsigned day = 0U;
    if (sscanf(date, "%u-%u-%u", &year, &month, &day) != 3)
    {
        return 7U;
    }
    if (month < 1U || month > 12U || day < 1U || day > 31U)
    {
        return 7U;
    }
    static const unsigned month_offsets[] =
    {
        0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U,
    };
    if (month < 3U)
    {
        --year;
    }
    return (year + year / 4U - year / 100U + year / 400U +
            month_offsets[month - 1U] + day) % 7U;
}

static const char *_weather_forecast_weekday_text(unsigned weekday)
{
    static const char *const names[] =
    {
        "周日", "周一", "周二", "周三", "周四", "周五", "周六",
    };
    return weekday < 7U ? names[weekday] : "日期";
}

static void _weather_forecast_render_daily(weather_forecast_state_t *state)
{
    if (state->snapshot == NULL || !state->snapshot->daily.meta.available ||
            state->snapshot->daily.count == 0U)
    {
        lv_obj_t *empty = weather_ui_text_label(
                              state->body, "暂无 7 天预报",
                              APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        _weather_forecast_add_service_status(state->body);
        return;
    }
    const weather_service_daily_t *daily = &state->snapshot->daily;
    int16_t full_minimum = INT16_MAX;
    int16_t full_maximum = INT16_MIN;
    for (uint8_t index = 0U; index < daily->count; ++index)
    {
        if (daily->items[index].minimum_temperature_tenths_c < full_minimum)
        {
            full_minimum = daily->items[index].minimum_temperature_tenths_c;
        }
        if (daily->items[index].maximum_temperature_tenths_c > full_maximum)
        {
            full_maximum = daily->items[index].maximum_temperature_tenths_c;
        }
    }
    if (full_minimum == full_maximum)
    {
        --full_minimum;
        ++full_maximum;
    }
    for (uint8_t index = 0U; index < daily->count; ++index)
    {
        const weather_service_day_t *day = &daily->items[index];
        lv_obj_t *row = weather_ui_surface(state->body, 96);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 7, 0);
        char text[96];
        (void)snprintf(text, sizeof(text), "%s\n%.5s",
                       _weather_forecast_weekday_text(
                           _weather_forecast_weekday(day->date)),
                       day->date + 5);
        lv_obj_t *date = weather_ui_text_label(
                             row, text, APP_THEME_FONT_BODY);
        lv_obj_set_width(date, 52);
        lv_obj_set_style_text_color(date, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        (void)weather_ui_small_icon(row, day->day_condition_code);
        lv_obj_t *summary = weather_ui_container(
                                row, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_grow(summary, 1);
        (void)snprintf(text, sizeof(text), "%s / %s",
                       day->day_condition_text, day->night_condition_text);
        lv_obj_t *condition = weather_ui_text_label(
                                  summary, text, APP_THEME_FONT_SMALL);
        lv_obj_set_width(condition, LV_PCT(100));
        lv_label_set_long_mode(condition, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(condition,
                                    lv_color_hex(WEATHER_COLOR_TEXT), 0);
        (void)snprintf(text, sizeof(text), "%.0f° / %.0f°",
                       day->maximum_temperature_tenths_c / 10.0,
                       day->minimum_temperature_tenths_c / 10.0);
        lv_obj_t *range = weather_ui_text_label(
                              summary, text, APP_THEME_FONT_SMALL);
        lv_obj_set_style_text_color(range, lv_color_hex(WEATHER_COLOR_TEXT),
                                    0);
        lv_obj_t *bar = lv_bar_create(summary);
        lv_obj_set_size(bar, LV_PCT(100), 6);
        lv_bar_set_range(bar, full_minimum, full_maximum);
        lv_bar_set_mode(bar, LV_BAR_MODE_RANGE);
        lv_bar_set_start_value(bar, day->minimum_temperature_tenths_c,
                               LV_ANIM_OFF);
        lv_bar_set_value(bar, day->maximum_temperature_tenths_c,
                         LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(WEATHER_COLOR_SURFACE_HI),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(WEATHER_COLOR_SUN),
                                  LV_PART_INDICATOR);
        (void)snprintf(text, sizeof(text), "降水 %.1f mm · UV %u",
                       day->precipitation_tenths_mm / 10.0,
                       (unsigned)day->uv_index);
        lv_obj_t *details = weather_ui_text_label(
                                summary, text, APP_THEME_FONT_BODY);
        lv_obj_set_style_text_color(details,
                                    lv_color_hex(WEATHER_COLOR_MUTED), 0);
    }
    _weather_forecast_add_meta(state->body, &daily->meta, "QWeather");
}

static void _weather_forecast_render(weather_forecast_state_t *state)
{
    lv_obj_clean(state->body);
    for (unsigned index = 0U; index < WEATHER_FORECAST_SEGMENT_COUNT; ++index)
    {
        if (index == (unsigned)state->segment)
        {
            lv_obj_add_state(state->segment_buttons[index], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(state->segment_buttons[index],
                                LV_STATE_CHECKED);
        }
    }
    switch (state->segment)
    {
    case WEATHER_FORECAST_HOURLY:
        _weather_forecast_render_hourly(state);
        break;
    case WEATHER_FORECAST_DAILY:
        _weather_forecast_render_daily(state);
        break;
    case WEATHER_FORECAST_CURRENT:
    default:
        _weather_forecast_render_current(state);
        break;
    }
}

static void _weather_forecast_segment_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    weather_forecast_state_t *state = lv_event_get_user_data(event);
    uintptr_t segment = (uintptr_t)lv_obj_get_user_data(
                            lv_event_get_target_obj(event));
    if (segment >= WEATHER_FORECAST_SEGMENT_COUNT)
    {
        return;
    }
    state->segment = (weather_forecast_segment_t)segment;
    _weather_forecast_render(state);
}

static void _weather_forecast_refresh(weather_forecast_state_t *state)
{
    weather_ui_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_forecast_render(state);
}

static void _weather_forecast_event(event_bus_msg_id_t msg_id,
                                    uint32_t sub_type, const void *payload,
                                    size_t payload_size, void *user_data)
{
    weather_forecast_state_t *state = user_data;
    if (weather_ui_is_snapshot_event(msg_id, sub_type, payload,
                                     payload_size) &&
            state->page.root != NULL)
    {
        _weather_forecast_refresh(state);
    }
}

static void _weather_forecast_build(weather_forecast_state_t *state)
{
    app_ui_page_create(&state->page, "详细预报", true);
    lv_obj_t *segments = weather_ui_container(
                             state->page.content, 42, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(segments, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(segments, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(segments, 6, 0);
    lv_obj_set_style_pad_all(segments, 3, 0);
    lv_obj_set_style_pad_column(segments, 3, 0);
    static const char *const titles[] = {"实况", "逐小时", "7 天"};
    for (uintptr_t index = 0U; index < WEATHER_FORECAST_SEGMENT_COUNT; ++index)
    {
        lv_obj_t *button = lv_button_create(segments);
        lv_obj_set_size(button, LV_PCT(32), LV_PCT(100));
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(WEATHER_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(WEATHER_COLOR_SURFACE_HI),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_user_data(button, (void *)index);
        lv_obj_add_event_cb(button, _weather_forecast_segment_event,
                            LV_EVENT_CLICKED, state);
        lv_obj_t *label = weather_ui_text_label(
                              button, titles[index], APP_THEME_FONT_SMALL);
        lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_TEXT),
                                    0);
        lv_obj_center(label);
        state->segment_buttons[index] = button;
    }
    state->body = weather_ui_container(state->page.content, LV_SIZE_CONTENT,
                                       LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->body, 8, 0);
    _weather_forecast_render(state);
}

static void _weather_forecast_resume(weather_forecast_state_t *state)
{
    _weather_forecast_refresh(state);
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return;
    }
    esp_err_t result = event_bus_subscribe(
                           WEATHER_SERVICE_MSG,
                           WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                           _weather_forecast_event, state,
                           EVENT_BUS_DISPATCH_UI, &state->subscription);
    if (result != ESP_OK)
    {
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_W("weather subscription failed: %s", esp_err_to_name(result));
    }
}

static esp_err_t _weather_forecast_pause(weather_forecast_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    weather_ui_release_snapshot(&state->snapshot);
    return result;
}

static void _weather_forecast_start(
    const app_manager_page_context_t *context)
{
    weather_forecast_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->segment = WEATHER_FORECAST_CURRENT;
}

static void _weather_forecast_mount(
    const app_manager_page_context_t *context)
{
    _weather_forecast_build(context->state);
}

static void _weather_forecast_resume_op(
    const app_manager_page_context_t *context)
{
    _weather_forecast_resume(context->state);
}

static esp_err_t _weather_forecast_pause_op(
    const app_manager_page_context_t *context)
{
    return _weather_forecast_pause(context->state);
}

static void _weather_forecast_unmount(
    const app_manager_page_context_t *context)
{
    weather_forecast_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    for (size_t index = 0U; index < WEATHER_FORECAST_SEGMENT_COUNT; ++index)
    {
        state->segment_buttons[index] = NULL;
    }
    state->body = NULL;
}

static const app_manager_page_ops_t s_weather_forecast_ops =
{
    .start = _weather_forecast_start,
    .mount = _weather_forecast_mount,
    .resume = _weather_forecast_resume_op,
    .pause = _weather_forecast_pause_op,
    .unmount = _weather_forecast_unmount,
};

const app_manager_page_definition_t weather_forecast_page_definition =
{
    .ops = &s_weather_forecast_ops,
    .memory_size = sizeof(weather_forecast_state_t),
};
