#define DBG_TAG "clock_countdown"
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>

static const uint32_t k_presets[4] = {1U, 5U, 10U, 25U};
static const char *const k_preset_text[4] = {"1 分", "5 分", "10 分",
                                             "25 分"
                                            };

typedef struct clock_countdown_state
{
    app_ui_page_t page;
    lv_obj_t *ring;
    lv_obj_t *value_label;
    lv_obj_t *phase_label;
    lv_obj_t *hint_label;
    lv_obj_t *detail_label;
    lv_obj_t *chips[5];
    lv_obj_t *btn_primary;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    uint32_t minutes;
    uint32_t last_minutes;
    uint32_t last_span;
    uint32_t last_color;
    uint32_t last_value_font;
    char last_value[12];
    char last_phase[16];
    char last_detail[32];
    timer_service_state_t last_state;
    bool state_valid;
} clock_countdown_state_t;

_Static_assert(sizeof(clock_countdown_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Countdown page state exceeds the lifecycle arena slot");

static void _countdown_set_text(lv_obj_t *label, char *cache, size_t cache_size,
                                const char *text)
{
    if (strcmp(cache, text) == 0)
    {
        return;
    }
    (void)snprintf(cache, cache_size, "%s", text);
    lv_label_set_text(label, text);
}

static void _countdown_expected_end(uint32_t remaining_ms, char *output,
                                    size_t output_size)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        (void)snprintf(output, output_size, "等待有效时间");
        return;
    }
    uint32_t seconds = (uint32_t)local_time.tm_hour * 3600U +
                       (uint32_t)local_time.tm_min * 60U +
                       (uint32_t)local_time.tm_sec +
                       (remaining_ms + 999U) / 1000U;
    seconds %= 86400U;
    (void)snprintf(output, output_size, "预计 %02u:%02u 结束",
                   (unsigned)(seconds / 3600U),
                   (unsigned)((seconds / 60U) % 60U));
}

static void _countdown_render(clock_countdown_state_t *state)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    const timer_service_state_t view = snapshot.countdown_state;
    const uint32_t remaining = view == TIMER_SERVICE_COMPLETED ?
                               snapshot.countdown_duration_ms :
                               snapshot.countdown_remaining_ms;
    const uint32_t display_ms = view == TIMER_SERVICE_IDLE ?
                                state->minutes * 60000U : remaining;
    const app_theme_font_id_t value_font = display_ms >= 100U * 60000U ?
                                           APP_THEME_FONT_HEAD :
                                           APP_THEME_FONT_HUGE;
    if (state->last_value_font != (uint32_t)value_font)
    {
        state->last_value_font = (uint32_t)value_font;
        lv_obj_set_style_text_font(state->value_label,
                                   app_ui_font(value_font), 0);
    }
    char text[32];
    clock_ui_format_mmss(display_ms, text, sizeof(text));
    _countdown_set_text(state->value_label, state->last_value,
                        sizeof(state->last_value), text);

    const uint32_t color = view == TIMER_SERVICE_PAUSED ? APP_UI_COLOR_MUTED :
                           (view == TIMER_SERVICE_COMPLETED ?
                            APP_UI_COLOR_SUN : APP_UI_COLOR_RAIN);
    const bool active = view != TIMER_SERVICE_IDLE;
    if (state->last_span != remaining || state->last_color != color)
    {
        state->last_span = remaining;
        state->last_color = color;
        clock_ui_ring_update(state->ring, active, remaining,
                             snapshot.countdown_duration_ms, color);
    }
    if (state->last_minutes != state->minutes)
    {
        state->last_minutes = state->minutes;
        bool preset_hit = false;
        for (size_t index = 0U; index < 4U; ++index)
        {
            const bool selected = state->minutes == k_presets[index];
            preset_hit = preset_hit || selected;
            app_ui_chip_set_selected(state->chips[index], selected);
        }
        app_ui_chip_set_selected(state->chips[4], !preset_hit);
    }

    const char *phase = "准备";
    if (view == TIMER_SERVICE_RUNNING)
    {
        phase = "进行中";
    }
    else if (view == TIMER_SERVICE_PAUSED)
    {
        phase = "暂停";
    }
    else if (view == TIMER_SERVICE_COMPLETED)
    {
        phase = "完成";
    }
    _countdown_set_text(state->phase_label, state->last_phase,
                        sizeof(state->last_phase), phase);

    if (view == TIMER_SERVICE_PAUSED)
    {
        (void)snprintf(text, sizeof(text), "剩余 %u 分钟",
                       (unsigned)((remaining + 59999U) / 60000U));
    }
    else if (view == TIMER_SERVICE_COMPLETED)
    {
        (void)snprintf(text, sizeof(text), "本次 %u 分钟",
                       (unsigned)(snapshot.countdown_duration_ms / 60000U));
    }
    else
    {
        _countdown_expected_end(display_ms, text, sizeof(text));
    }
    _countdown_set_text(state->detail_label, state->last_detail,
                        sizeof(state->last_detail), text);

    if (!state->state_valid || state->last_state != view)
    {
        state->state_valid = true;
        state->last_state = view;
        switch (view)
        {
        case TIMER_SERVICE_IDLE:
            lv_label_set_text(state->hint_label, "选择时长后开始");
            app_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_set_hidden(state->btn_reset, true);
            break;
        case TIMER_SERVICE_RUNNING:
            lv_label_set_text(state->hint_label, "倒计时进行中");
            app_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_set_hidden(state->btn_reset, false);
            break;
        case TIMER_SERVICE_PAUSED:
            lv_label_set_text(state->hint_label, "已暂停");
            app_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_set_hidden(state->btn_reset, false);
            break;
        default:
            lv_label_set_text(state->hint_label, "时间到");
            app_ui_button_set_text(state->btn_primary, "重新开始");
            lv_obj_set_hidden(state->btn_reset, false);
            break;
        }
        clock_ui_pulse(state->ring);
    }
}

