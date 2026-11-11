#define DBG_TAG "clock_focus"
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>

typedef struct
{
    uint32_t work_ms;
    uint32_t break_ms;
} clock_focus_preset_t;

static const clock_focus_preset_t k_presets[3] =
{
    {25U * 60000U, 5U * 60000U},
    {45U * 60000U, 10U * 60000U},
    {50U * 60000U, 10U * 60000U},
};
static const char *const k_preset_text[3] = {"25/5", "45/10", "50/10"};

typedef struct clock_focus_state
{
    app_ui_page_t page;
    lv_obj_t *ring;
    lv_obj_t *value_label;
    lv_obj_t *phase_label;
    lv_obj_t *cycle_label;
    lv_obj_t *plan_label;
    lv_obj_t *plan_bar;
    lv_obj_t *chips[3];
    lv_obj_t *btn_primary;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    uint32_t preset;
    uint32_t last_preset;
    uint32_t last_span;
    uint32_t last_total;
    uint32_t last_color;
    uint32_t last_phase_color;
    uint32_t last_cycles;
    uint32_t last_progress;
    char last_value[12];
    char last_phase_text[16];
    char last_plan[64];
    timer_service_state_t last_state;
    timer_service_focus_phase_t last_phase;
    bool state_valid;
    bool plan_valid;
} clock_focus_state_t;

_Static_assert(sizeof(clock_focus_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Focus page state exceeds the lifecycle arena slot");

static void _focus_show_phase(clock_focus_state_t *state, const char *text)
{
    if (strcmp(state->last_phase_text, text) == 0)
    {
        return;
    }
    (void)snprintf(state->last_phase_text, sizeof(state->last_phase_text),
                   "%s", text);
    lv_label_set_text(state->phase_label, text);
}

static uint32_t _focus_phase_progress(const timer_service_snapshot_t *snapshot)
{
    const uint32_t total = snapshot->focus_phase ==
                           TIMER_SERVICE_FOCUS_WORK ?
                           snapshot->focus_work_ms :
                           snapshot->focus_break_ms;
    if (total == 0U || snapshot->focus_remaining_ms > total)
    {
        return 0U;
    }
    return (uint32_t)(((uint64_t)(total - snapshot->focus_remaining_ms) *
                       100U) / total);
}

static void _focus_render(clock_focus_state_t *state)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    const timer_service_state_t view = snapshot.focus_state;
    const bool running = view == TIMER_SERVICE_RUNNING ||
                         view == TIMER_SERVICE_PAUSED;
    char value[12];
    char text[64];
    clock_ui_format_mmss(running ? snapshot.focus_remaining_ms :
                         k_presets[state->preset].work_ms, value,
                         sizeof(value));
    if (strcmp(state->last_value, value) != 0)
    {
        (void)snprintf(state->last_value, sizeof(state->last_value), "%s",
                       value);
        lv_label_set_text(state->value_label, value);
    }
    const timer_service_focus_phase_t phase = snapshot.focus_phase;
    const uint32_t phase_total = phase == TIMER_SERVICE_FOCUS_WORK ?
                                 snapshot.focus_work_ms :
                                 snapshot.focus_break_ms;
    const uint32_t color = view == TIMER_SERVICE_PAUSED ? APP_UI_COLOR_MUTED :
                           APP_UI_COLOR_SUN;
    if (state->last_span != snapshot.focus_remaining_ms ||
            state->last_total != phase_total || state->last_color != color)
    {
        state->last_span = snapshot.focus_remaining_ms;
        state->last_total = phase_total;
        state->last_color = color;
        clock_ui_ring_update(state->ring, running,
                             snapshot.focus_remaining_ms, phase_total, color);
    }
    if (state->last_preset != state->preset)
    {
        state->last_preset = state->preset;
        for (size_t index = 0U; index < 3U; ++index)
        {
            app_ui_chip_set_selected(state->chips[index],
                                     state->preset == (uint32_t)index);
        }
    }
    if (state->last_cycles != snapshot.focus_completed_cycles)
    {
        state->last_cycles = snapshot.focus_completed_cycles;
        char cycle_text[32];
        (void)snprintf(cycle_text, sizeof(cycle_text), "已完成 %u 轮",
                       (unsigned)snapshot.focus_completed_cycles);
        lv_label_set_text(state->cycle_label, cycle_text);
    }

    const uint32_t progress = running ? _focus_phase_progress(&snapshot) : 0U;
    if (!state->plan_valid || state->last_progress != progress)
    {
        state->plan_valid = true;
        state->last_progress = progress;
        lv_bar_set_value(state->plan_bar, (int32_t)progress, LV_ANIM_OFF);
    }
    if (running)
    {
        const uint32_t phase_left = (phase_total + 59999U) / 60000U;
        (void)snprintf(text, sizeof(text), "%s %u 分 · 本轮 %u%%",
                       phase == TIMER_SERVICE_FOCUS_WORK ? "专注" : "休息",
                       (unsigned)phase_left, (unsigned)progress);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%u 分专注 · %u 分休息",
                       (unsigned)(k_presets[state->preset].work_ms / 60000U),
                       (unsigned)(k_presets[state->preset].break_ms / 60000U));
    }
    if (strcmp(state->last_plan, text) != 0)
    {
        (void)snprintf(state->last_plan, sizeof(state->last_plan), "%s", text);
        lv_label_set_text(state->plan_label, text);
    }

    const char *phase_text = "准备";
    if (view == TIMER_SERVICE_PAUSED)
    {
        phase_text = "已暂停";
    }
    else if (view == TIMER_SERVICE_RUNNING)
    {
        phase_text = phase == TIMER_SERVICE_FOCUS_WORK ? "专注中" : "休息中";
    }
    _focus_show_phase(state, phase_text);
    const uint32_t phase_color = view == TIMER_SERVICE_RUNNING ?
                                 APP_UI_COLOR_SUN : APP_UI_COLOR_MUTED;
    if (state->last_phase_color != phase_color)
    {
        state->last_phase_color = phase_color;
        lv_obj_set_style_text_color(state->phase_label,
                                    lv_color_hex(phase_color), 0);
    }
    if (state->last_phase != phase)
    {
        state->last_phase = phase;
        if (view == TIMER_SERVICE_RUNNING)
        {
            clock_ui_pulse(state->phase_label);
        }
    }
    if (!state->state_valid || state->last_state != view)
    {
        state->state_valid = true;
        state->last_state = view;
        switch (view)
        {
        case TIMER_SERVICE_RUNNING:
            app_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_set_hidden(state->btn_reset, false);
            break;
        case TIMER_SERVICE_PAUSED:
            app_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_set_hidden(state->btn_reset, false);
            break;
        default:
            app_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_set_hidden(state->btn_reset, true);
            break;
        }
        clock_ui_pulse(state->ring);
    }
}

