#define DBG_TAG "clock_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "time_service.h"
#include "timer_service.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef enum
{
    CLOCK_VIEW_CLOCK = 0,
    CLOCK_VIEW_COUNTDOWN,
    CLOCK_VIEW_STOPWATCH,
    CLOCK_VIEW_FOCUS,
} clock_view_t;

typedef struct clock_page_state clock_page_state_t;

typedef struct clock_action_context
{
    clock_page_state_t *state;
    uintptr_t action;
} clock_action_context_t;

struct clock_page_state
{
    app_ui_page_t page;
    lv_obj_t *time_label;
    lv_obj_t *detail_label;
    lv_obj_t *status_label;
    lv_timer_t *refresh_timer;
    clock_view_t view;
    uint32_t countdown_minutes;
    clock_action_context_t actions[3];
};

_Static_assert(sizeof(clock_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Clock page state exceeds lifecycle arena slot");

static void _clock_render(clock_page_state_t *state)
{
    timer_service_snapshot_t snapshot;
    char text[64];
    if (state->view == CLOCK_VIEW_CLOCK)
    {
        struct tm local_time;
        if (time_service_get_local(&local_time) == ESP_OK)
        {
            (void)strftime(text, sizeof(text), "%H:%M:%S", &local_time);
            lv_label_set_text(state->time_label, text);
            (void)snprintf(text, sizeof(text), "%d月%d日", local_time.tm_mon + 1,
                           local_time.tm_mday);
            lv_label_set_text(state->detail_label, text);
        }
        else
        {
            lv_label_set_text(state->time_label, "--:--:--");
            lv_label_set_text(state->detail_label, "等待有效时间");
        }
        lv_label_set_text(state->status_label, "时钟");
        return;
    }
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    if (state->view == CLOCK_VIEW_COUNTDOWN)
    {
        const uint32_t seconds = snapshot.countdown_remaining_ms / 1000U;
        (void)snprintf(text, sizeof(text), "%02u:%02u",
                       (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
        lv_label_set_text(state->time_label, text);
        lv_label_set_text(state->detail_label,
                          snapshot.countdown_state == TIMER_SERVICE_COMPLETED ?
                          "已完成" : "倒计时");
    }
    else if (state->view == CLOCK_VIEW_STOPWATCH)
    {
        (void)snprintf(text, sizeof(text), "%02llu:%02llu",
                       (unsigned long long)(snapshot.stopwatch_elapsed_ms / 60000U),
                       (unsigned long long)((snapshot.stopwatch_elapsed_ms / 1000U) % 60U));
        lv_label_set_text(state->time_label, text);
        lv_label_set_text(state->detail_label, "秒表");
    }
    else
    {
        const uint32_t seconds = snapshot.focus_remaining_ms / 1000U;
        (void)snprintf(text, sizeof(text), "%02u:%02u",
                       (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
        lv_label_set_text(state->time_label, text);
        lv_label_set_text(state->detail_label,
                          snapshot.focus_phase == TIMER_SERVICE_FOCUS_WORK ?
                          "专注" : "休息");
    }
    app_ui_set_status_text(state->status_label, "运行状态",
                           snapshot.countdown_state == TIMER_SERVICE_COMPLETED ?
                           APP_UI_STATUS_SUCCESS : APP_UI_STATUS_ACCENT);
}

static void _clock_refresh(lv_timer_t *timer)
{
    _clock_render(lv_timer_get_user_data(timer));
}

static void _clock_set_view(clock_page_state_t *state, clock_view_t view)
{
    state->view = view;
    _clock_render(state);
}

static void _clock_view_clock(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _clock_set_view(lv_event_get_user_data(event), CLOCK_VIEW_CLOCK);
    }
}

static void _clock_view_countdown(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _clock_set_view(lv_event_get_user_data(event), CLOCK_VIEW_COUNTDOWN);
    }
}

static void _clock_view_stopwatch(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _clock_set_view(lv_event_get_user_data(event), CLOCK_VIEW_STOPWATCH);
    }
}

static void _clock_view_focus(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _clock_set_view(lv_event_get_user_data(event), CLOCK_VIEW_FOCUS);
    }
}

static void _clock_action_event(lv_event_t *event)
{
    const clock_action_context_t *action_context = lv_event_get_user_data(event);
    clock_page_state_t *state = action_context->state;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    const uintptr_t action = action_context->action;
    esp_err_t result = ESP_OK;
    timer_service_snapshot_t snapshot;
    const bool snapshot_valid = timer_service_get_snapshot(&snapshot) == ESP_OK;
    if (state->view == CLOCK_VIEW_COUNTDOWN)
    {
        if (action == 1U)
        {
            result = snapshot_valid &&
                     snapshot.countdown_state == TIMER_SERVICE_PAUSED ?
                     timer_service_countdown_resume() :
                     timer_service_countdown_start(
                         state->countdown_minutes * 60000U);
        }
        else if (action == 2U)
        {
            result = timer_service_countdown_pause();
        }
        else
        {
            result = timer_service_countdown_reset();
        }
    }
    else if (state->view == CLOCK_VIEW_STOPWATCH)
    {
        result = action == 1U ? timer_service_stopwatch_start() :
                 (action == 2U ? timer_service_stopwatch_pause() :
                  timer_service_stopwatch_reset());
    }
    else if (state->view == CLOCK_VIEW_FOCUS)
    {
        result = action == 1U ?
                 (snapshot_valid && snapshot.focus_state == TIMER_SERVICE_PAUSED ?
                  timer_service_focus_resume() :
                  timer_service_focus_start(25U * 60000U, 5U * 60000U)) :
                 (action == 2U ? timer_service_focus_pause() :
                  timer_service_focus_reset());
    }
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "当前状态不可执行",
                               APP_UI_STATUS_WARNING);
    }
    _clock_render(state);
}

