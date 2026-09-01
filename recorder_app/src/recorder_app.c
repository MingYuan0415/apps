#define DBG_TAG "recorder_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "recorder_service.h"

#include <stdio.h>
#include <string.h>

typedef struct recorder_page_state
{
    app_ui_page_t page;
    lv_obj_t *state_label;
    lv_obj_t *duration_label;
    lv_obj_t *space_label;
    lv_obj_t *file_label;
    lv_obj_t *list_label;
    lv_timer_t *refresh_timer;
    char selected_name[64];
} recorder_page_state_t;

_Static_assert(sizeof(recorder_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder page state exceeds lifecycle arena slot");

static void _recorder_render(recorder_page_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        app_ui_set_status_text(state->state_label, "录音服务不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    char text[64];
    (void)snprintf(text, sizeof(text), "%02u:%02u",
                   (unsigned)(snapshot.duration_ms / 60000U),
                   (unsigned)((snapshot.duration_ms / 1000U) % 60U));
    lv_label_set_text(state->duration_label, text);
    (void)snprintf(text, sizeof(text), "可用空间 %llu MB",
                   (unsigned long long)(snapshot.free_bytes / (1024U * 1024U)));
    lv_label_set_text(state->space_label, text);
    lv_label_set_text(state->file_label,
                      snapshot.name[0] != '\0' ? snapshot.name : "尚未创建录音");
    recorder_service_file_t files[RECORDER_SERVICE_MAX_FILES];
    size_t file_count = 0U;
    if (recorder_service_list(files, RECORDER_SERVICE_MAX_FILES,
                              &file_count) == ESP_OK)
    {
        char listing[256] = "";
        for (size_t index = 0U; index < file_count; ++index)
        {
            char line[48];
            const char *name = strrchr(files[index].name, '/');
            name = name != NULL ? name + 1 : files[index].name;
            (void)snprintf(line, sizeof(line), "%.30s  %02u:%02u\n", name,
                           (unsigned)(files[index].duration_ms / 60000U),
                           (unsigned)((files[index].duration_ms / 1000U) % 60U));
            (void)strncat(listing, line, sizeof(listing) - strlen(listing) - 1U);
            if (index == 0U && state->selected_name[0] == '\0')
            {
                (void)memcpy(state->selected_name, files[index].name,
                             sizeof(state->selected_name) - 1U);
                state->selected_name[sizeof(state->selected_name) - 1U] = '\0';
            }
        }
        lv_label_set_text(state->list_label,
                          file_count == 0U ? "暂无已完成录音" : listing);
    }
    const char *status = "就绪";
    app_ui_status_t color = APP_UI_STATUS_NEUTRAL;
    if (snapshot.state == RECORDER_SERVICE_RECORDING)
    {
        status = "录音中";
        color = APP_UI_STATUS_ACCENT;
    }
    else if (snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        status = "已暂停";
        color = APP_UI_STATUS_WARNING;
    }
    else if (snapshot.state == RECORDER_SERVICE_ERROR)
    {
        status = "录音失败";
        color = APP_UI_STATUS_ERROR;
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        status = "播放中";
        color = APP_UI_STATUS_ACCENT;
    }
    if (snapshot.operation_pending)
    {
        status = "处理中";
        color = APP_UI_STATUS_WARNING;
    }
    app_ui_set_status_text(state->state_label, status, color);
}

static void _recorder_refresh(lv_timer_t *timer)
{
    _recorder_render(lv_timer_get_user_data(timer));
}

static void _recorder_start_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        (void)recorder_service_start();
    }
}

static void _recorder_pause_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        (void)recorder_service_pause();
    }
}

static void _recorder_resume_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        (void)recorder_service_resume();
    }
}

static void _recorder_stop_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        (void)recorder_service_stop();
    }
}

static void _recorder_play_event(lv_event_t *event)
{
    recorder_page_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state == NULL || state->selected_name[0] == '\0')
    {
        return;
    }
    (void)recorder_service_play(state->selected_name);
}

