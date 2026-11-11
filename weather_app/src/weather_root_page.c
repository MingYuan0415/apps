#define DBG_TAG "weather_root"
#include "mt_log.h"

#include "weather_app_internal.h"

#include <stdio.h>
#include <string.h>

#define WEATHER_ROOT_METRIC_COUNT 3U
#define WEATHER_ROOT_HERO_MIN_HEIGHT 160
#define WEATHER_ROOT_DETAIL_HEIGHT 52

typedef struct weather_root_state
{
    app_ui_page_t page;
    lv_obj_t *city_label;
    lv_obj_t *status_label;
    lv_obj_t *hero;
    lv_obj_t *temperature_label;
    lv_obj_t *condition_label;
    lv_obj_t *range_label;
    lv_obj_t *main_image;
    lv_obj_t *image_fallback;
    lv_obj_t *alert_button;
    lv_obj_t *alert_label;
    lv_obj_t *calm_pill;
    lv_obj_t *calm_glyph;
    lv_obj_t *calm_label;
    lv_obj_t *metric_values[WEATHER_ROOT_METRIC_COUNT];
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
    int32_t last_temperature_tenths;
    bool has_temperature;
} weather_root_state_t;

_Static_assert(sizeof(weather_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Weather root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_service_event_t) <= 256U,
               "Weather event exceeds the Event Bus payload limit");

static void _weather_root_open_forecast(lv_event_t *event);
static void _weather_root_open_alerts(lv_event_t *event);

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

static weather_mood_t _weather_root_mood(
    const weather_service_snapshot_t *snapshot)
{
    if (snapshot == NULL || !snapshot->current.meta.available ||
            snapshot->current.meta.stale || snapshot->current.meta.expired)
    {
        return WEATHER_MOOD_NEUTRAL;
    }
    return weather_ui_mood(snapshot->current.condition_code);
}

static void _weather_root_opa_animation(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void _weather_root_scale_animation(void *object, int32_t value)
{
    lv_image_set_scale((lv_obj_t *)object, (uint32_t)value);
}

static void _weather_root_pulse(weather_root_state_t *state, bool has_image)
{
    lv_obj_t *target = has_image ? state->main_image : state->image_fallback;
    if (target == NULL)
    {
        return;
    }
    (void)lv_anim_delete(target, NULL);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, target);
    lv_anim_set_duration(&animation, 150U);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    if (has_image)
    {
        lv_image_set_scale(state->main_image, 230U);
        lv_anim_set_values(&animation, 230, 256);
        lv_anim_set_exec_cb(&animation, _weather_root_scale_animation);
    }
    else
    {
        lv_obj_set_style_opa(state->image_fallback, LV_OPA_60, 0);
        lv_anim_set_values(&animation, LV_OPA_60, LV_OPA_COVER);
        lv_anim_set_exec_cb(&animation, _weather_root_opa_animation);
    }
    (void)lv_anim_start(&animation);
}

static void _weather_root_set_temperature(weather_root_state_t *state,
        int32_t tenths, bool animate)
{
    char text[16];
    (void)snprintf(text, sizeof(text), "%.0f°", tenths / 10.0);
    const bool changed = state->has_temperature &&
                         state->last_temperature_tenths != tenths;
    lv_label_set_text(state->temperature_label, text);
    if (animate && changed)
    {
        (void)lv_anim_delete(state->temperature_label, NULL);
        lv_obj_set_style_opa(state->temperature_label, LV_OPA_60, 0);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, state->temperature_label);
        lv_anim_set_values(&animation, LV_OPA_60, LV_OPA_COVER);
        lv_anim_set_duration(&animation, 150U);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&animation, _weather_root_opa_animation);
        (void)lv_anim_start(&animation);
    }
    state->last_temperature_tenths = tenths;
    state->has_temperature = true;
}

