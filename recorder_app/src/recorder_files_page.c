#define DBG_TAG "recorder_files"
#include "mt_log.h"

#include "recorder_app_internal.h"

#include <stdlib.h>

typedef struct recorder_files_state
{
    app_ui_page_t page;
    lv_obj_t *list;
    lv_obj_t *bar;
    lv_obj_t *btn_play;
    lv_obj_t *btn_delete;
    lv_obj_t *status_label;
    lv_obj_t *confirm_sheet;
    lv_timer_t *refresh_timer;
    recorder_service_file_t *files;
    size_t file_count;
    recorder_service_target_t selected;
    recorder_service_target_t confirmation_target;
    recorder_service_target_t pending_delete;
    uint32_t last_generation;
    uint32_t last_storage_generation;
    uint32_t last_content_revision;
    uint32_t last_index_generation;
    uint32_t last_index_revision;
    uint32_t delete_operation_id;
    uint32_t stop_operation_id;
    recorder_service_state_t last_state;
    bool has_selection;
    bool has_confirmation_target;
    bool pending_after_stop;
    bool delete_pending;
    bool last_index_valid;
    bool rows_dirty;
} recorder_files_state_t;

_Static_assert(sizeof(recorder_files_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder files state exceeds the lifecycle arena slot");

static void _files_row_event(lv_event_t *event);
static void _files_delete_confirm(lv_event_t *event);
static void _files_delete_cancel(lv_event_t *event);

static void _files_add_row(recorder_files_state_t *state,
                           const recorder_service_file_t *file,
                           size_t file_index)
{
    const bool selected = state->has_selection &&
                          state->selected.generation == file->target.generation &&
                          state->selected.revision == file->target.revision &&
                          strcmp(state->selected.name, file->target.name) == 0;
    lv_obj_t *row = lv_button_create(state->list);
    app_ui_click_only(row);
    lv_obj_set_user_data(row, (void *)(uintptr_t)(file_index + 1U));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, APP_UI_ROW_HEIGHT, 0);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(row,
                              lv_color_hex(selected ? APP_UI_COLOR_RAIN_TINT :
                                           APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, _files_row_event, LV_EVENT_CLICKED, state);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_width(name, 0);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(name,
                                lv_color_hex(selected ? APP_UI_COLOR_RAIN :
                                        APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(name, recorder_ui_display_name(file->target.name));

    char text[16];
    recorder_ui_format_mmss(file->duration_ms, text, sizeof(text));
    lv_obj_t *duration = lv_label_create(row);
    lv_obj_set_style_text_color(duration, lv_color_hex(APP_UI_COLOR_MUTED),
                                0);
    lv_obj_set_style_text_font(duration, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(duration, text);
}

static bool _files_target_equal(const recorder_service_target_t *left,
                                const recorder_service_target_t *right)
{
    return left->generation == right->generation &&
           left->revision == right->revision &&
           strcmp(left->name, right->name) == 0;
}

static void _files_rebuild(recorder_files_state_t *state)
{
    lv_obj_clean(state->list);
    state->rows_dirty = false;
    if (state->files == NULL)
    {
        (void)app_ui_add_empty_state(state->list, "列表暂不可用",
                                     "请稍后重试");
        return;
    }
    if (state->file_count == 0U)
    {
        (void)app_ui_add_empty_state(state->list, "暂无已完成录音",
                                     "录音完成后会出现在这里");
        return;
    }
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        _files_add_row(state, &state->files[index], index);
    }
}

static void _files_reload(recorder_files_state_t *state)
{
    state->file_count = 0U;
    if (state->files != NULL &&
            recorder_service_list(state->files, RECORDER_SERVICE_MAX_FILES,
                                  &state->file_count) != ESP_OK)
    {
        state->file_count = 0U;
    }
    bool selected_exists = false;
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        if (state->has_selection &&
                _files_target_equal(&state->selected, &state->files[index].target))
        {
            selected_exists = true;
        }
    }
    if (!selected_exists)
    {
        state->has_selection = false;
    }
    _files_rebuild(state);
}

static void _files_set_status(recorder_files_state_t *state, const char *text,
                              uint32_t color)
{
    if (state->status_label != NULL && state->page.root != NULL)
    {
        app_ui_set_status_text(state->status_label, text, color);
    }
}

static void _files_submit_delete(recorder_files_state_t *state,
                                 const recorder_service_target_t *target)
{
    state->pending_delete = *target;
    if (recorder_service_delete(&state->pending_delete) != ESP_OK)
    {
        state->pending_after_stop = false;
        state->delete_pending = false;
        _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        state->pending_after_stop = false;
        _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        return;
    }
    state->delete_operation_id = snapshot.operation_id;
    state->pending_after_stop = false;
    state->delete_pending = true;
    _files_set_status(state, "删除中", APP_UI_STATUS_WARNING);
}

static void _files_render(recorder_files_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    const bool epoch_changed =
        state->last_storage_generation != snapshot.storage_generation ||
        state->last_content_revision != snapshot.content_revision ||
        state->last_index_generation != snapshot.index_generation ||
        state->last_index_revision != snapshot.index_revision ||
        state->last_index_valid != snapshot.index_valid;
    const bool index_current = snapshot.index_valid &&
                               snapshot.index_generation == snapshot.storage_generation &&
                               snapshot.index_revision == snapshot.content_revision;
    if (state->has_selection &&
            (!index_current ||
             state->selected.generation != snapshot.storage_generation ||
             state->selected.revision != snapshot.content_revision))
    {
        state->has_selection = false;
        state->rows_dirty = true;
    }
    bool reload = epoch_changed ||
                  (index_current && (snapshot.generation != state->last_generation ||
                                     snapshot.state != state->last_state));
    state->last_storage_generation = snapshot.storage_generation;
    state->last_content_revision = snapshot.content_revision;
    state->last_index_generation = snapshot.index_generation;
    state->last_index_revision = snapshot.index_revision;
    state->last_index_valid = snapshot.index_valid;
    state->last_generation = snapshot.generation;
    state->last_state = snapshot.state;
    if (state->pending_after_stop && !snapshot.operation_pending &&
            snapshot.operation_id == state->stop_operation_id)
    {
        if (snapshot.last_error == ESP_OK &&
                snapshot.state != RECORDER_SERVICE_PLAYING)
        {
            _files_submit_delete(state, &state->pending_delete);
            if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
            {
                return;
            }
        }
        else
        {
            state->pending_after_stop = false;
            _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        }
    }
    if (state->delete_pending && !snapshot.operation_pending &&
            snapshot.operation_id == state->delete_operation_id)
    {
        state->delete_pending = false;
        if (snapshot.last_error == ESP_OK)
        {
            state->has_selection = false;
            _files_set_status(state, "已删除", APP_UI_STATUS_SUCCESS);
        }
        else
        {
            _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
        }
        state->rows_dirty = true;
        reload |= index_current;
    }
    if (reload)
    {
        /* A stale epoch reaches list() only once; publication changes the index
         * epoch and triggers the next copy. All row destruction is timer-safe. */
        _files_reload(state);
    }
    else if (state->rows_dirty)
    {
        _files_rebuild(state);
    }
    const bool playing = snapshot.state == RECORDER_SERVICE_PLAYING;
    app_ui_button_set_text(state->btn_play,
                           playing ? "停止播放" : "播放");
    const bool show_bar = playing && snapshot.playback_duration_ms > 0U;
    recorder_ui_set_visible(state->bar, show_bar);
    if (show_bar)
    {
        lv_bar_set_value(state->bar,
                         (int32_t)(snapshot.playback_position_ms * 100U /
                                   snapshot.playback_duration_ms),
                         LV_ANIM_OFF);
    }
}

static void _files_refresh(lv_timer_t *timer)
{
    _files_render(lv_timer_get_user_data(timer));
}

static void _files_play_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || state->delete_pending ||
            state->pending_after_stop)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        if (recorder_service_stop_playback() != ESP_OK)
        {
            _files_set_status(state, "停止播放失败", APP_UI_STATUS_ERROR);
        }
    }
    else if (state->has_selection)
    {
        if (recorder_service_play(&state->selected) != ESP_OK)
        {
            _files_set_status(state, "播放失败", APP_UI_STATUS_ERROR);
        }
    }
    _files_render(state);
}

