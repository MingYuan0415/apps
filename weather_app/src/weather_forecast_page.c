#define DBG_TAG "weather_forecast"
#include "mt_log.h"

#include "weather_app_internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Every dataset in the snapshot is served by QWeather (和风天气). The
 * snapshot's location.provider (e.g. cloudflare) only reports the IP
 * geolocation that resolved the city, so it is not the weather data source and
 * must never be shown as one. */
#define WEATHER_FORECAST_SOURCE "和风天气"
#define WEATHER_FORECAST_TILE_COUNT 3U
#define WEATHER_FORECAST_CHART_HEIGHT 108
#define WEATHER_FORECAST_METRIC_HEIGHT 30

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
    lv_obj_t *tileview;
    lv_obj_t *service_status_box;
    lv_obj_t *service_status;
    lv_obj_t *tiles[WEATHER_FORECAST_TILE_COUNT];
    lv_obj_t *scroll_owners[WEATHER_FORECAST_TILE_COUNT];
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
    weather_forecast_segment_t segment;
} weather_forecast_state_t;

_Static_assert(sizeof(weather_forecast_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Weather forecast state exceeds the lifecycle arena slot");

static void _weather_forecast_render(weather_forecast_state_t *state);

static void _weather_forecast_update_service_status(
    weather_forecast_state_t *state)
{
    if (state->service_status_box == NULL || state->service_status == NULL)
    {
        return;
    }
    weather_service_status_snapshot_t status = {0};
    if (weather_service_get_status(&status) != ESP_OK ||
            status.state == WEATHER_SERVICE_STATE_READY)
    {
        lv_obj_set_hidden(state->service_status_box, true);
        return;
    }

    const char *short_text = weather_ui_state_short_text(status.state);
    const bool retained =
        (status.available_mask & (WEATHER_SERVICE_DATA_CURRENT |
                                  WEATHER_SERVICE_DATA_ALERTS |
                                  WEATHER_SERVICE_DATA_HOURLY |
                                  WEATHER_SERVICE_DATA_DAILY)) != 0U;
    char text[64];
    if (retained && (status.state == WEATHER_SERVICE_STATE_DEGRADED ||
                     status.state == WEATHER_SERVICE_STATE_AUTH_ERROR ||
                     status.state == WEATHER_SERVICE_STATE_RATE_LIMITED))
    {
        (void)snprintf(text, sizeof(text), "%s，显示已有数据", short_text);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%s", short_text);
    }
    lv_label_set_text(state->service_status, text);

    const app_ui_status_t semantic = weather_ui_state_color(status.state);
    uint32_t text_color = WEATHER_COLOR_MUTED;
    uint32_t tint = WEATHER_COLOR_SURFACE;
    if (semantic == APP_UI_STATUS_WARNING)
    {
        text_color = APP_UI_COLOR_SUN;
        tint = APP_UI_COLOR_SUN_TINT;
    }
    else if (semantic == APP_UI_STATUS_ERROR)
    {
        text_color = APP_UI_COLOR_WARNING;
        tint = APP_UI_COLOR_WARNING_TINT;
    }
    else if (semantic == APP_UI_STATUS_ACCENT)
    {
        text_color = WEATHER_COLOR_RAIN;
        tint = APP_UI_COLOR_RAIN_TINT;
    }
    lv_obj_set_style_bg_color(state->service_status_box,
                              lv_color_hex(tint), 0);
    lv_obj_set_style_text_color(state->service_status,
                                lv_color_hex(text_color), 0);
    lv_obj_set_hidden(state->service_status_box, false);
}

static void _weather_forecast_empty(lv_obj_t *parent, const char *text)
{
    lv_obj_t *empty = weather_ui_text_label(parent, APP_THEME_FONT_BODY);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(empty, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(empty, text);
}

static lv_obj_t *_weather_forecast_metric(lv_obj_t *grid, const char *name,
        const char *value)
{
    lv_obj_t *cell = weather_ui_container(grid, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(cell, LV_PCT(31));
    lv_obj_set_style_min_height(cell, WEATHER_FORECAST_METRIC_HEIGHT, 0);
    lv_obj_set_style_pad_hor(cell, 2, 0);
    lv_obj_set_style_pad_row(cell, 0, 0);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_t *name_label = weather_ui_text_label(cell, APP_THEME_FONT_SMALL);
    lv_obj_set_width(name_label, LV_PCT(100));
    lv_obj_set_style_text_color(name_label, lv_color_hex(APP_UI_COLOR_FAINT),
                                0);
    lv_label_set_text(name_label, name);
    lv_obj_t *value_label = weather_ui_text_label(cell, APP_THEME_FONT_SMALL);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_letter_space(value_label, -1, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    lv_label_set_text(value_label, value);
    return cell;
}

static lv_obj_t *_weather_forecast_temperature_scale(lv_obj_t *parent,
        const weather_service_current_t *current,
        const weather_service_day_t *today)
{
    int32_t minimum = current->temperature_tenths_c;
    int32_t maximum = minimum;
    if (today != NULL)
    {
        minimum = today->minimum_temperature_tenths_c;
        maximum = today->maximum_temperature_tenths_c;
    }
    if (minimum == maximum)
    {
        --minimum;
        ++maximum;
    }
    lv_obj_t *scale_box = weather_ui_container(parent, 34,
        LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(scale_box, 10, 0);
    lv_obj_t *scale = lv_scale_create(scale_box);
    lv_obj_set_size(scale, LV_PCT(100), 34);
    lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(scale, 5);
    lv_scale_set_major_tick_every(scale, 2);
    lv_scale_set_label_show(scale, true);
    int32_t low = minimum / 10;
    int32_t high = maximum / 10;
    if (low == high)
    {
        --low;
        ++high;
    }
    lv_scale_set_range(scale, low, high);
    lv_obj_set_style_text_font(scale, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(scale, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_obj_set_style_line_color(scale, lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_t *needle = lv_line_create(scale);
    lv_obj_set_style_line_width(needle, 2, 0);
    lv_obj_set_style_line_color(needle, lv_color_hex(WEATHER_COLOR_SUN), 0);
    lv_scale_set_line_needle_value(scale, needle, 16,
                                   current->temperature_tenths_c / 10);
    app_ui_make_passive(scale, false);
    return scale;
}

static void _weather_forecast_wind_card(lv_obj_t *parent,
                                        const weather_service_current_t *current)
{
    /* Keep the wind card textual: live provider names such as "东南风" are
     * three glyphs wide and must never be centered over a second label. */
    lv_obj_t *card = weather_ui_surface(parent, LV_SIZE_CONTENT);
    lv_obj_set_width(card, 104);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *direction = weather_ui_text_label(card, APP_THEME_FONT_SMALL);
    lv_obj_set_width(direction, LV_PCT(100));
    lv_label_set_long_mode(direction, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(direction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(direction, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    lv_label_set_text(direction,
                      current->wind_direction[0] != '\0' ?
                      current->wind_direction : "--");
    char text[32];
    (void)snprintf(text, sizeof(text), "%s 级",
                   current->wind_scale[0] != '\0' ?
                   current->wind_scale : "--");
    lv_obj_t *scale = weather_ui_text_label(card, APP_THEME_FONT_BODY);
    lv_obj_set_width(scale, LV_PCT(100));
    lv_obj_set_style_text_align(scale, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scale, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(scale, text);
}

static void _weather_forecast_render_now(weather_forecast_state_t *state,
        lv_obj_t *tile)
{
    state->scroll_owners[WEATHER_FORECAST_CURRENT] = tile;
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        _weather_forecast_empty(tile, "暂无实况数据");
        return;
    }
    const weather_service_snapshot_t *snapshot = state->snapshot;
    const weather_service_current_t *current = &snapshot->current;
    const weather_service_day_t *today = NULL;
    char date[16];
    weather_ui_format_time(&current->observed_at, "%Y-%m-%d", date,
                           sizeof(date));
    if (snapshot->daily.meta.available && date[0] != '\0')
    {
        for (uint8_t index = 0U; index < snapshot->daily.count; ++index)
        {
            if (strcmp(snapshot->daily.items[index].date, date) == 0)
            {
                today = &snapshot->daily.items[index];
                break;
            }
        }
    }

    lv_obj_t *hero = weather_ui_container(tile, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hero, 8, 0);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *summary = weather_ui_container(hero, LV_SIZE_CONTENT,
        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(summary, 0);
    lv_obj_set_flex_grow(summary, 1);
    lv_obj_set_style_pad_row(summary, 0, 0);
    lv_obj_t *temperature = weather_ui_text_label(summary, APP_THEME_FONT_TITLE);
    lv_obj_set_style_text_color(temperature, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    char text[48];
    (void)snprintf(text, sizeof(text), "%.0f°",
                   current->temperature_tenths_c / 10.0);
    lv_label_set_text(temperature, text);
    lv_obj_t *condition_row = weather_ui_container(summary, LV_SIZE_CONTENT,
        LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(condition_row, 6, 0);
    lv_obj_set_flex_align(condition_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)weather_ui_small_icon(condition_row, current->condition_code);
    lv_obj_t *condition = weather_ui_text_label(condition_row,
        APP_THEME_FONT_HEAD);
    lv_obj_set_width(condition, 0);
    lv_obj_set_flex_grow(condition, 1);
    lv_label_set_long_mode(condition, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(condition, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(condition, current->condition_text[0] != '\0' ?
                      current->condition_text : "天气未知");
    _weather_forecast_wind_card(hero, current);
    (void)_weather_forecast_temperature_scale(tile, current, today);

    lv_obj_t *grid = weather_ui_container(tile, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 2, 0);
    lv_obj_set_style_pad_row(grid, 0, 0);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    char value[32];
    (void)snprintf(value, sizeof(value), "%.1f mm",
                   current->precipitation_tenths_mm / 10.0);
    (void)_weather_forecast_metric(grid, "降水", value);
    (void)snprintf(value, sizeof(value), "%u%%",
                   (unsigned)current->humidity_percent);
    (void)_weather_forecast_metric(grid, "湿度", value);
    (void)snprintf(value, sizeof(value), "%.0f km/h",
                   current->wind_speed_tenths_kmh / 10.0);
    (void)_weather_forecast_metric(grid, "风速", value);
    (void)snprintf(value, sizeof(value), "%u hPa",
                   (unsigned)current->pressure_hpa);
    (void)_weather_forecast_metric(grid, "气压", value);
    (void)snprintf(value, sizeof(value), "%.1f km",
                   current->visibility_tenths_km / 10.0);
    (void)_weather_forecast_metric(grid, "能见度", value);
    (void)snprintf(value, sizeof(value), "%.1f°C",
                   current->feels_like_tenths_c / 10.0);
    (void)_weather_forecast_metric(grid, "体感", value);
}

static void _weather_forecast_render_chart(weather_forecast_state_t *state,
        lv_obj_t *parent)
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
    lv_obj_t *chart_row = weather_ui_container(parent,
        WEATHER_FORECAST_CHART_HEIGHT, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chart_row, 6, 0);
    lv_obj_t *chart = lv_chart_create(chart_row);
    lv_obj_set_size(chart, 0, WEATHER_FORECAST_CHART_HEIGHT);
    lv_obj_set_flex_grow(chart, 1);
    app_ui_make_passive(chart, false);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, count);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, axis_minimum,
                            axis_maximum);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, APP_UI_RADIUS_CARD, 0);
    lv_obj_t *scale = weather_ui_container(chart_row,
                                           WEATHER_FORECAST_CHART_HEIGHT,
                                           LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(scale, LV_SIZE_CONTENT);
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
        lv_obj_t *label = weather_ui_text_label(scale, APP_THEME_FONT_BODY);
        lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_MUTED),
                                    0);
        lv_label_set_text(label, text);
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

static void _weather_forecast_hour_row(lv_obj_t *list,
                                       const weather_service_hour_t *hour,
                                       bool first)
{
    if (!first)
    {
        lv_obj_t *separator = lv_obj_create(list);
        lv_obj_remove_style_all(separator);
        lv_obj_set_size(separator, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(separator,
                                  lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
        lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
        app_ui_make_passive(separator, false);
    }
    lv_obj_t *row = weather_ui_container(list, LV_SIZE_CONTENT,
                                         LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_min_height(row, 64, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_row(row, 0, 0);
    lv_obj_t *top = weather_ui_container(row, LV_SIZE_CONTENT,
                                         LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 8, 0);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    char text[64];
    weather_ui_format_time(&hour->forecast_at, "%H:%M", text, sizeof(text));
    lv_obj_t *time = weather_ui_text_label(top, APP_THEME_FONT_SMALL);
    lv_obj_set_size(time, 54, LV_SIZE_CONTENT);
    lv_label_set_long_mode(time, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(time, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(time, text[0] != '\0' ? text : "--:--");
    (void)weather_ui_small_icon(top, hour->condition_code);
    (void)snprintf(text, sizeof(text), "%.0f°",
                   hour->temperature_tenths_c / 10.0);
    lv_obj_t *temperature = weather_ui_text_label(top, APP_THEME_FONT_HEAD);
    lv_obj_set_size(temperature, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(temperature, lv_color_hex(WEATHER_COLOR_TEXT),
                                0);
    lv_label_set_text(temperature, text);
    (void)snprintf(text, sizeof(text), "降水 %u%%",
                   (unsigned)hour->precipitation_chance_percent);
    lv_obj_t *precipitation = weather_ui_text_label(top,
        APP_THEME_FONT_SMALL);
    lv_obj_set_size(precipitation, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(precipitation,
                                lv_color_hex(WEATHER_COLOR_RAIN), 0);
    lv_label_set_text(precipitation, text);
    (void)snprintf(text, sizeof(text), "湿度 %u%% · %.0f km/h",
                   (unsigned)hour->humidity_percent,
                   hour->wind_speed_tenths_kmh / 10.0);
    lv_obj_t *details = weather_ui_text_label(row, APP_THEME_FONT_SMALL);
    lv_obj_set_width(details, LV_PCT(100));
    lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(details, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_label_set_text(details, text);
}

static void _weather_forecast_render_hourly(weather_forecast_state_t *state,
        lv_obj_t *tile)
{
    state->scroll_owners[WEATHER_FORECAST_HOURLY] = tile;
    if (state->snapshot == NULL || !state->snapshot->hourly.meta.available ||
            state->snapshot->hourly.count == 0U)
    {
        _weather_forecast_empty(tile, "暂无逐小时预报");
        return;
    }
    _weather_forecast_render_chart(state, tile);
    lv_obj_t *list = lv_obj_create(tile);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_color(list, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    app_ui_make_passive(list, true);
    for (uint8_t index = 0U; index < state->snapshot->hourly.count; ++index)
    {
        _weather_forecast_hour_row(list, &state->snapshot->hourly.items[index],
                                   index == 0U);
    }
    state->scroll_owners[WEATHER_FORECAST_HOURLY] = list;
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

static void _weather_forecast_day_row(lv_obj_t *list,
                                      const weather_service_day_t *day,
                                      int16_t full_minimum, int16_t full_maximum)
{
    lv_obj_t *row = weather_ui_container(list, LV_SIZE_CONTENT,
                                         LV_FLEX_FLOW_ROW);
    lv_obj_set_style_min_height(row, 64, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    char text[96];
    (void)snprintf(text, sizeof(text), "%s\n%.5s",
                   _weather_forecast_weekday_text(
                       _weather_forecast_weekday(day->date)),
                   day->date + 5);
    lv_obj_t *date = weather_ui_text_label(row, APP_THEME_FONT_SMALL);
    lv_obj_set_width(date, 62);
    lv_label_set_long_mode(date, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(date, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(date, text);
    (void)weather_ui_small_icon(row, day->day_condition_code);
    lv_obj_t *summary = weather_ui_container(row, LV_SIZE_CONTENT,
        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(summary, 0);
    lv_obj_set_flex_grow(summary, 1);
    lv_obj_set_style_pad_row(summary, 2, 0);
    if (strcmp(day->day_condition_text, day->night_condition_text) == 0)
    {
        (void)snprintf(text, sizeof(text), "%s", day->day_condition_text);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%s / %s",
                       day->day_condition_text, day->night_condition_text);
    }
    lv_obj_t *condition = weather_ui_text_label(summary, APP_THEME_FONT_SMALL);
    lv_obj_set_width(condition, LV_PCT(100));
    lv_label_set_long_mode(condition, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(condition, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(condition, text);
    (void)snprintf(text, sizeof(text), "%.0f° / %.0f°",
                   day->maximum_temperature_tenths_c / 10.0,
                   day->minimum_temperature_tenths_c / 10.0);
    lv_obj_t *range = weather_ui_text_label(summary, APP_THEME_FONT_BODY);
    lv_obj_set_width(range, LV_PCT(100));
    lv_label_set_long_mode(range, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(range, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(range, text);
    (void)snprintf(text, sizeof(text), "雨%.1fmm · UV%u",
                   day->precipitation_tenths_mm / 10.0,
                   (unsigned)day->uv_index);
    lv_obj_t *details = weather_ui_text_label(summary, APP_THEME_FONT_SMALL);
    lv_obj_set_width(details, LV_PCT(100));
    lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(details, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_label_set_text(details, text);
    lv_obj_t *bar = lv_bar_create(summary);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_obj_set_style_pad_top(bar, 3, 0);
    lv_bar_set_range(bar, full_minimum, full_maximum);
    lv_bar_set_mode(bar, LV_BAR_MODE_RANGE);
    lv_bar_set_start_value(bar, day->minimum_temperature_tenths_c, LV_ANIM_OFF);
    lv_bar_set_value(bar, day->maximum_temperature_tenths_c, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(APP_UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(WEATHER_COLOR_SUN),
                              LV_PART_INDICATOR);
    app_ui_make_passive(bar, false);
}

static void _weather_forecast_render_daily(weather_forecast_state_t *state,
        lv_obj_t *tile)
{
    state->scroll_owners[WEATHER_FORECAST_DAILY] = tile;
    if (state->snapshot == NULL || !state->snapshot->daily.meta.available ||
            state->snapshot->daily.count == 0U)
    {
        _weather_forecast_empty(tile, "暂无 7 天预报");
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
    lv_obj_t *list = lv_obj_create(tile);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_color(list, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    app_ui_make_passive(list, true);
    for (uint8_t index = 0U; index < daily->count; ++index)
    {
        _weather_forecast_day_row(list, &daily->items[index], full_minimum,
                                  full_maximum);
    }
    state->scroll_owners[WEATHER_FORECAST_DAILY] = list;
}

static void _weather_forecast_render(weather_forecast_state_t *state)
{
    int32_t restore_y[WEATHER_FORECAST_TILE_COUNT] = {0};
    for (size_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        if (state->scroll_owners[index] == NULL)
        {
            continue;
        }
        restore_y[index] = lv_obj_get_scroll_y(state->scroll_owners[index]);
    }
    for (size_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        state->scroll_owners[index] = NULL;
        if (state->tiles[index] != NULL)
        {
            lv_obj_clean(state->tiles[index]);
        }
    }
    _weather_forecast_render_now(state, state->tiles[0]);
    _weather_forecast_render_hourly(state, state->tiles[1]);
    _weather_forecast_render_daily(state, state->tiles[2]);
    for (size_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        lv_obj_t *owner = state->scroll_owners[index];
        if (owner == NULL)
        {
            continue;
        }
        lv_obj_update_layout(owner);
        if (restore_y[index] > 0)
        {
            lv_obj_scroll_to_y(owner, restore_y[index], LV_ANIM_OFF);
        }
    }
    _weather_forecast_update_service_status(state);

    char subtitle[64] = "和风天气";
    if (state->snapshot != NULL && state->snapshot->current.meta.available)
    {
        char updated[32];
        weather_ui_format_dataset_time(&state->snapshot->current.meta, updated,
                                       sizeof(updated));
        if (updated[0] != '\0')
        {
            (void)snprintf(subtitle, sizeof(subtitle), "%s · %s 更新",
                           WEATHER_FORECAST_SOURCE, updated);
        }
    }
    app_ui_page_set_subtitle(&state->page, subtitle);
}

static void _weather_forecast_update_segments(weather_forecast_state_t *state)
{
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
}

static void _weather_forecast_select_segment(weather_forecast_state_t *state,
        uintptr_t segment)
{
    if (segment >= WEATHER_FORECAST_SEGMENT_COUNT)
    {
        return;
    }
    state->segment = (weather_forecast_segment_t)segment;
    _weather_forecast_update_segments(state);
    if (state->tileview != NULL)
    {
        lv_tileview_set_tile_by_index(state->tileview, (uint32_t)segment, 0,
                                      LV_ANIM_ON);
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
    _weather_forecast_select_segment(state, segment);
}

static void _weather_forecast_tile_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END &&
            lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }
    weather_forecast_state_t *state = lv_event_get_user_data(event);
    if (state->tileview == NULL)
    {
        return;
    }
    lv_obj_t *active = lv_tileview_get_tile_active(state->tileview);
    for (size_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        if (state->tiles[index] == active)
        {
            state->segment = (weather_forecast_segment_t)index;
            _weather_forecast_update_segments(state);
            return;
        }
    }
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
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_style_pad_top(state->page.content, 4, 0);
    lv_obj_set_style_pad_bottom(state->page.content, 4, 0);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_t *segments = weather_ui_container(
                             state->page.content, APP_UI_CHIP_HEIGHT,
                             LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(segments, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(segments, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(segments, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(segments, 4, 0);
    lv_obj_set_style_pad_column(segments, 2, 0);
    static const char *const titles[] = {"实况", "逐小时", "7 天"};
    for (uintptr_t index = 0U; index < WEATHER_FORECAST_SEGMENT_COUNT; ++index)
    {
        lv_obj_t *button = lv_button_create(segments);
        app_ui_click_only(button);
        lv_obj_set_size(button, 0, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(WEATHER_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_TRACK),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(WEATHER_COLOR_SURFACE_HI),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_user_data(button, (void *)index);
        lv_obj_add_event_cb(button, _weather_forecast_segment_event,
                            LV_EVENT_CLICKED, state);
        lv_obj_t *label = weather_ui_text_label(button,
                                                APP_THEME_FONT_SMALL);
        lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_TEXT),
                                    0);
        lv_obj_center(label);
        lv_label_set_text(label, titles[index]);
        state->segment_buttons[index] = button;
    }

    state->tileview = lv_tileview_create(state->page.content);
    lv_obj_set_width(state->tileview, LV_PCT(100));
    lv_obj_set_height(state->tileview, 0);
    lv_obj_set_flex_grow(state->tileview, 1);
    lv_obj_set_scrollbar_mode(state->tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(state->tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(state->tileview, 0, 0);
    lv_obj_set_style_anim_duration(state->tileview, 150, 0);
    lv_obj_add_event_cb(state->tileview, _weather_forecast_tile_event,
                        LV_EVENT_SCROLL_END, state);
    for (uintptr_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        lv_dir_t direction = LV_DIR_NONE;
        if (index == 0U)
        {
            direction = LV_DIR_RIGHT;
        }
        else if (index + 1U < WEATHER_FORECAST_TILE_COUNT)
        {
            direction = (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT);
        }
        else
        {
            direction = LV_DIR_LEFT;
        }
        lv_obj_t *tile = lv_tileview_add_tile(state->tileview,
                                              (uint8_t)index, 0, direction);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_radius(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_pad_row(tile, APP_UI_GAP_TIGHT, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(tile, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
        state->tiles[index] = tile;
    }

    state->service_status_box = weather_ui_container(
                                    state->page.content, 34,
                                    LV_FLEX_FLOW_ROW);
    lv_obj_set_width(state->service_status_box, LV_PCT(100));
    lv_obj_set_style_radius(state->service_status_box,
                            APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_opa(state->service_status_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(state->service_status_box, 10, 0);
    lv_obj_set_style_pad_ver(state->service_status_box, 0, 0);
    lv_obj_set_flex_align(state->service_status_box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *status_glyph = weather_ui_symbol_label(state->service_status_box);
    lv_obj_set_style_text_color(status_glyph,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(status_glyph, LV_SYMBOL_WARNING);
    state->service_status = weather_ui_text_label(state->service_status_box,
        APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->service_status, 0);
    lv_obj_set_flex_grow(state->service_status, 1);
    lv_label_set_long_mode(state->service_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_hidden(state->service_status_box, true);
    _weather_forecast_select_segment(state, (uintptr_t)state->segment);
    _weather_forecast_render(state);
}

static void _weather_forecast_resume(weather_forecast_state_t *state)
{
    if (state->subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
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
    _weather_forecast_refresh(state);
}

static esp_err_t _weather_forecast_pause(weather_forecast_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    weather_ui_release_snapshot(&state->snapshot);
    return result;
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
    weather_ui_release_snapshot(&state->snapshot);
    app_ui_page_destroy(&state->page);
    for (size_t index = 0U; index < WEATHER_FORECAST_SEGMENT_COUNT; ++index)
    {
        state->segment_buttons[index] = NULL;
    }
    for (size_t index = 0U; index < WEATHER_FORECAST_TILE_COUNT; ++index)
    {
        state->tiles[index] = NULL;
        state->scroll_owners[index] = NULL;
    }
    state->tileview = NULL;
    state->service_status_box = NULL;
    state->service_status = NULL;
}

static const app_manager_page_ops_t s_weather_forecast_ops =
{
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
