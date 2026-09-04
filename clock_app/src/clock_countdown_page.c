#define DBG_TAG "clock_countdown"
#define DBG_LVL DBG_INFO
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
    lv_obj_t *hint_label;
    lv_obj_t *chips[5];
    lv_obj_t *btn_primary;
    lv_obj_t *btn_reset;
    lv_timer_t *refresh_timer;
    uint32_t minutes;
    uint32_t last_minutes;
    uint32_t last_span;
    uint32_t last_color;
    char last_value[12];
    timer_service_state_t last_state;
} clock_countdown_state_t;

_Static_assert(sizeof(clock_countdown_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Countdown page state exceeds the lifecycle arena slot");

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
    char text[12];
    clock_ui_format_mmss(view == TIMER_SERVICE_IDLE ?
                         state->minutes * 60000U : remaining, text,
                         sizeof(text));
    if (strcmp(state->last_value, text) != 0)
    {
        (void)snprintf(state->last_value, sizeof(state->last_value), "%s",
                       text);
        lv_label_set_text(state->value_label, text);
    }
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
    if (state->last_state != view)
    {
        state->last_state = view;
        lv_label_set_text(state->hint_label, "");
        switch (view)
        {
        case TIMER_SERVICE_IDLE:
            lv_label_set_text(state->hint_label, "选择时长后开始");
            app_ui_button_set_text(state->btn_primary, "开始");
            lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        case TIMER_SERVICE_RUNNING:
            lv_label_set_text(state->hint_label, "倒计时进行中");
            app_ui_button_set_text(state->btn_primary, "暂停");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        case TIMER_SERVICE_PAUSED:
            lv_label_set_text(state->hint_label, "已暂停");
            app_ui_button_set_text(state->btn_primary, "继续");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_label_set_text(state->hint_label, "时间到");
            app_ui_button_set_text(state->btn_primary, "重新开始");
            lv_obj_remove_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);
            break;
        }
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
        return;
    }
    if (snapshot.countdown_state == TIMER_SERVICE_RUNNING ||
            snapshot.countdown_state == TIMER_SERVICE_PAUSED)
    {
        lv_label_set_text(state->hint_label, "运行中不能切换时长");
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
        lv_label_set_text(state->hint_label, "状态读取失败");
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
        lv_label_set_text(state->hint_label, "专注进行中，无法开始倒计时");
        return;
    }
    if (result != ESP_OK)
    {
        lv_label_set_text(state->hint_label, "操作失败");
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
    app_ui_page_create(&state->page, "倒计时", true);

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

    state->value_label = lv_label_create(state->ring);
    lv_obj_set_style_text_color(state->value_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->value_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_label_set_text(state->value_label, "05:00");
    lv_obj_center(state->value_label);

    state->hint_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->hint_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->hint_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->hint_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->hint_label, "选择时长后开始");

    lv_obj_t *chip_row = app_ui_chip_row_create(state->page.content);
    for (size_t index = 0U; index < 4U; ++index)
    {
        state->chips[index] = app_ui_chip_create(chip_row, k_preset_text[index],
                              _countdown_chip_event, state);
    }
    state->chips[4] = app_ui_chip_create(chip_row, "自定义",
                                         _countdown_chip_event, state);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content, 52);
    state->btn_primary = app_ui_button_create(controls, "开始",
                         _countdown_primary_event,
                         state);
    state->btn_reset = app_ui_button_create(controls, "重置",
                                            _countdown_reset_event, state);
    lv_obj_add_flag(state->btn_reset, LV_OBJ_FLAG_HIDDEN);

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
    state->last_minutes = 0U;
    state->last_value[0] = '\0';
    state->last_span = 0U;
    state->last_color = 0U;
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
    state->ring = NULL;
    state->value_label = NULL;
    state->hint_label = NULL;
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