static void _files_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || !state->has_selection ||
            state->delete_pending || state->pending_after_stop ||
            state->confirm_sheet != NULL)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_RECORDING ||
            snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        _files_set_status(state, "录音中无法删除", APP_UI_STATUS_WARNING);
        return;
    }
    state->confirmation_target = state->selected;
    state->has_confirmation_target = true;
    char message[96];
    (void)snprintf(message, sizeof(message), "删除“%s”？此操作无法撤销。",
                   recorder_ui_display_name(state->confirmation_target.name));
    state->confirm_sheet = app_ui_sheet_open(state->page.root, "删除录音",
        message);
    if (state->confirm_sheet == NULL)
    {
        state->has_confirmation_target = false;
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "删除", true,
                                  _files_delete_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _files_delete_cancel, state);
}

static void _files_delete_cancel(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    app_ui_sheet_dismiss(state->confirm_sheet);
    state->confirm_sheet = NULL;
    state->has_confirmation_target = false;
}

static void _files_delete_confirm(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (state->has_confirmation_target &&
            recorder_service_get_snapshot(&snapshot) == ESP_OK &&
            !snapshot.operation_pending)
    {
        if (snapshot.state == RECORDER_SERVICE_PLAYING)
        {
            state->pending_delete = state->confirmation_target;
            if (recorder_service_stop_playback() == ESP_OK &&
                    recorder_service_get_snapshot(&snapshot) == ESP_OK)
            {
                state->stop_operation_id = snapshot.operation_id;
                state->pending_after_stop = true;
                _files_set_status(state, "删除中", APP_UI_STATUS_WARNING);
            }
            else
            {
                _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
            }
        }
        else
        {
            _files_submit_delete(state, &state->confirmation_target);
        }
    }
    else
    {
        _files_set_status(state, "删除失败", APP_UI_STATUS_ERROR);
    }
    state->has_confirmation_target = false;
    app_ui_sheet_dismiss(state->confirm_sheet);
    state->confirm_sheet = NULL;
}