static void _recorder_stop_playback_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        (void)recorder_service_stop_playback();
    }
}

static void _recorder_delete_event(lv_event_t *event)
{
    recorder_page_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state == NULL || state->selected_name[0] == '\0')
    {
        return;
    }
    (void)recorder_service_delete(state->selected_name);
    state->selected_name[0] = '\0';
}

static lv_obj_t *_recorder_action(lv_obj_t *parent, const char *text,
                                  lv_event_cb_t callback,
                                  recorder_page_state_t *state)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_width(button, LV_PCT(23));
    lv_obj_set_height(button, 54);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void _recorder_mount(const app_manager_page_context_t *context)
{
    recorder_page_state_t *state = context->state;
    app_ui_page_create(&state->page, "录音", false);
    app_ui_page_set_subtitle(&state->page, "语音备忘");
    state->state_label = app_ui_add_body_label(state->page.content, "就绪");
    state->duration_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->duration_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->duration_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->duration_label, app_ui_font(APP_THEME_FONT_TITLE), 0);
    state->space_label = app_ui_add_body_label(state->page.content, "可用空间 --");
    state->file_label = app_ui_add_body_label(state->page.content, "尚未创建录音");
    lv_label_set_long_mode(state->file_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    state->list_label = app_ui_add_body_label(state->page.content, "暂无已完成录音");
    lv_label_set_long_mode(state->list_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *actions = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 60);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(actions, false);
    _recorder_action(actions, "开始", _recorder_start_event, state);
    _recorder_action(actions, "暂停", _recorder_pause_event, state);
    _recorder_action(actions, "继续", _recorder_resume_event, state);
    _recorder_action(actions, "停止", _recorder_stop_event, state);
    lv_obj_t *playback = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(playback);
    lv_obj_set_width(playback, LV_PCT(100));
    lv_obj_set_height(playback, 60);
    lv_obj_set_flex_flow(playback, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(playback, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(playback, false);
    lv_obj_t *play = _recorder_action(playback, "播放", _recorder_play_event,
                                      state);
    lv_obj_set_width(play, LV_PCT(48));
    lv_obj_t *stop_playback = _recorder_action(playback, "停止播放",
                              _recorder_stop_playback_event,
                              state);
    lv_obj_set_width(stop_playback, LV_PCT(48));
    app_ui_add_command(state->page.content, LV_SYMBOL_TRASH,
                       "删除当前录音", "移除最近完成的 WAV 文件",
                       _recorder_delete_event, state);
    state->refresh_timer = lv_timer_create(_recorder_refresh, 250U, state);
    _recorder_render(state);
}

static esp_err_t _recorder_pause(const app_manager_page_context_t *context)
{
    recorder_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _recorder_resume(const app_manager_page_context_t *context)
{
    recorder_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _recorder_render(state);
}

static void _recorder_unmount(const app_manager_page_context_t *context)
{
    recorder_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->state_label = NULL;
    state->duration_label = NULL;
    state->space_label = NULL;
    state->file_label = NULL;
    state->list_label = NULL;
}

static const app_manager_page_ops_t s_recorder_ops =
{
    .mount = _recorder_mount,
    .resume = _recorder_resume,
    .pause = _recorder_pause,
    .unmount = _recorder_unmount,
};

static const app_manager_page_definition_t s_recorder_definition =
{
    .ops = &s_recorder_ops,
    .memory_size = sizeof(recorder_page_state_t),
};

static const app_manager_page_route_t s_recorder_routes[] =
{
    {.page_id = "root", .definition = &s_recorder_definition, .user_data = NULL},
};

APP_MANAGER_APP_EXPORT_META(recorder, APP_IMAGE_RECORDER_ICON, "录音",
                            APP_MANAGER_ID_RECORDER, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_recorder_routes, 30U,
                            "WAV 语音备忘");
