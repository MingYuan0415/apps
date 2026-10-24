#define DBG_TAG "recorder_detail"
#include "mt_log.h"

#include "recorder_app_internal.h"

typedef struct recorder_detail_state
{
    app_ui_page_t page;
    lv_obj_t *time_value;
    lv_obj_t *duration_value;
    lv_obj_t *size_value;
    lv_obj_t *bar;
    lv_obj_t *position_label;
    lv_obj_t *status_label;
    lv_obj_t *btn_play;
    lv_obj_t *btn_delete;
    lv_obj_t *confirm_sheet;
    lv_timer_t *refresh_timer;
    recorder_service_file_t file;
    uint32_t delete_operation_id;
    uint32_t stop_operation_id;
    bool has_file;
    bool target_current;
    bool delete_pending;
    bool pending_after_stop;
} recorder_detail_state_t;

_Static_assert(sizeof(recorder_detail_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder detail state exceeds the lifecycle arena slot");

static void _detail_set_status(recorder_detail_state_t *state, const char *text,
                               app_ui_status_t status)
{
    if (state->status_label != NULL && state->page.root != NULL)
    {
        app_ui_set_status_text(state->status_label, text, status);
    }
}

static bool _detail_target_current(const recorder_service_snapshot_t *snapshot,
                                   const recorder_service_file_t *file)
{
    return snapshot->index_valid && snapshot->storage_ready &&
           file->target.generation == snapshot->storage_generation &&
           file->target.revision == snapshot->content_revision &&
           file->target.generation == snapshot->index_generation &&
           file->target.revision == snapshot->index_revision;
}

static void _detail_set_controls(recorder_detail_state_t *state,
                                 bool enabled)
{
    if (state->btn_play == NULL || state->btn_delete == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_obj_remove_state(state->btn_play, LV_STATE_DISABLED);
        lv_obj_remove_state(state->btn_delete, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->btn_play, LV_STATE_DISABLED);
        lv_obj_add_state(state->btn_delete, LV_STATE_DISABLED);
    }
}

static void _detail_render(recorder_detail_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _detail_set_status(state, "录音服务不可用", APP_UI_STATUS_ERROR);
        _detail_set_controls(state, false);
        return;
    }
    if (!state->has_file)
    {
        _detail_set_status(state, "无法打开该录音", APP_UI_STATUS_ERROR);
        _detail_set_controls(state, false);
        return;
    }
    state->target_current = _detail_target_current(&snapshot, &state->file);
    const bool playing = snapshot.state == RECORDER_SERVICE_PLAYING;
    const bool playing_this = playing &&
                              strcmp(snapshot.name, state->file.target.name) == 0;
    const bool recording = snapshot.state == RECORDER_SERVICE_RECORDING ||
                           snapshot.state == RECORDER_SERVICE_PAUSED;

    app_ui_button_set_text(state->btn_play, playing_this ? "停止" : "播放");
    recorder_ui_set_visible(state->btn_play, state->target_current);
    recorder_ui_set_visible(state->btn_delete, state->target_current);
    recorder_ui_set_visible(state->bar, playing_this &&
                            snapshot.playback_duration_ms > 0U);
    if (playing_this && snapshot.playback_duration_ms > 0U)
    {
        lv_bar_set_value(state->bar,
                         (int32_t)(snapshot.playback_position_ms * 100U /
                                   snapshot.playback_duration_ms),
                         LV_ANIM_OFF);
        char position[16];
        char total[16];
        recorder_ui_format_mmss(snapshot.playback_position_ms, position,
                                sizeof(position));
        recorder_ui_format_mmss(snapshot.playback_duration_ms, total,
                                sizeof(total));
        char text[40];
        (void)snprintf(text, sizeof(text), "%s / %s", position, total);
        app_ui_label_set_text_if(state->position_label, text);
    }
    else
    {
        char total[16];
        recorder_ui_format_mmss(state->file.duration_ms, total, sizeof(total));
        app_ui_label_set_text_if(state->position_label, total);
    }

    const bool busy = snapshot.operation_pending || state->delete_pending ||
                      state->pending_after_stop;
    _detail_set_controls(state, state->target_current && !busy);
    if (!state->target_current)
    {
        _detail_set_status(state, "内容已变化，请返回重新选择",
                           APP_UI_STATUS_WARNING);
        return;
    }
    if (state->delete_pending)
    {
        _detail_set_status(state, "删除中", APP_UI_STATUS_WARNING);
    }
    else if (state->pending_after_stop)
    {
        _detail_set_status(state, "删除中", APP_UI_STATUS_WARNING);
    }
    else if (playing_this)
    {
        _detail_set_status(state, "播放中", APP_UI_STATUS_ACCENT);
    }
    else if (playing)
    {
        _detail_set_status(state, "正在播放其他录音", APP_UI_STATUS_WARNING);
    }
    else if (recording)
    {
        _detail_set_status(state, "录音中，暂时无法播放或删除",
                           APP_UI_STATUS_WARNING);
    }
    else if (snapshot.last_error != ESP_OK)
    {
        _detail_set_status(state, recorder_ui_error_text(snapshot.last_error),
                           APP_UI_STATUS_ERROR);
    }
    else
    {
        _detail_set_status(state, "", APP_UI_STATUS_NEUTRAL);
    }
}

static void _detail_play_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_detail_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || state->delete_pending ||
            state->pending_after_stop)
    {
        return;
    }
    const bool playing_this = snapshot.state == RECORDER_SERVICE_PLAYING &&
                              strcmp(snapshot.name, state->file.target.name) == 0;
    const esp_err_t result = playing_this ? recorder_service_stop_playback() :
                             recorder_service_play(&state->file.target);
    if (result != ESP_OK)
    {
        _detail_set_status(state, recorder_ui_error_text(result),
                           APP_UI_STATUS_ERROR);
        return;
    }
    _detail_render(state);
}

