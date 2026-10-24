#define DBG_TAG "recorder_files"
#include "mt_log.h"

#include "recorder_app_internal.h"

#include <stdlib.h>

/* Feedback such as "已删除 2 个录音" lives for a fixed number of 250 ms polls,
 * the same deterministic receipt the root Page uses. */
#define RECORDER_FILES_RECEIPT_POLLS 20U

/**
 * @brief Selected recordings captured when a batch starts.
 *
 * The service republishes the index after every removal, so a batch cannot keep
 * `recorder_service_target_t` values captured up front: their revision goes
 * stale after the first delete. The full path is the only stable identity, and
 * each submission rebuilds the epoch from the live snapshot instead.
 */
typedef struct recorder_batch
{
    size_t count;
    char names[RECORDER_SERVICE_MAX_FILES][RECORDER_SERVICE_PATH_BYTES];
} recorder_batch_t;

typedef struct recorder_files_state
{
    app_ui_page_t page;
    lv_obj_t *list;
    lv_obj_t *notice_label;
    lv_obj_t *selection_bar;
    lv_obj_t *btn_select_all;
    lv_obj_t *btn_delete;
    lv_obj_t *btn_cancel;
    lv_obj_t *confirm_sheet;
    lv_timer_t *refresh_timer;
    recorder_service_file_t *files;
    recorder_batch_t *batch;
    /* Row handles survive a selection change so toggling never rebuilds the
     * list (and never loses the scroll position). */
    lv_obj_t *rows[RECORDER_SERVICE_MAX_FILES];
    lv_obj_t *marks[RECORDER_SERVICE_MAX_FILES];
    size_t file_count;
    size_t batch_index;
    size_t batch_done;
    uint32_t selected_mask;
    uint32_t delete_operation_id;
    uint32_t stop_operation_id;
    uint32_t last_generation;
    uint32_t last_storage_generation;
    uint32_t last_content_revision;
    uint32_t last_index_generation;
    uint32_t last_index_revision;
    recorder_service_state_t last_state;
    uint16_t receipt_polls;
    bool receipt_success;
    char receipt[48];
    char notice[48];
    bool last_index_valid;
    bool last_truncated;
    bool rows_dirty;
    bool selection_mode;
    bool suppress_click;
    bool delete_pending;
    bool after_stop;
    bool batch_active;
    /* Set when a batch retires: the rows it froze are now stale by design, so
     * the next frame must copy a fresh list regardless of epoch bookkeeping. */
    bool reload_needed;
} recorder_files_state_t;