static lv_obj_t *_clock_button(lv_obj_t *parent, const char *text,
                               void *user_data, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(24));
    lv_obj_set_height(button, 52);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void _clock_mount(const app_manager_page_context_t *context)
{
    clock_page_state_t *state = context->state;
    app_ui_page_create(&state->page, "时钟", false);
    app_ui_page_set_subtitle(&state->page, "时间与计时");
    state->time_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->time_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->time_label, app_ui_font(APP_THEME_FONT_TITLE), 0);
    state->detail_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_style_text_align(state->detail_label, LV_TEXT_ALIGN_CENTER, 0);
    state->status_label = app_ui_add_body_label(state->page.content, "");

    lv_obj_t *views = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(views);
    lv_obj_set_width(views, LV_PCT(100));
    lv_obj_set_height(views, 58);
    lv_obj_set_flex_flow(views, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(views, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(views, false);
    _clock_button(views, "时钟", state, _clock_view_clock);
    _clock_button(views, "倒计时", state, _clock_view_countdown);
    _clock_button(views, "秒表", state, _clock_view_stopwatch);
    _clock_button(views, "专注", state, _clock_view_focus);

    lv_obj_t *actions = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 58);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(actions, false);
    for (size_t index = 0U; index < 3U; ++index)
    {
        state->actions[index].state = state;
        state->actions[index].action = index + 1U;
    }
    _clock_button(actions, "开始", &state->actions[0], _clock_action_event);
    _clock_button(actions, "暂停", &state->actions[1], _clock_action_event);
    _clock_button(actions, "重置", &state->actions[2], _clock_action_event);
    state->refresh_timer = lv_timer_create(_clock_refresh, 250U, state);
    _clock_render(state);
}

static void _clock_start(const app_manager_page_context_t *context)
{
    clock_page_state_t *state = context->state;
    state->view = CLOCK_VIEW_CLOCK;
    state->countdown_minutes = 5U;
}

static esp_err_t _clock_pause(const app_manager_page_context_t *context)
{
    clock_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _clock_resume(const app_manager_page_context_t *context)
{
    clock_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _clock_render(state);
}

static void _clock_unmount(const app_manager_page_context_t *context)
{
    clock_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->time_label = NULL;
    state->detail_label = NULL;
    state->status_label = NULL;
}

static const app_manager_page_ops_t s_clock_ops =
{
    .start = _clock_start,
    .mount = _clock_mount,
    .resume = _clock_resume,
    .pause = _clock_pause,
    .unmount = _clock_unmount,
};

static const app_manager_page_definition_t s_clock_definition =
{
    .ops = &s_clock_ops,
    .memory_size = sizeof(clock_page_state_t),
};

static const app_manager_page_route_t s_clock_routes[] =
{
    {.page_id = "root", .definition = &s_clock_definition, .user_data = NULL},
};

APP_MANAGER_APP_EXPORT_META(clock, APP_IMAGE_CLOCK_ICON, "时钟",
                            APP_MANAGER_ID_CLOCK, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_clock_routes, 20U,
                            "倒计时与专注");
