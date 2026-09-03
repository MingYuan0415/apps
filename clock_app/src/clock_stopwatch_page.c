#define DBG_TAG "clock_stopwatch"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>

typedef struct clock_stopwatch_state
{
    app_ui_page_t page;
    lv_obj_t *value_label;
    lv_obj_t *ms_label;
    lv_obj_t *hint_label;
    lv_obj_t *btn_primary;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    char last_value[12];
    char last_ms[6];
    timer_service_state_t last_state;
} clock_stopwatch_state_t;

_Static_assert(sizeof(clock_stopwatch_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Stopwatch page state exceeds the lifecycle arena slot");

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
    if (state->refresh_timer != NULL)
    {
        lv_timer_set_period(state->refresh_timer,
                            view == TIMER_SERVICE_RUNNING ? 50U : 250U);
    }
    if (state->last_state != view)
    {
        state->last_state = view;
        switch (view)
        {
        case TIMER_SERVICE_RUNNING:
            lv_label_set_text(state->hint_label, "计时中");
            app_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_TEXT),
                                        0);
            break;
        case TIMER_SERVICE_PAUSED:
            lv_label_set_text(state->hint_label, "已暂停");
            app_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_MUTED),
                                        0);
            break;
        default:
            lv_label_set_text(state->hint_label, "未开始");
            app_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(state->value_label,
                                        lv_color_hex(APP_UI_COLOR_TEXT),
                                        0);
            break;
        }
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
        return;
    }
    if (snapshot.stopwatch_state == TIMER_SERVICE_RUNNING)
    {
        (void)timer_service_stopwatch_pause();
    }
    else
    {
        (void)timer_service_stopwatch_start();
    }
    _stopwatch_render(state);
}

static void _stopwatch_reset_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_stopwatch_state_t *state = lv_event_get_user_data(event);
    (void)timer_service_stopwatch_reset();
    _stopwatch_render(state);
}

static void _stopwatch_mount(const app_manager_page_context_t *context)
{
    clock_stopwatch_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "秒表", true);

    lv_obj_t *stage = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(stage);
    lv_obj_set_width(stage, LV_PCT(100));
    lv_obj_set_height(stage, 0);
    lv_obj_set_flex_grow(stage, 1);
    lv_obj_set_flex_flow(stage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stage, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(stage, false);

    lv_obj_t *value_row = lv_obj_create(stage);
    lv_obj_remove_style_all(value_row);
    lv_obj_set_width(value_row, LV_SIZE_CONTENT);
    lv_obj_set_height(value_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    app_ui_make_passive(value_row, false);

    state->value_label = lv_label_create(value_row);
    lv_obj_set_style_text_color(state->value_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->value_label,
                               app_ui_font(APP_THEME_FONT_HUGE), 0);
    lv_label_set_text(state->value_label, "00:00");

    state->ms_label = lv_label_create(value_row);
    lv_obj_set_style_text_color(state->ms_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->ms_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_pad_bottom(state->ms_label, 10, 0);
    lv_label_set_text(state->ms_label, ".000");

    state->hint_label = lv_label_create(stage);
    lv_obj_set_style_text_color(state->hint_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->hint_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->hint_label, "未开始");

    lv_obj_t *controls = app_ui_button_row_create(state->page.content, 52);
    state->btn_primary = app_ui_button_create(controls, "开始",
                         _stopwatch_primary_event,
                         state);
    state->btn_reset = app_ui_button_create(controls, "重置",
                                            _stopwatch_reset_event, state);
    lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);

    state->refresh_timer = lv_timer_create(_stopwatch_refresh, 250U, state);
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
    state->last_value[0] = '\0';
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
    state->value_label = NULL;
    state->ms_label = NULL;
    state->hint_label = NULL;
    state->btn_primary = NULL;
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
