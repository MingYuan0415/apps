#define DBG_TAG "clock_stopwatch"
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>

typedef struct clock_stopwatch_state
{
    app_ui_page_t page;
    lv_obj_t *display_card;
    lv_obj_t *value_label;
    lv_obj_t *ms_label;
    lv_obj_t *hint_label;
    lv_obj_t *minute_bar;
    lv_obj_t *last_lap_label;
    lv_obj_t *lap_summary;
    lv_obj_t *btn_primary;
    lv_obj_t *btn_lap;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    uint64_t lap_start_ms;
    uint64_t last_lap_ms;
    uint64_t fastest_lap_ms;
    uint32_t lap_count;
    uint32_t last_minute_progress;
    char last_value[12];
    char last_ms[6];
    timer_service_state_t last_state;
    bool state_valid;
} clock_stopwatch_state_t;

_Static_assert(sizeof(clock_stopwatch_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Stopwatch page state exceeds the lifecycle arena slot");

static void _stopwatch_format_lap(uint64_t milliseconds, char *output,
                                  size_t output_size)
{
    const uint32_t minutes = (uint32_t)(milliseconds / 60000U);
    const uint32_t seconds = (uint32_t)((milliseconds / 1000U) % 60U);
    const uint32_t hundredths = (uint32_t)((milliseconds / 10U) % 100U);
    (void)snprintf(output, output_size, "%02u:%02u.%02u",
                   (unsigned)minutes, (unsigned)seconds,
                   (unsigned)hundredths);
}

static void _stopwatch_render_laps(clock_stopwatch_state_t *state)
{
    char text[80];
    if (state->lap_count == 0U)
    {
        lv_label_set_text(state->last_lap_label, "还没有分段");
        lv_label_set_text(state->lap_summary, "开始后可记录每一圈");
        return;
    }
    char last[16];
    char fastest[16];
    _stopwatch_format_lap(state->last_lap_ms, last, sizeof(last));
    (void)snprintf(text, sizeof(text), "最近 %s", last);
    lv_label_set_text(state->last_lap_label, text);
    _stopwatch_format_lap(state->fastest_lap_ms, fastest, sizeof(fastest));
    (void)snprintf(text, sizeof(text), "第 %u 段 · 最快 %s",
                   (unsigned)state->lap_count, fastest);
    lv_label_set_text(state->lap_summary, text);
}

static void _stopwatch_render(clock_stopwatch_state_t *state)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    const timer_service_state_t view = snapshot.stopwatch_state;
    char text[12];
    clock_ui_format_mmss((uint32_t)(snapshot.stopwatch_elapsed_ms / 1000U *
                                    1000U), text, sizeof(text));
    if (strcmp(state->last_value, text) != 0)
    {
        (void)snprintf(state->last_value, sizeof(state->last_value), "%s",
                       text);
        lv_label_set_text(state->value_label, text);
    }
    char ms[6];
    (void)snprintf(ms, sizeof(ms), ".%03u",
                   (unsigned)(snapshot.stopwatch_elapsed_ms % 1000U));
    if (strcmp(state->last_ms, ms) != 0)
    {
        (void)snprintf(state->last_ms, sizeof(state->last_ms), "%s", ms);
        lv_label_set_text(state->ms_label, ms);
    }
    const uint32_t minute_progress =
        (uint32_t)((snapshot.stopwatch_elapsed_ms % 60000ULL) / 600U);
    if (state->last_minute_progress != minute_progress)
    {
        state->last_minute_progress = minute_progress;
        lv_bar_set_value(state->minute_bar, (int32_t)minute_progress,
                         LV_ANIM_OFF);
    }
    if (state->refresh_timer != NULL)
    {
        lv_timer_set_period(state->refresh_timer,
                            view == TIMER_SERVICE_RUNNING ? 50U : 250U);
    }
    if (!state->state_valid || state->last_state != view)
    {
        state->state_valid = true;
        state->last_state = view;
        switch (view)
        {
        case TIMER_SERVICE_RUNNING:
            lv_label_set_text(state->hint_label, "正在计时");
            app_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_set_hidden(state->btn_lap, false);
            lv_obj_set_hidden(state->btn_reset, false);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_TEXT),
                                        0);
            break;
        case TIMER_SERVICE_PAUSED:
            lv_label_set_text(state->hint_label, "已暂停");
            app_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_set_hidden(state->btn_lap, true);
            lv_obj_set_hidden(state->btn_reset, false);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_MUTED),
                                        0);
            break;
        default:
            lv_label_set_text(state->hint_label, "轻触开始计时");
            app_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_set_hidden(state->btn_lap, true);
            lv_obj_set_hidden(state->btn_reset, true);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_TEXT),
                                        0);
            break;
        }
        clock_ui_pulse(state->display_card);
    }
}