static void _countdown_refresh(lv_timer_t *timer)
{
    _countdown_render(lv_timer_get_user_data(timer));
}

static void _countdown_chip_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_countdown_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "状态读取失败");
        return;
    }
    if (snapshot.countdown_state == TIMER_SERVICE_RUNNING ||
            snapshot.countdown_state == TIMER_SERVICE_PAUSED)
    {
        (void)app_ui_toast_show(state->page.root, "运行中不能切换时长");
        return;
    }
    const bool was_completed = snapshot.countdown_state ==
                               TIMER_SERVICE_COMPLETED;
    if (was_completed && timer_service_countdown_reset() != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "操作失败");
        return;
    }
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index == 4U)
    {
        clock_ui_open_page_with_minutes(CLOCK_PAGE_DURATION,
                                        state->minutes);
        return;
    }
    if (index < 4U)
    {
        state->minutes = k_presets[index];
        clock_ui_minutes_set(state->minutes);
    }
    _countdown_render(state);
    if (!was_completed)
    {
        clock_ui_pulse(state->value_label);
    }
}

static void _countdown_primary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_countdown_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "状态读取失败");
        return;
    }
    esp_err_t result = ESP_OK;
    switch (snapshot.countdown_state)
    {
    case TIMER_SERVICE_RUNNING:
        result = timer_service_countdown_pause();
        break;
    case TIMER_SERVICE_PAUSED:
        result = timer_service_countdown_resume();
        break;
    default:
        result = timer_service_countdown_start(state->minutes * 60000U);
        break;
    }
    if (result == ESP_ERR_INVALID_STATE)
    {
        (void)app_ui_toast_show(state->page.root, "专注进行中，无法开始倒计时");
        return;
    }
    if (result != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "操作失败");
        return;
    }
    _countdown_render(state);
}

static void _countdown_reset_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_countdown_state_t *state = lv_event_get_user_data(event);
    (void)timer_service_countdown_reset();
    _countdown_render(state);
}

