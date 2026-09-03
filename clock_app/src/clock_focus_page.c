#define DBG_TAG "clock_focus"
#define DBG_LVL DBG_INFO
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
    lv_obj_t *chips[3];
    lv_obj_t *btn_primary;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    uint32_t preset;
    uint32_t last_preset;
    uint32_t last_span;
    uint32_t last_color;
    uint32_t last_cycles;
    char last_value[12];
    timer_service_state_t last_state;
    timer_service_focus_phase_t last_phase;
} clock_focus_state_t;

_Static_assert(sizeof(clock_focus_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Focus page state exceeds the lifecycle arena slot");

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
    char text[12];
    clock_ui_format_mmss(running ? snapshot.focus_remaining_ms :
                         k_presets[state->preset].work_ms, text,
                         sizeof(text));
    if (strcmp(state->last_value, text) != 0)
    {
        (void)snprintf(state->last_value, sizeof(state->last_value), "%s",
                       text);
        lv_label_set_text(state->value_label, text);
    }
    const uint32_t color = view == TIMER_SERVICE_PAUSED ? APP_UI_COLOR_MUTED :
                           APP_UI_COLOR_SUN;
    if (state->last_span != snapshot.focus_remaining_ms ||
            state->last_color != color)
    {
        state->last_span = snapshot.focus_remaining_ms;
        state->last_color = color;
        clock_ui_ring_update(state->ring, running,
                             snapshot.focus_remaining_ms, 0U, color);
    }
    if (state->last_preset != state->preset)
    {
        state->last_preset = state->preset;
        for (size_t index = 0U; index < 3U; ++index)
        {
            clock_ui_chip_set_selected(state->chips[index],
                                       state->preset == (uint32_t)index);
        }
    }
    if (state->last_cycles != snapshot.focus_completed_cycles)
    {
        state->last_cycles = snapshot.focus_completed_cycles;
        char cycles[24];
        (void)snprintf(cycles, sizeof(cycles), "已完成 %u 轮",
                       (unsigned)snapshot.focus_completed_cycles);
        lv_label_set_text(state->cycle_label, cycles);
    }
    if (state->last_phase != snapshot.focus_phase)
    {
        state->last_phase = snapshot.focus_phase;
        lv_label_set_text(state->phase_label,
                          snapshot.focus_phase == TIMER_SERVICE_FOCUS_WORK ?
                          "专注中" : "休息中");
    }
    if (state->last_state != view)
    {
        state->last_state = view;
        switch (view)
        {
        case TIMER_SERVICE_RUNNING:
            clock_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        case TIMER_SERVICE_PAUSED:
            clock_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            clock_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        }
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
    if (timer_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.focus_state == TIMER_SERVICE_RUNNING ||
            snapshot.focus_state == TIMER_SERVICE_PAUSED)
    {
        return;
    }
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index < 3U)
    {
        state->preset = index;
    }
    _focus_render(state);
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
        return;
    }
    switch (snapshot.focus_state)
    {
    case TIMER_SERVICE_RUNNING:
        (void)timer_service_focus_pause();
        break;
    case TIMER_SERVICE_PAUSED:
        (void)timer_service_focus_resume();
        break;
    default:
        (void)timer_service_focus_start(k_presets[state->preset].work_ms,
                                        k_presets[state->preset].break_ms);
        break;
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

    lv_obj_t *ring_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(ring_row);
    lv_obj_set_width(ring_row, LV_PCT(100));
    lv_obj_set_height(ring_row, 200);
    lv_obj_set_flex_flow(ring_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ring_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(ring_row, false);

    state->ring = app_ui_ring_create(ring_row, 200, 8,
                                     APP_UI_COLOR_SURFACE_HI);

    lv_obj_t *text = lv_obj_create(state->ring);
    lv_obj_remove_style_all(text);
    lv_obj_set_size(text, 140, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
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

    state->phase_label = lv_label_create(text);
    lv_obj_set_width(state->phase_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->phase_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->phase_label,
                                lv_color_hex(APP_UI_COLOR_SUN), 0);
    lv_obj_set_style_text_font(state->phase_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->phase_label, "专注中");

    state->cycle_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->cycle_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->cycle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->cycle_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->cycle_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->cycle_label, "已完成 0 轮");

    lv_obj_t *chip_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(chip_row);
    lv_obj_set_width(chip_row, LV_PCT(100));
    lv_obj_set_height(chip_row, 40);
    lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip_row, 8, 0);
    app_ui_make_passive(chip_row, false);
    for (size_t index = 0U; index < 3U; ++index)
    {
        state->chips[index] = clock_ui_chip(chip_row, k_preset_text[index],
                                            _focus_chip_event, state);
    }

    lv_obj_t *controls = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(controls);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 52);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 8, 0);
    app_ui_make_passive(controls, false);
    state->btn_primary = clock_ui_action_button(controls, "开始",
                         _focus_primary_event, state);
    state->btn_reset = clock_ui_action_button(controls, "重置",
                       _focus_reset_event, state);
    lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);

    state->refresh_timer = lv_timer_create(_focus_refresh, 250U, state);
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
    state->last_phase = TIMER_SERVICE_FOCUS_WORK;
    state->last_cycles = UINT32_MAX;
    state->last_preset = UINT32_MAX;
    state->last_span = 0U;
    state->last_value[0] = '\0';
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
    state->ring = NULL;
    state->value_label = NULL;
    state->phase_label = NULL;
    state->cycle_label = NULL;
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