static void _weather_root_create_metric(lv_obj_t *strip, const char *name,
                                        lv_obj_t **value_label)
{
    lv_obj_t *cell = weather_ui_container(strip, LV_SIZE_CONTENT,
                                          LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(cell, 0);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_pad_hor(cell, 2, 0);
    lv_obj_set_style_pad_row(cell, 0, 0);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = weather_ui_text_label(cell, APP_THEME_FONT_SMALL);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_label_set_text(label, name);
    lv_obj_t *value = weather_ui_text_label(cell, APP_THEME_FONT_SMALL);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(value, "--");
    *value_label = value;
}

static void _weather_root_create_metrics(weather_root_state_t *state,
        lv_obj_t *parent)
{
    lv_obj_t *strip = weather_ui_surface(parent, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(strip, 8, 0);
    lv_obj_set_style_pad_column(strip, 4, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const names[WEATHER_ROOT_METRIC_COUNT] =
    {
        "湿度", "降水", "风速",
    };
    for (size_t index = 0U; index < WEATHER_ROOT_METRIC_COUNT; ++index)
    {
        _weather_root_create_metric(strip, names[index],
                                    &state->metric_values[index]);
    }
}

static void _weather_root_create_hero(weather_root_state_t *state,
                                      lv_obj_t *parent)
{
    lv_obj_t *hero = lv_obj_create(parent);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(hero, WEATHER_ROOT_HERO_MIN_HEIGHT, 0);
    lv_obj_set_style_radius(hero, APP_UI_RADIUS_SHEET, 0);
    lv_obj_set_style_bg_color(hero, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(hero, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_row(hero, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    app_ui_make_passive(hero, false);
    state->hero = hero;

    lv_obj_t *summary = weather_ui_container(hero, LV_SIZE_CONTENT,
        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_style_pad_right(summary, 116, 0);
    lv_obj_set_style_pad_row(summary, 0, 0);
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    state->temperature_label = weather_ui_text_label(summary,
        APP_THEME_FONT_TITLE);
    lv_obj_set_style_text_color(state->temperature_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(state->temperature_label, "--°");
    state->condition_label = weather_ui_text_label(summary,
        APP_THEME_FONT_HEAD);
    lv_obj_set_width(state->condition_label, LV_PCT(100));
    lv_label_set_long_mode(state->condition_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->condition_label,
                                lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(state->condition_label, "暂无天气");

    lv_obj_t *visual = lv_obj_create(hero);
    lv_obj_remove_style_all(visual);
    lv_obj_set_size(visual, 112, 112);
    lv_obj_set_ignore_layout(visual, true);
    lv_obj_align(visual, LV_ALIGN_TOP_RIGHT, 0, 0);
    app_ui_make_passive(visual, false);
    lv_obj_t *disc = lv_obj_create(visual);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 112, 112);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(WEATHER_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, 0);
    app_ui_make_passive(disc, false);
    state->main_image = lv_image_create(visual);
    lv_obj_set_size(state->main_image, 112, 112);
    lv_obj_align(state->main_image, LV_ALIGN_CENTER, 0, 0);
    app_ui_make_passive(state->main_image, false);
    state->image_fallback = weather_ui_symbol_label(visual);
    lv_obj_set_style_text_color(state->image_fallback,
                                lv_color_hex(WEATHER_COLOR_SUN), 0);
    lv_obj_align(state->image_fallback, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(state->image_fallback, true);
    lv_label_set_text(state->image_fallback, LV_SYMBOL_IMAGE);

    state->range_label = weather_ui_text_label(hero, APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->range_label, LV_PCT(100));
    lv_label_set_long_mode(state->range_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->range_label,
                                lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(state->range_label, "体感--°  高--°  低--°");
}

static void _weather_root_create_alert(weather_root_state_t *state,
                                       lv_obj_t *parent)
{
    /* Tinted capsule with a leading glyph so the alert never relies on hue. */
    state->alert_button = lv_button_create(parent);
    app_ui_click_only(state->alert_button);
    lv_obj_set_size(state->alert_button, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(state->alert_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->alert_button,
                              lv_color_hex(APP_UI_COLOR_WARNING_TINT), 0);
    lv_obj_set_style_bg_color(state->alert_button,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(state->alert_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->alert_button, 0, 0);
    lv_obj_set_style_shadow_width(state->alert_button, 0, 0);
    lv_obj_set_style_pad_hor(state->alert_button, 16, 0);
    lv_obj_set_style_pad_ver(state->alert_button, 14, 0);
    lv_obj_set_style_min_height(state->alert_button, 52, 0);
    lv_obj_set_style_pad_column(state->alert_button, 10, 0);
    lv_obj_set_flex_flow(state->alert_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->alert_button, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *glyph = weather_ui_symbol_label(state->alert_button);
    lv_obj_set_style_text_color(glyph, lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(glyph, LV_SYMBOL_WARNING);
    lv_obj_add_event_cb(state->alert_button, _weather_root_open_alerts,
                        LV_EVENT_CLICKED, NULL);
    state->alert_label = weather_ui_text_label(state->alert_button,
        APP_THEME_FONT_SMALL);
    lv_label_set_text(state->alert_label, "");
    lv_obj_set_width(state->alert_label, 0);
    lv_obj_set_flex_grow(state->alert_label, 1);
    lv_label_set_long_mode(state->alert_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->alert_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_obj_t *chevron = weather_ui_symbol_label(state->alert_button);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
}

static void _weather_root_create_calm(weather_root_state_t *state,
                                      lv_obj_t *parent)
{
    /* Keep the alert slot occupied when there is nothing to warn about. This
     * prevents the hero from stretching into a large empty card and keeps the
     * page geometry stable across alert/no-alert refreshes. */
    lv_obj_t *pill = weather_ui_container(parent, 52, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(APP_UI_COLOR_SUCCESS_TINT), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(pill, 16, 0);
    lv_obj_set_style_pad_column(pill, 10, 0);
    lv_obj_set_style_pad_ver(pill, 0, 0);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    state->calm_pill = pill;
    state->calm_glyph = weather_ui_symbol_label(pill);
    lv_obj_set_style_text_color(state->calm_glyph,
                                lv_color_hex(APP_UI_COLOR_SUCCESS), 0);
    lv_label_set_text(state->calm_glyph, LV_SYMBOL_OK);
    state->calm_label = weather_ui_text_label(pill, APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->calm_label, 0);
    lv_obj_set_flex_grow(state->calm_label, 1);
    lv_label_set_long_mode(state->calm_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->calm_label,
                                lv_color_hex(APP_UI_COLOR_SUCCESS), 0);
    lv_label_set_text(state->calm_label, "当前无气象预警");
}

static void _weather_root_create_detail(lv_obj_t *parent)
{
    lv_obj_t *detail = lv_button_create(parent);
    app_ui_click_only(detail);
    lv_obj_set_size(detail, LV_PCT(100), WEATHER_ROOT_DETAIL_HEIGHT);
    lv_obj_set_style_radius(detail, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(detail, lv_color_hex(WEATHER_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_color(detail, lv_color_hex(APP_UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(detail, 0, 0);
    lv_obj_set_style_shadow_width(detail, 0, 0);
    lv_obj_set_style_pad_column(detail, 8, 0);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detail, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(detail, _weather_root_open_forecast,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *text = weather_ui_text_label(detail, APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(text, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(text, "查看详细预报");
    lv_obj_t *chevron = weather_ui_symbol_label(detail);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
}

static void _weather_root_render_metrics(weather_root_state_t *state)
{
    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        for (size_t index = 0U; index < WEATHER_ROOT_METRIC_COUNT; ++index)
        {
            lv_label_set_text(state->metric_values[index], "--");
        }
        return;
    }
    const weather_service_current_t *current = &state->snapshot->current;
    char humidity[32];
    char precipitation[32];
    char wind[32];
    (void)snprintf(humidity, sizeof(humidity), "%u%%",
                   (unsigned)current->humidity_percent);
    (void)snprintf(precipitation, sizeof(precipitation), "%.1fmm",
                   current->precipitation_tenths_mm / 10.0);
    (void)snprintf(wind, sizeof(wind), "%.0fkm/h",
                   current->wind_speed_tenths_kmh / 10.0);
    lv_label_set_text(state->metric_values[0], humidity);
    lv_label_set_text(state->metric_values[1], precipitation);
    lv_label_set_text(state->metric_values[2], wind);
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
        char text[48];
        weather_ui_format_dataset_time(meta, updated, sizeof(updated));
        if (updated[0] != '\0')
        {
            (void)snprintf(text, sizeof(text), "%s 更新", updated);
            app_ui_set_status_text(state->status_label, text,
                                   APP_UI_STATUS_NEUTRAL);
            return;
        }
    }
    char text[64] = "";
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

    const weather_mood_t mood = _weather_root_mood(state->snapshot);
    lv_obj_set_style_bg_color(state->hero, lv_color_hex(
                                  weather_ui_mood_tint(mood)), 0);
    lv_obj_set_style_text_color(state->temperature_label,
                                lv_color_hex(mood == WEATHER_MOOD_NEUTRAL ?
                                        WEATHER_COLOR_TEXT :
                                        weather_ui_mood_accent(mood)), 0);

    if (state->snapshot == NULL || !state->snapshot->current.meta.available)
    {
        state->has_temperature = false;
        lv_label_set_text(state->temperature_label, "--°");
        lv_label_set_text(state->condition_label, "暂无天气");
        lv_label_set_text(state->range_label, "体感--°  高--°  低--°");
        if (weather_ui_set_image(state->main_image, 0U, false))
        {
            lv_obj_set_hidden(state->image_fallback, true);
        }
        else
        {
            lv_obj_set_hidden(state->image_fallback, false);
        }
    }
    else
    {
        const weather_service_snapshot_t *snapshot = state->snapshot;
        char text[128];
        _weather_root_set_temperature(state,
                                      snapshot->current.temperature_tenths_c,
                                      true);
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
        const bool has_image = weather_ui_set_image(state->main_image,
            snapshot->current.condition_code, false);
        lv_obj_set_hidden(state->image_fallback, has_image);
        _weather_root_pulse(state, has_image);
    }
    _weather_root_render_metrics(state);

    const bool alerts_available = state->snapshot != NULL &&
                                  state->snapshot->alerts.meta.available;
    const bool has_alerts = alerts_available &&
                            state->snapshot->alerts.count > 0U;
    if (has_alerts)
    {
        char text[64];
        (void)snprintf(text, sizeof(text), "共 %u 条气象预警%s",
                       (unsigned)state->snapshot->alerts.count,
                       state->snapshot->alerts.meta.expired ? " · 已过期" :
                       (state->snapshot->alerts.meta.stale ? " · 缓存" : ""));
        lv_label_set_text(state->alert_label, text);
    }
    lv_obj_set_hidden(state->alert_button, !has_alerts);
    lv_obj_set_hidden(state->calm_pill, has_alerts);
    if (!has_alerts)
    {
        const bool clear = alerts_available;
        lv_label_set_text(state->calm_label,
                          clear ? "当前无气象预警" : "预警数据暂不可用");
        lv_label_set_text(state->calm_glyph,
                          clear ? LV_SYMBOL_OK : LV_SYMBOL_MINUS);
        const uint32_t color = clear ? APP_UI_COLOR_SUCCESS :
                               APP_UI_COLOR_MUTED;
        const uint32_t tint = clear ? APP_UI_COLOR_SUCCESS_TINT :
                              APP_UI_COLOR_SURFACE;
        lv_obj_set_style_bg_color(state->calm_pill, lv_color_hex(tint), 0);
        lv_obj_set_style_text_color(state->calm_glyph, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(state->calm_label, lv_color_hex(color), 0);
    }
}

static void _weather_root_refresh_snapshot(weather_root_state_t *state)
{
    weather_ui_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_root_render(state);
}

/* Single retained Weather root instance; the pull state lives outside the page
 * arena to keep the slot under APP_MANAGER_PAGE_STATE_BYTES. */
static app_ui_pull_refresh_t s_weather_root_pull;

static void _weather_root_request_refresh(weather_root_state_t *state)
{
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
        app_ui_pull_refresh_set_refreshing(&s_weather_root_pull, false);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "刷新请求失败",
                               APP_UI_STATUS_ERROR);
        app_ui_pull_refresh_set_refreshing(&s_weather_root_pull, false);
    }
}

static void _weather_root_pull_refresh(void *user_data)
{
    _weather_root_request_refresh(user_data);
}

static void _weather_root_refresh_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    _weather_root_request_refresh(lv_event_get_user_data(event));
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
        app_ui_pull_refresh_set_refreshing(&s_weather_root_pull, false);
    }
}

static lv_obj_t *_weather_root_header_button(weather_root_state_t *state)
{
    return app_ui_page_set_action(&state->page, APP_IMAGE_UI_REFRESH,
                                  LV_SYMBOL_REFRESH, _weather_root_refresh_event,
                                  state);
}

static void _weather_root_build(weather_root_state_t *state)
{
    app_ui_page_create(&state->page, "天气", true);
    app_ui_page_set_subtitle(&state->page, "当前位置");
    state->city_label = state->page.title;
    lv_label_set_long_mode(state->city_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    state->status_label = state->page.subtitle;
    lv_obj_set_width(state->status_label, LV_PCT(100));
    (void)_weather_root_header_button(state);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_bottom(state->page.content, 4, 0);

    _weather_root_create_hero(state, state->page.content);
    _weather_root_create_alert(state, state->page.content);
    _weather_root_create_calm(state, state->page.content);
    _weather_root_create_metrics(state, state->page.content);
    _weather_root_create_detail(state->page.content);

    _weather_root_render(state);
    app_ui_pull_refresh_attach(&s_weather_root_pull, &state->page,
                               _weather_root_pull_refresh, state);
}

static void _weather_root_resume(weather_root_state_t *state)
{
    app_ui_pull_refresh_resume(&s_weather_root_pull);
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
    _weather_root_refresh_snapshot(state);
}

static esp_err_t _weather_root_pause(weather_root_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    app_ui_pull_refresh_pause(&s_weather_root_pull);
    weather_ui_release_snapshot(&state->snapshot);
    return result;
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
    (void)lv_anim_delete(state->temperature_label, NULL);
    (void)lv_anim_delete(state->main_image, NULL);
    (void)lv_anim_delete(state->image_fallback, NULL);
    app_ui_pull_refresh_detach(&s_weather_root_pull);
    weather_ui_release_snapshot(&state->snapshot);
    app_ui_page_destroy(&state->page);
    state->city_label = NULL;
    state->status_label = NULL;
    state->hero = NULL;
    state->temperature_label = NULL;
    state->condition_label = NULL;
    state->range_label = NULL;
    state->main_image = NULL;
    state->image_fallback = NULL;
    state->alert_button = NULL;
    state->alert_label = NULL;
    state->calm_pill = NULL;
    state->calm_glyph = NULL;
    state->calm_label = NULL;
    for (size_t index = 0U; index < WEATHER_ROOT_METRIC_COUNT; ++index)
    {
        state->metric_values[index] = NULL;
    }
}

static const app_manager_page_ops_t s_weather_root_ops =
{
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