static void _stopwatch_refresh(lv_timer_t *timer)
{
    _stopwatch_render(lv_timer_get_user_data(timer));
}

static void _stopwatch_primary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_stopwatch_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "状态读取失败");
        return;
    }
    esp_err_t result = ESP_OK;
    if (snapshot.stopwatch_state == TIMER_SERVICE_RUNNING)
    {
        result = timer_service_stopwatch_pause();
    }
    else
    {
        result = timer_service_stopwatch_start();
    }
    if (result != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "操作失败");
    }
    _stopwatch_render(state);
}

static void _stopwatch_lap_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_stopwatch_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.stopwatch_state != TIMER_SERVICE_RUNNING)
    {
        return;
    }
    const uint64_t elapsed = snapshot.stopwatch_elapsed_ms;
    state->last_lap_ms = elapsed - state->lap_start_ms;
    state->lap_start_ms = elapsed;
    ++state->lap_count;
    if (state->lap_count == 1U || state->last_lap_ms < state->fastest_lap_ms)
    {
        state->fastest_lap_ms = state->last_lap_ms;
    }
    _stopwatch_render_laps(state);
    clock_ui_pulse(state->last_lap_label);
}

static void _stopwatch_reset_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_stopwatch_state_t *state = lv_event_get_user_data(event);
    (void)timer_service_stopwatch_reset();
    state->lap_start_ms = 0U;
    state->last_lap_ms = 0U;
    state->fastest_lap_ms = 0U;
    state->lap_count = 0U;
    state->last_minute_progress = UINT32_MAX;
    _stopwatch_render_laps(state);
    _stopwatch_render(state);
}