static void _countdown_mount(const app_manager_page_context_t *context)
{
    clock_countdown_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->minutes = clock_ui_minutes_get();
    state->last_minutes = UINT32_MAX;
    state->last_span = UINT32_MAX;
    state->last_color = UINT32_MAX;
    app_ui_page_create(&state->page, "倒计时", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_bottom(state->page.content, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    lv_obj_t *stage = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(stage);
    lv_obj_set_width(stage, LV_PCT(100));
    lv_obj_set_height(stage, 0);
    lv_obj_set_flex_grow(stage, 1);
    lv_obj_set_flex_flow(stage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stage, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stage, 2, 0);
    app_ui_make_passive(stage, false);

    state->ring = app_ui_ring_create(stage, 156, 12, APP_UI_COLOR_TRACK);
    state->value_label = clock_ui_label_create(state->ring, "05:00",
        APP_THEME_FONT_HUGE, APP_UI_COLOR_TEXT);
    state->last_value_font = APP_THEME_FONT_HUGE;
    lv_obj_align(state->value_label, LV_ALIGN_CENTER, 0, -11);
    state->phase_label = clock_ui_label_create(state->ring, "准备",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);
    lv_obj_align(state->phase_label, LV_ALIGN_CENTER, 0, 42);

    state->hint_label = clock_ui_label_create(stage, "选择时长后开始",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);
    state->detail_label = clock_ui_label_create(stage, "",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_FAINT);

    lv_obj_t *chip_row = app_ui_chip_row_create(state->page.content);
    for (size_t index = 0U; index < 4U; ++index)
    {
        state->chips[index] = app_ui_chip_create(chip_row, k_preset_text[index],
            _countdown_chip_event, state);
    }
    state->chips[4] = app_ui_chip_create(chip_row, "自定义",
                                         _countdown_chip_event, state);
    /* The three-character custom caption is wider than an equal fifth of the
     * 320 px target; give it a fixed width and let the four presets flex. */
    lv_obj_set_width(state->chips[4], 64);
    lv_obj_set_flex_grow(state->chips[4], 0);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->btn_primary = app_ui_button_create(controls, "开始",
        _countdown_primary_event, state);
    app_ui_button_set_kind(state->btn_primary, APP_UI_BUTTON_PRIMARY);
    state->btn_reset = app_ui_button_create(controls, "重置",
                                            _countdown_reset_event, state);
    lv_obj_set_hidden(state->btn_reset, true);

    state->refresh_timer = lv_timer_create(_countdown_refresh, 250U, state);
    _countdown_render(state);
}

static esp_err_t _countdown_pause(const app_manager_page_context_t *context)
{
    clock_countdown_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _countdown_resume(const app_manager_page_context_t *context)
{
    clock_countdown_state_t *state = context->state;
    state->minutes = clock_ui_minutes_get();
    state->last_state = TIMER_SERVICE_IDLE;
    state->state_valid = false;
    state->last_minutes = UINT32_MAX;
    state->last_value[0] = '\0';
    state->last_phase[0] = '\0';
    state->last_detail[0] = '\0';
    state->last_span = UINT32_MAX;
    state->last_color = UINT32_MAX;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _countdown_render(state);
}

static void _countdown_unmount(const app_manager_page_context_t *context)
{
    clock_countdown_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    memset(state->chips, 0, sizeof(state->chips));
    state->ring = NULL;
    state->value_label = NULL;
    state->phase_label = NULL;
    state->hint_label = NULL;
    state->detail_label = NULL;
    state->btn_primary = NULL;
    state->btn_reset = NULL;
}

static void _countdown_new_intent(const app_manager_page_context_t *context)
{
    clock_countdown_state_t *state = context->state;
    uint32_t minutes = 0U;
    if (clock_ui_take_minutes_argument(&minutes))
    {
        clock_ui_minutes_set(minutes);
        state->minutes = clock_ui_minutes_get();
    }
}

static const app_manager_page_ops_t s_clock_countdown_ops =
{
    .mount = _countdown_mount,
    .new_intent = _countdown_new_intent,
    .resume = _countdown_resume,
    .pause = _countdown_pause,
    .unmount = _countdown_unmount,
};

const app_manager_page_definition_t clock_countdown_page_definition =
{
    .ops = &s_clock_countdown_ops,
    .memory_size = sizeof(clock_countdown_state_t),
};