static void _focus_refresh(lv_timer_t *timer)
{
    _focus_render(lv_timer_get_user_data(timer));
}

static void _focus_chip_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_focus_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "状态读取失败");
        return;
    }
    if (snapshot.focus_state == TIMER_SERVICE_RUNNING ||
            snapshot.focus_state == TIMER_SERVICE_PAUSED)
    {
        (void)app_ui_toast_show(state->page.root, "运行中不能切换时长");
        return;
    }
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index < 3U)
    {
        state->preset = index;
    }
    _focus_render(state);
    clock_ui_pulse(state->value_label);
}

static void _focus_primary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_focus_state_t *state = lv_event_get_user_data(event);
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "状态读取失败");
        return;
    }
    esp_err_t result = ESP_OK;
    switch (snapshot.focus_state)
    {
    case TIMER_SERVICE_RUNNING:
        result = timer_service_focus_pause();
        break;
    case TIMER_SERVICE_PAUSED:
        result = timer_service_focus_resume();
        break;
    default:
        result = timer_service_focus_start(k_presets[state->preset].work_ms,
                                           k_presets[state->preset].break_ms);
        break;
    }
    if (result == ESP_ERR_INVALID_STATE)
    {
        (void)app_ui_toast_show(state->page.root, "倒计时进行中，无法开始专注");
        return;
    }
    if (result != ESP_OK)
    {
        (void)app_ui_toast_show(state->page.root, "操作失败");
        return;
    }
    _focus_render(state);
}

static void _focus_reset_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_focus_state_t *state = lv_event_get_user_data(event);
    (void)timer_service_focus_reset();
    _focus_render(state);
}