static void _detail_delete_confirm(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_detail_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || !state->target_current)
    {
        _detail_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        /* Stopping is a precondition, not a retry: the delete follows the
         * completed stop through the poll timer. */
        if (recorder_service_stop_playback() != ESP_OK ||
                recorder_service_get_snapshot(&snapshot) != ESP_OK)
        {
            _detail_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
            return;
        }
        state->stop_operation_id = snapshot.operation_id;
        state->pending_after_stop = true;
        _detail_set_status(state, "删除中", APP_UI_STATUS_WARNING);
        return;
    }
    if (recorder_service_delete(&state->file.target) != ESP_OK ||
            recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _detail_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        return;
    }
    state->delete_operation_id = snapshot.operation_id;
    state->delete_pending = true;
    _detail_set_status(state, "删除中", APP_UI_STATUS_WARNING);
}

static void _detail_delete_cancel(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_detail_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _detail_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_detail_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (state == NULL || state->confirm_sheet != NULL ||
            recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || state->delete_pending ||
            state->pending_after_stop || !state->target_current)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_RECORDING ||
            snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        _detail_set_status(state, "录音中无法删除", APP_UI_STATUS_WARNING);
        return;
    }
    char time_text[32];
    recorder_ui_format_time(&state->file, time_text, sizeof(time_text));
    char message[96];
    (void)snprintf(message, sizeof(message), "删除 %s 的录音？此操作无法撤销。",
                   time_text);
    state->confirm_sheet = app_ui_sheet_open(state->page.root, "删除录音",
        message);
    if (state->confirm_sheet == NULL)
    {
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "删除", true,
                                  _detail_delete_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _detail_delete_cancel, state);
}