static void _files_row_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    const size_t encoded = (size_t)(uintptr_t)
                           lv_obj_get_user_data(lv_event_get_current_target(event));
    if (state == NULL || encoded == 0U || encoded > state->file_count ||
            state->files == NULL || state->delete_pending ||
            state->pending_after_stop)
    {
        return;
    }
    const recorder_service_target_t *target = &state->files[encoded - 1U].target;
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            !snapshot.index_valid ||
            target->generation != snapshot.storage_generation ||
            target->revision != snapshot.content_revision ||
            target->generation != snapshot.index_generation ||
            target->revision != snapshot.index_revision)
    {
        state->has_selection = false;
        state->rows_dirty = true;
        return;
    }
    state->selected = *target;
    state->has_selection = true;
    _files_set_status(state, "", APP_UI_STATUS_NEUTRAL);
    /* The poll timer rebuilds after this row's CLICKED callback returns. */
    state->rows_dirty = true;
}

static void _files_mount(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->last_generation = UINT32_MAX;
    state->last_storage_generation = UINT32_MAX;
    state->last_content_revision = UINT32_MAX;
    state->last_index_generation = UINT32_MAX;
    state->last_index_revision = UINT32_MAX;
    state->last_index_valid = false;
    state->files = calloc(RECORDER_SERVICE_MAX_FILES, sizeof(*state->files));
    app_ui_page_create(&state->page, "播放库", true);
    lv_obj_set_style_pad_bottom(state->page.content, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    state->list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->list);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, 0);
    lv_obj_set_flex_grow(state->list, 1);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 8, 0);
    app_ui_make_passive(state->list, true);
    lv_obj_set_scroll_dir(state->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state->list, LV_SCROLLBAR_MODE_AUTO);

    state->bar = lv_bar_create(state->page.content);
    lv_obj_set_width(state->bar, LV_PCT(100));
    lv_obj_set_height(state->bar, 8);
    lv_bar_set_range(state->bar, 0, 100);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_TRACK),
                              0);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(state->bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(state->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    app_ui_make_passive(state->bar, false);
    recorder_ui_set_visible(state->bar, false);

    state->status_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->btn_play = app_ui_button_create(controls, "播放",
                                           _files_play_event, state);
    app_ui_button_set_kind(state->btn_play, APP_UI_BUTTON_PRIMARY);
    state->btn_delete = app_ui_button_create(controls, "删除",
        _files_delete_event, state);
    app_ui_button_set_kind(state->btn_delete, APP_UI_BUTTON_DANGER);

    state->refresh_timer = lv_timer_create(_files_refresh, 250U, state);
    _files_render(state);
}

static esp_err_t _files_pause(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _files_resume(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    state->last_generation = UINT32_MAX;
    state->rows_dirty = true;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _files_render(state);
}

static void _files_unmount(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    free(state->files);
    state->files = NULL;
    state->file_count = 0U;
    state->has_selection = false;
    state->has_confirmation_target = false;
    state->pending_after_stop = false;
    state->delete_pending = false;
    state->list = NULL;
    state->bar = NULL;
    state->btn_play = NULL;
    state->btn_delete = NULL;
    state->status_label = NULL;
    state->confirm_sheet = NULL;
}

static const app_manager_page_ops_t s_recorder_files_ops =
{
    .mount = _files_mount,
    .resume = _files_resume,
    .pause = _files_pause,
    .unmount = _files_unmount,
};

const app_manager_page_definition_t recorder_files_page_definition =
{
    .ops = &s_recorder_files_ops,
    .memory_size = sizeof(recorder_files_state_t),
};