static void _stopwatch_mount(const app_manager_page_context_t *context)
{
    clock_stopwatch_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->last_minute_progress = UINT32_MAX;
    app_ui_page_create(&state->page, "秒表", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_bottom(state->page.content, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    state->display_card = clock_ui_card_create(state->page.content);
    lv_obj_set_height(state->display_card, 0);
    lv_obj_set_flex_grow(state->display_card, 1);
    lv_obj_set_style_min_height(state->display_card, 200, 0);
    lv_obj_set_style_pad_all(state->display_card, 12, 0);
    lv_obj_set_style_pad_row(state->display_card, 4, 0);

    state->hint_label = clock_ui_label_create(state->display_card,
        "轻触开始计时", APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);

    lv_obj_t *value_row = lv_obj_create(state->display_card);
    lv_obj_remove_style_all(value_row);
    lv_obj_set_width(value_row, LV_SIZE_CONTENT);
    lv_obj_set_height(value_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(value_row, 2, 0);
    app_ui_make_passive(value_row, false);

    state->value_label = lv_label_create(value_row);
    lv_obj_set_style_text_color(state->value_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->value_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    lv_label_set_text(state->value_label, "00:00");
    app_ui_make_passive(state->value_label, false);

    state->ms_label = lv_label_create(value_row);
    lv_obj_set_style_text_color(state->ms_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->ms_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_obj_set_style_pad_bottom(state->ms_label, 8, 0);
    lv_label_set_text(state->ms_label, ".000");
    app_ui_make_passive(state->ms_label, false);

    lv_obj_t *rail = lv_obj_create(state->display_card);
    lv_obj_remove_style_all(rail);
    lv_obj_set_width(rail, LV_PCT(100));
    lv_obj_set_height(rail, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rail, 4, 0);
    app_ui_make_passive(rail, false);

    state->minute_bar = lv_bar_create(rail);
    lv_obj_set_width(state->minute_bar, LV_PCT(100));
    lv_obj_set_height(state->minute_bar, 5);
    lv_bar_set_range(state->minute_bar, 0, 100);
    lv_bar_set_value(state->minute_bar, 0, LV_ANIM_OFF);
    lv_bar_set_mode(state->minute_bar, LV_BAR_MODE_RANGE);
    lv_obj_set_style_radius(state->minute_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->minute_bar,
                              lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_set_style_bg_opa(state->minute_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->minute_bar, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->minute_bar,
                              lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(state->minute_bar, LV_OPA_COVER,
                            LV_PART_INDICATOR);
    app_ui_make_passive(state->minute_bar, false);

    lv_obj_t *scale = lv_obj_create(rail);
    lv_obj_remove_style_all(scale);
    lv_obj_set_width(scale, LV_PCT(100));
    lv_obj_set_height(scale, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scale, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(scale, false);
    lv_obj_t *left_scale = clock_ui_label_create(scale, "0 秒",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_FAINT);
    lv_obj_set_width(left_scale, LV_SIZE_CONTENT);
    lv_obj_t *right_scale = clock_ui_label_create(scale, "60 秒",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_FAINT);
    lv_obj_set_width(right_scale, LV_SIZE_CONTENT);

    lv_obj_t *laps = clock_ui_card_create(state->page.content);
    lv_obj_set_height(laps, 72);
    lv_obj_set_style_pad_all(laps, 10, 0);
    lv_obj_set_style_pad_row(laps, 0, 0);
    state->last_lap_label = clock_ui_label_create(laps, "还没有分段",
        APP_THEME_FONT_HEAD, APP_UI_COLOR_TEXT);
    state->lap_summary = clock_ui_label_create(laps, "开始后可记录每一圈",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->btn_primary = app_ui_button_create(controls, "开始",
        _stopwatch_primary_event, state);
    app_ui_button_set_kind(state->btn_primary, APP_UI_BUTTON_PRIMARY);
    state->btn_lap = app_ui_button_create(controls, "分段",
                                          _stopwatch_lap_event, state);
    state->btn_reset = app_ui_button_create(controls, "重置",
                                            _stopwatch_reset_event, state);
    lv_obj_set_hidden(state->btn_lap, true);
    lv_obj_set_hidden(state->btn_reset, true);

    state->refresh_timer = app_ui_timer_create_passive(_stopwatch_refresh, 250U, state);
    _stopwatch_render_laps(state);
    _stopwatch_render(state);
}

static esp_err_t _stopwatch_pause(const app_manager_page_context_t *context)
{
    clock_stopwatch_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _stopwatch_resume(const app_manager_page_context_t *context)
{
    clock_stopwatch_state_t *state = context->state;
    state->last_state = TIMER_SERVICE_IDLE;
    state->state_valid = false;
    state->last_value[0] = '\0';
    state->last_ms[0] = '\0';
    state->last_minute_progress = UINT32_MAX;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _stopwatch_render(state);
}

static void _stopwatch_unmount(const app_manager_page_context_t *context)
{
    clock_stopwatch_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->display_card = NULL;
    state->value_label = NULL;
    state->ms_label = NULL;
    state->hint_label = NULL;
    state->minute_bar = NULL;
    state->last_lap_label = NULL;
    state->lap_summary = NULL;
    state->btn_primary = NULL;
    state->btn_lap = NULL;
    state->btn_reset = NULL;
}

static const app_manager_page_ops_t s_clock_stopwatch_ops =
{
    .mount = _stopwatch_mount,
    .resume = _stopwatch_resume,
    .pause = _stopwatch_pause,
    .unmount = _stopwatch_unmount,
};

const app_manager_page_definition_t clock_stopwatch_page_definition =
{
    .ops = &s_clock_stopwatch_ops,
    .memory_size = sizeof(clock_stopwatch_state_t),
};