_Static_assert(sizeof(recorder_files_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder files state exceeds the lifecycle arena slot");

static void _files_row_event(lv_event_t *event);

static uint32_t _files_selected_count(const recorder_files_state_t *state)
{
    uint32_t count = 0U;
    for (size_t index = 0U; index < RECORDER_SERVICE_MAX_FILES; ++index)
    {
        count += (state->selected_mask >> index) & 1U;
    }
    return count;
}

/**
 * @brief Paint one row for the current mode and selection.
 *
 * The trailing glyph carries the state: a chevron while browsing, a check while
 * selecting. Both come from the same label, so a row never grows a control.
 */
static void _files_apply_row(recorder_files_state_t *state, size_t index)
{
    if (index >= RECORDER_SERVICE_MAX_FILES)
    {
        return;
    }
    lv_obj_t *row = state->rows[index];
    lv_obj_t *mark = state->marks[index];
    if (row == NULL || mark == NULL)
    {
        return;
    }
    const bool selected = state->selection_mode &&
                          (state->selected_mask & (1UL << index)) != 0U;
    /* lv_obj_set_local_style_prop always invalidates the widget, so painting
     * every row on every pass would repaint the whole list four times a second
     * for a pixel-identical result. Only the rows that changed state are
     * touched, keyed off the mark label that carries the mode. */
    const char *const glyph = state->selection_mode ?
                              (selected ? LV_SYMBOL_OK : LV_SYMBOL_DUMMY) :
                              LV_SYMBOL_RIGHT;
    if (lv_label_get_text(mark) != NULL &&
            strcmp(lv_label_get_text(mark), glyph) == 0)
    {
        return;
    }
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ?
            APP_UI_COLOR_RAIN_TINT : APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(mark, lv_color_hex(selected ?
            APP_UI_COLOR_RAIN : APP_UI_COLOR_FAINT), 0);
    lv_label_set_text(mark, glyph);
}

static void _files_update_selection_ui(recorder_files_state_t *state)
{
    const uint32_t selected = _files_selected_count(state);
    char text[24];
    (void)snprintf(text, sizeof(text), "删除 %u", (unsigned)selected);
    app_ui_button_set_text(state->btn_delete, text);
    app_ui_button_set_text(state->btn_select_all,
                           selected == state->file_count ? "全不选" : "全选");
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        _files_apply_row(state, index);
    }
}

static void _files_exit_selection(recorder_files_state_t *state)
{
    state->selection_mode = false;
    state->selected_mask = 0U;
    recorder_ui_set_visible(state->selection_bar, false);
    for (size_t index = 0U; index < RECORDER_SERVICE_MAX_FILES; ++index)
    {
        _files_apply_row(state, index);
    }
}

static void _files_enter_selection(recorder_files_state_t *state, size_t index)
{
    state->selection_mode = true;
    state->selected_mask = 1UL << index;
    recorder_ui_set_visible(state->selection_bar, true);
    _files_update_selection_ui(state);
}

static void _files_add_row(recorder_files_state_t *state,
                           const recorder_service_file_t *file,
                           size_t file_index)
{
    char title[32];
    char subtitle[32];
    recorder_ui_format_time(file, title, sizeof(title));
    recorder_ui_format_mmss(file->duration_ms, subtitle, sizeof(subtitle));

    lv_obj_t *row = lv_button_create(state->list);
    app_ui_click_only(row);
    lv_obj_set_user_data(row, (void *)(uintptr_t)(file_index + 1U));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, APP_UI_ROW_HEIGHT_TALL);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
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
    /* One callback sees pressed/long-pressed/clicked: the long press must not
     * also navigate, and the release that follows it is swallowed in place. */
    lv_obj_add_event_cb(row, _files_row_event, LV_EVENT_ALL, state);

    lv_obj_t *text = lv_obj_create(row);
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(text, false);

    lv_obj_t *name = lv_label_create(text);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(name, title);
    app_ui_make_passive(name, false);

    lv_obj_t *duration = lv_label_create(text);
    lv_obj_set_width(duration, LV_PCT(100));
    lv_obj_set_style_text_color(duration, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(duration, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(duration, subtitle);
    app_ui_make_passive(duration, false);

    lv_obj_t *chevron = lv_label_create(row);
    lv_obj_set_style_text_color(chevron, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_obj_set_style_text_font(chevron, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    app_ui_make_passive(chevron, false);

    state->rows[file_index] = row;
    state->marks[file_index] = chevron;
}

static void _files_rebuild(recorder_files_state_t *state)
{
    lv_obj_clean(state->list);
    memset(state->rows, 0, sizeof(state->rows));
    memset(state->marks, 0, sizeof(state->marks));
    state->rows_dirty = false;
    if (state->files == NULL)
    {
        (void)app_ui_add_empty_state(state->list, "列表暂不可用",
                                     "请稍后重试");
        return;
    }
    if (state->file_count == 0U)
    {
        /* The last removal of a batch leaves nothing to keep selected. */
        _files_exit_selection(state);
        /* A batch in flight would flash the empty state between removals. */
        if (!state->batch_active)
        {
            (void)app_ui_add_empty_state(state->list, "暂无已完成录音",
                                         "录音完成后会出现在这里");
        }
        return;
    }
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        _files_add_row(state, &state->files[index], index);
        _files_apply_row(state, index);
    }
    if (state->last_truncated)
    {
        char text[64];
        (void)snprintf(text, sizeof(text), "仅显示最新的 %u 个录音",
                       (unsigned)state->file_count);
        lv_obj_t *footer = app_ui_add_body_label(state->list, text);
        lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(footer, app_ui_font(APP_THEME_FONT_SMALL),
                                   0);
        lv_obj_set_style_text_color(footer,
                                    lv_color_hex(APP_UI_COLOR_FAINT), 0);
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
    _files_rebuild(state);
}

static void _files_set_receipt(recorder_files_state_t *state, const char *text,
                               bool success)
{
    (void)snprintf(state->receipt, sizeof(state->receipt), "%s", text);
    state->receipt_polls = RECORDER_FILES_RECEIPT_POLLS;
    state->receipt_success = success;
}

static void _files_end_batch(recorder_files_state_t *state, esp_err_t error)
{
    const size_t removed = state->batch_done;
    state->batch_active = false;
    state->delete_pending = false;
    state->after_stop = false;
    state->batch_index = 0U;
    state->batch_done = 0U;
    free(state->batch);
    state->batch = NULL;
    state->reload_needed = true;
    if (error == ESP_OK)
    {
        char text[48];
        (void)snprintf(text, sizeof(text), "已删除 %u 个录音",
                       (unsigned)removed);
        _files_set_receipt(state, text, true);
    }
    else
    {
        _files_set_receipt(state, recorder_ui_error_text(error), false);
    }
    _files_exit_selection(state);
}

/**
 * @brief Submit the next removal, or retire the batch once it is complete.
 *
 * Deletes are admitted one at a time. The epoch of each target is read from the
 * live snapshot rather than from the captured metadata, which the service
 * invalidates as soon as the previous removal publishes a new revision.
 */
static void _files_batch_submit(recorder_files_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _files_end_batch(state, ESP_ERR_INVALID_STATE);
        return;
    }
    if (state->batch_index >= state->batch->count)
    {
        _files_end_batch(state, ESP_OK);
        return;
    }
    if (!snapshot.storage_ready ||
            snapshot.state == RECORDER_SERVICE_RECORDING ||
            snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        _files_end_batch(state, ESP_ERR_INVALID_STATE);
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        /* Playback is a precondition, not a retry: the removal follows the
         * completed stop through the poll timer. */
        if (recorder_service_stop_playback() != ESP_OK ||
                recorder_service_get_snapshot(&snapshot) != ESP_OK)
        {
            _files_end_batch(state, ESP_ERR_INVALID_STATE);
            return;
        }
        state->stop_operation_id = snapshot.operation_id;
        state->after_stop = true;
        return;
    }
    recorder_service_target_t target = {0};
    (void)snprintf(target.name, sizeof(target.name), "%s",
                   state->batch->names[state->batch_index]);
    target.generation = snapshot.storage_generation;
    target.revision = snapshot.content_revision;
    const esp_err_t result = recorder_service_delete(&target);
    if (result != ESP_OK || recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _files_end_batch(state, result != ESP_OK ? result : ESP_ERR_INVALID_STATE);
        return;
    }
    state->delete_operation_id = snapshot.operation_id;
    state->delete_pending = true;
    ++state->batch_index;
}

static void _files_batch_poll(recorder_files_state_t *state)
{
    if (!state->batch_active || state->batch == NULL)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _files_end_batch(state, ESP_ERR_INVALID_STATE);
        return;
    }
    if (snapshot.operation_pending)
    {
        return;
    }
    if (state->after_stop)
    {
        if (snapshot.operation_id != state->stop_operation_id)
        {
            return;
        }
        state->after_stop = false;
        if (snapshot.last_error != ESP_OK)
        {
            _files_end_batch(state, snapshot.last_error);
            return;
        }
    }
    else if (state->delete_pending)
    {
        if (snapshot.operation_id != state->delete_operation_id)
        {
            return;
        }
        state->delete_pending = false;
        if (snapshot.last_error != ESP_OK)
        {
            _files_end_batch(state, snapshot.last_error);
            return;
        }
        ++state->batch_done;
    }
    _files_batch_submit(state);
}

static void _files_start_batch(recorder_files_state_t *state)
{
    if (state->files == NULL || state->batch != NULL)
    {
        return;
    }
    recorder_batch_t *batch = recorder_payload_alloc(sizeof(*batch));
    if (batch == NULL)
    {
        _files_set_receipt(state, "内存不足", false);
        _files_exit_selection(state);
        return;
    }
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        if ((state->selected_mask & (1UL << index)) == 0U)
        {
            continue;
        }
        (void)snprintf(batch->names[batch->count],
                       sizeof(batch->names[0]), "%s",
                       state->files[index].target.name);
        ++batch->count;
    }
    if (batch->count == 0U)
    {
        free(batch);
        _files_exit_selection(state);
        return;
    }
    state->batch = batch;
    state->batch_active = true;
    state->batch_index = 0U;
    state->batch_done = 0U;
    state->delete_pending = false;
    state->after_stop = false;
    _files_exit_selection(state);
    /* Start the first removal here: the sheet action already decided, so the
     * user should not wait for the next poll to see it acknowledged. */
    _files_batch_submit(state);
}

static void _files_set_notice(recorder_files_state_t *state,
                              const char *text, app_ui_status_t status)
{
    app_ui_set_status_text(state->notice_label, text, status);
}

static void _files_render(recorder_files_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    _files_batch_poll(state);
    const bool epoch_changed =
        state->last_storage_generation != snapshot.storage_generation ||
        state->last_content_revision != snapshot.content_revision ||
        state->last_index_generation != snapshot.index_generation ||
        state->last_index_revision != snapshot.index_revision ||
        state->last_index_valid != snapshot.index_valid;
    const bool index_current = snapshot.index_valid &&
                               snapshot.index_generation == snapshot.storage_generation &&
                               snapshot.index_revision == snapshot.content_revision;
    /* A batch runs while the service republishes the index after every removal,
     * and a stale index reports no rows. Reloading then would blank the list
     * and renumber the survivors under the running batch, so the batch keeps the
     * rows it started with and the reload happens when it retires. */
    const bool reload = !state->batch_active &&
                        (state->reload_needed ||
                         epoch_changed ||
                         (index_current &&
                          (snapshot.generation != state->last_generation ||
                           snapshot.state != state->last_state)) ||
                         state->last_truncated != snapshot.library_truncated);
    state->last_truncated = snapshot.library_truncated;
    state->last_storage_generation = snapshot.storage_generation;
    state->last_content_revision = snapshot.content_revision;
    state->last_index_generation = snapshot.index_generation;
    state->last_index_revision = snapshot.index_revision;
    state->last_index_valid = snapshot.index_valid;
    state->last_generation = snapshot.generation;
    state->last_state = snapshot.state;
    if (reload)
    {
        /* A stale epoch reaches list() only once; publication changes the index
         * epoch and triggers the next copy. All row destruction is timer-safe. */
        state->reload_needed = false;
        _files_reload(state);
    }
    else if (state->rows_dirty)
    {
        _files_rebuild(state);
    }
    /* One notice slot states why the list may be short or missing; it never
     * doubles as a control, so there is no idle dead-button state. */
    if (state->receipt_polls > 0U)
    {
        --state->receipt_polls;
    }
    if (state->batch_active)
    {
        (void)snprintf(state->notice, sizeof(state->notice),
                       "正在删除 %u/%u",
                       (unsigned)(state->batch_done + 1U),
                       (unsigned)state->batch->count);
        _files_set_notice(state, state->notice, APP_UI_STATUS_WARNING);
    }
    else if (state->receipt_polls > 0U)
    {
        _files_set_notice(state, state->receipt,
                          state->receipt_success ? APP_UI_STATUS_SUCCESS :
                          APP_UI_STATUS_ERROR);
    }
    else if (!snapshot.storage_ready)
    {
        _files_set_notice(state, "存储卡未挂载", APP_UI_STATUS_WARNING);
    }
    else if (!snapshot.index_valid)
    {
        _files_set_notice(state, "正在读取录音", APP_UI_STATUS_ACCENT);
    }
    else if (snapshot.state == RECORDER_SERVICE_RECORDING ||
             snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        _files_set_notice(state, "录音中，可稍后编辑", APP_UI_STATUS_ACCENT);
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        _files_set_notice(state, "正在播放", APP_UI_STATUS_ACCENT);
    }
    else if (state->selection_mode)
    {
        (void)snprintf(state->notice, sizeof(state->notice), "已选 %u 个",
                       (unsigned)_files_selected_count(state));
        _files_set_notice(state, state->notice, APP_UI_STATUS_ACCENT);
    }
    else if (state->file_count > 0U)
    {
        _files_set_notice(state, "长按录音可多选删除", APP_UI_STATUS_NEUTRAL);
    }
    else
    {
        _files_set_notice(state, "", APP_UI_STATUS_NEUTRAL);
    }
}

static void _files_refresh(lv_timer_t *timer)
{
    _files_render(lv_timer_get_user_data(timer));
}

/** @brief Whether the library may be edited right now. */
static bool _files_editable(const recorder_service_snapshot_t *snapshot)
{
    return snapshot->storage_ready && snapshot->index_valid &&
           snapshot->state != RECORDER_SERVICE_RECORDING &&
           snapshot->state != RECORDER_SERVICE_PAUSED;
}

static void _files_toggle(recorder_files_state_t *state, size_t index)
{
    const uint32_t bit = 1UL << index;
    state->selected_mask ^= bit;
    if (state->selected_mask == 0U)
    {
        _files_exit_selection(state);
        return;
    }
    _files_update_selection_ui(state);
}

static void _files_row_event(lv_event_t *event)
{
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    const size_t encoded = (size_t)(uintptr_t)
                           lv_obj_get_user_data(lv_event_get_current_target(event));
    if (encoded == 0U || encoded > state->file_count || state->files == NULL)
    {
        return;
    }
    const size_t index = encoded - 1U;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
    {
        /* A new touch decides for itself whether it becomes a click. */
        state->suppress_click = false;
        return;
    }
    if (code == LV_EVENT_LONG_PRESSED)
    {
        if (state->batch_active)
        {
            return;
        }
        recorder_service_snapshot_t snapshot;
        if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
                !_files_editable(&snapshot))
        {
            _files_set_receipt(state, "录音中无法编辑", false);
            return;
        }
        /* The release that ends this press still reports CLICKED; it must not
         * also open the detail Page or toggle the row a second time. */
        state->suppress_click = true;
        if (state->selection_mode)
        {
            _files_toggle(state, index);
        }
        else
        {
            _files_enter_selection(state, index);
        }
        return;
    }
    if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    if (state->suppress_click)
    {
        state->suppress_click = false;
        return;
    }
    if (state->batch_active)
    {
        return;
    }
    if (state->selection_mode)
    {
        _files_toggle(state, index);
        return;
    }
    const recorder_service_file_t *file = &state->files[index];
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            !snapshot.index_valid || !snapshot.storage_ready ||
            file->target.generation != snapshot.storage_generation ||
            file->target.revision != snapshot.content_revision ||
            file->target.generation != snapshot.index_generation ||
            file->target.revision != snapshot.index_revision)
    {
        /* A renamed/replaced entry must not be opened under a stale identity. */
        _files_set_notice(state, "列表已变化，请重试", APP_UI_STATUS_WARNING);
        state->rows_dirty = true;
        return;
    }
    recorder_app_select(file);
    app_ui_request_open_page(APP_MANAGER_ID_RECORDER, RECORDER_PAGE_DETAIL);
}