static void _focus_mount(const app_manager_page_context_t *context)
{
    clock_focus_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->last_preset = UINT32_MAX;
    app_ui_page_create(&state->page, "专注", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_bottom(state->page.content, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    lv_obj_t *ring_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(ring_row);
    lv_obj_set_width(ring_row, LV_PCT(100));
    lv_obj_set_height(ring_row, 168);
    lv_obj_set_flex_flow(ring_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ring_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(ring_row, false);

    state->ring = app_ui_ring_create(ring_row, 156, 12, APP_UI_COLOR_TRACK);

    lv_obj_t *text = lv_obj_create(state->ring);
    lv_obj_remove_style_all(text);
    lv_obj_set_size(text, 124, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(text, 0, 0);
    app_ui_make_passive(text, false);
    lv_obj_center(text);

    state->value_label = lv_label_create(text);
    lv_obj_set_width(state->value_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->value_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->value_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_label_set_text(state->value_label, "25:00");
    app_ui_make_passive(state->value_label, false);

    state->phase_label = lv_label_create(text);
    lv_obj_set_width(state->phase_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->phase_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->phase_label,
                                lv_color_hex(APP_UI_COLOR_SUN), 0);
    lv_obj_set_style_text_font(state->phase_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->phase_label, "准备");
    app_ui_make_passive(state->phase_label, false);

    state->cycle_label = lv_label_create(text);
    lv_obj_set_width(state->cycle_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->cycle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->cycle_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->cycle_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->cycle_label, "已完成 0 轮");
    app_ui_make_passive(state->cycle_label, false);

    lv_obj_t *plan = clock_ui_card_create(state->page.content);
    lv_obj_set_style_pad_all(plan, 12, 0);
    lv_obj_set_style_pad_row(plan, APP_UI_GAP_TIGHT, 0);
    state->plan_label = clock_ui_label_create(plan, "",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);
    state->plan_bar = lv_bar_create(plan);
    lv_obj_set_width(state->plan_bar, LV_PCT(100));
    lv_obj_set_height(state->plan_bar, 4);
    lv_bar_set_range(state->plan_bar, 0, 100);
    lv_bar_set_value(state->plan_bar, 0, LV_ANIM_OFF);
    lv_bar_set_mode(state->plan_bar, LV_BAR_MODE_RANGE);
    lv_obj_set_style_radius(state->plan_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->plan_bar,
                              lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_set_style_bg_opa(state->plan_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->plan_bar, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->plan_bar,
                              lv_color_hex(APP_UI_COLOR_SUN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(state->plan_bar, LV_OPA_COVER,
                            LV_PART_INDICATOR);
    app_ui_make_passive(state->plan_bar, false);

    lv_obj_t *chip_row = app_ui_chip_row_create(state->page.content);
    for (size_t index = 0U; index < 3U; ++index)
    {
        state->chips[index] = app_ui_chip_create(chip_row, k_preset_text[index],
            _focus_chip_event, state);
    }

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->btn_primary = app_ui_button_create(controls, "开始",
        _focus_primary_event, state);
    app_ui_button_set_kind(state->btn_primary, APP_UI_BUTTON_PRIMARY);
    state->btn_reset = app_ui_button_create(controls, "重置",
                                            _focus_reset_event, state);
    lv_obj_set_hidden(state->btn_reset, true);

    state->refresh_timer = app_ui_timer_create_passive(_focus_refresh, 250U, state);
    _focus_render(state);
}

static esp_err_t _focus_pause(const app_manager_page_context_t *context)
{
    clock_focus_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _focus_resume(const app_manager_page_context_t *context)
{
    clock_focus_state_t *state = context->state;
    state->last_state = TIMER_SERVICE_IDLE;
    state->state_valid = false;
    state->last_phase = TIMER_SERVICE_FOCUS_WORK;
    state->last_cycles = UINT32_MAX;
    state->last_preset = UINT32_MAX;
    state->last_span = UINT32_MAX;
    state->last_total = UINT32_MAX;
    state->last_phase_color = UINT32_MAX;
    state->last_progress = UINT32_MAX;
    state->plan_valid = false;
    state->last_value[0] = '\0';
    state->last_phase_text[0] = '\0';
    state->last_plan[0] = '\0';
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _focus_render(state);
}

static void _focus_unmount(const app_manager_page_context_t *context)
{
    clock_focus_state_t *state = context->state;
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
    state->cycle_label = NULL;
    state->plan_label = NULL;
    state->plan_bar = NULL;
    state->btn_primary = NULL;
    state->btn_reset = NULL;
}

static const app_manager_page_ops_t s_clock_focus_ops =
{
    .mount = _focus_mount,
    .resume = _focus_resume,
    .pause = _focus_pause,
    .unmount = _focus_unmount,
};

const app_manager_page_definition_t clock_focus_page_definition =
{
    .ops = &s_clock_focus_ops,
    .memory_size = sizeof(clock_focus_state_t),
};