static void _detail_poll(recorder_detail_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    if (state->pending_after_stop &&
            snapshot.operation_id == state->stop_operation_id)
    {
        state->pending_after_stop = false;
        if (snapshot.last_error == ESP_OK &&
                snapshot.state != RECORDER_SERVICE_PLAYING &&
                recorder_service_delete(&state->file.target) == ESP_OK &&
                recorder_service_get_snapshot(&snapshot) == ESP_OK)
        {
            state->delete_operation_id = snapshot.operation_id;
            state->delete_pending = true;
        }
        else
        {
            /* The render pass reports the mapped reason from last_error. */
            state->delete_pending = false;
        }
        return;
    }
    if (state->delete_pending &&
            snapshot.operation_id == state->delete_operation_id)
    {
        state->delete_pending = false;
        if (snapshot.last_error == ESP_OK)
        {
            recorder_app_clear_selection();
            /* Return to the list so the removal is visible where it happened. */
            app_ui_request_back();
            return;
        }
        state->delete_operation_id = 0U;
    }
}

static void _detail_tick(lv_timer_t *timer)
{
    recorder_detail_state_t *state = lv_timer_get_user_data(timer);
    _detail_poll(state);
    if (state->page.root != NULL)
    {
        _detail_render(state);
    }
}

static void _detail_mount(const app_manager_page_context_t *context)
{
    recorder_detail_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "录音详情", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);

    state->has_file = recorder_app_copy_selection(&state->file) == ESP_OK;
    recorder_app_clear_selection();

    lv_obj_t *facts = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_value(facts, "录制时间", "--", &state->time_value);
    (void)app_ui_group_add_value(facts, "时长", "--", &state->duration_value);
    (void)app_ui_group_add_value(facts, "大小", "--", &state->size_value);

    state->bar = lv_bar_create(state->page.content);
    lv_obj_set_width(state->bar, LV_PCT(100));
    lv_obj_set_height(state->bar, 8);
    lv_bar_set_range(state->bar, 0, 100);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(state->bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(state->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    app_ui_make_passive(state->bar, false);

    state->position_label = app_ui_add_body_label(state->page.content, "--");
    lv_obj_set_width(state->position_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->position_label, LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_style_text_font(state->position_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->position_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);

    state->status_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->btn_play = app_ui_button_create(controls, "播放",
                                           _detail_play_event, state);
    app_ui_button_set_kind(state->btn_play, APP_UI_BUTTON_PRIMARY);
    state->btn_delete = app_ui_button_create(controls, "删除",
        _detail_delete_event, state);
    app_ui_button_set_kind(state->btn_delete, APP_UI_BUTTON_DANGER);

    if (state->has_file)
    {
        char text[32];
        recorder_ui_format_time(&state->file, text, sizeof(text));
        lv_label_set_text(state->time_value, text);
        recorder_ui_format_mmss(state->file.duration_ms, text, sizeof(text));
        lv_label_set_text(state->duration_value, text);
        recorder_ui_format_size(state->file.bytes, text, sizeof(text));
        lv_label_set_text(state->size_value, text);
    }
    else
    {
        lv_label_set_text(state->time_value, "不可用");
        lv_label_set_text(state->duration_value, "--");
        lv_label_set_text(state->size_value, "--");
    }

    state->refresh_timer = lv_timer_create(_detail_tick, 250U, state);
    _detail_render(state);
}

static esp_err_t _detail_pause(const app_manager_page_context_t *context)
{
    recorder_detail_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _detail_resume(const app_manager_page_context_t *context)
{
    recorder_detail_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _detail_render(state);
}

static void _detail_unmount(const app_manager_page_context_t *context)
{
    recorder_detail_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->time_value = NULL;
    state->duration_value = NULL;
    state->size_value = NULL;
    state->bar = NULL;
    state->position_label = NULL;
    state->status_label = NULL;
    state->btn_play = NULL;
    state->btn_delete = NULL;
    state->confirm_sheet = NULL;
}

static const app_manager_page_ops_t s_recorder_detail_ops =
{
    .mount = _detail_mount,
    .resume = _detail_resume,
    .pause = _detail_pause,
    .unmount = _detail_unmount,
};

const app_manager_page_definition_t recorder_detail_page_definition =
{
    .ops = &s_recorder_detail_ops,
    .memory_size = sizeof(recorder_detail_state_t),
};