static void _files_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    _files_exit_selection(lv_event_get_user_data(event));
}

static void _files_select_all_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->batch_active)
    {
        return;
    }
    if (_files_selected_count(state) == state->file_count)
    {
        _files_exit_selection(state);
        return;
    }
    state->selected_mask = 0U;
    for (size_t index = 0U; index < state->file_count; ++index)
    {
        state->selected_mask |= 1UL << index;
    }
    _files_update_selection_ui(state);
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
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    _files_start_batch(state);
}

static void _files_delete_cancel(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _files_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->confirm_sheet != NULL ||
            state->batch_active || !state->selection_mode)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            !_files_editable(&snapshot))
    {
        _files_set_receipt(state, "录音中无法编辑", false);
        return;
    }
    const uint32_t selected = _files_selected_count(state);
    if (selected == 0U)
    {
        return;
    }
    char message[64];
    (void)snprintf(message, sizeof(message), "删除 %u 个录音？此操作无法撤销。",
                   (unsigned)selected);
    state->confirm_sheet = app_ui_sheet_open(state->page.root, "删除录音",
        message);
    if (state->confirm_sheet == NULL)
    {
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "删除", true,
                                  _files_delete_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _files_delete_cancel, state);
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
    state->last_state = RECORDER_SERVICE_ERROR;
    state->files = recorder_payload_alloc(RECORDER_SERVICE_MAX_FILES *
                                          sizeof(*state->files));
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
    lv_obj_set_style_pad_row(state->list, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(state->list, true);
    lv_obj_set_scroll_dir(state->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state->list, LV_SCROLLBAR_MODE_AUTO);

    state->notice_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->notice_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->notice_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->notice_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);

    /* The batch bar is a mode, not a permanent slot: it takes its height only
     * while selecting, so browsing keeps the full list height. */
    state->selection_bar = app_ui_button_row_create(state->page.root,
        APP_UI_BUTTON_HEIGHT);
    lv_obj_set_style_pad_left(state->selection_bar, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_style_pad_right(state->selection_bar, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_style_pad_bottom(state->selection_bar, APP_UI_PAGE_MARGIN, 0);
    state->btn_cancel = app_ui_button_create(state->selection_bar, "取消",
        _files_cancel_event, state);
    state->btn_select_all = app_ui_button_create(state->selection_bar, "全选",
        _files_select_all_event, state);
    state->btn_delete = app_ui_button_create(state->selection_bar, "删除",
        _files_delete_event, state);
    app_ui_button_set_kind(state->btn_delete, APP_UI_BUTTON_DANGER);
    recorder_ui_set_visible(state->selection_bar, false);

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
    /* Leaving the Page abandons the rest of a batch: each removal is already
     * atomic, so the library is consistent and the next mount reloads it. */
    free(state->batch);
    state->batch = NULL;
    free(state->files);
    state->files = NULL;
    state->file_count = 0U;
    state->list = NULL;
    state->notice_label = NULL;
    state->selection_bar = NULL;
    state->btn_cancel = NULL;
    state->btn_select_all = NULL;
    state->btn_delete = NULL;
    state->confirm_sheet = NULL;
    memset(state->rows, 0, sizeof(state->rows));
    memset(state->marks, 0, sizeof(state->marks));
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
