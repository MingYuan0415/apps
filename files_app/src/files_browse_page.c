#define DBG_TAG "files_browse"
#include "mt_log.h"

#include "files_app_internal.h"

typedef struct files_browse_payload
{
    file_service_batch_t batch;
    file_service_target_t target;
    char directory[FILES_PATH_BYTES];
    size_t page_index;
} files_browse_payload_t;

typedef struct files_browse_state
{
    app_ui_page_t page;
    lv_obj_t *path_label;
    lv_obj_t *status_label;
    lv_obj_t *list;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;
    lv_obj_t *refresh_button;
    lv_obj_t *up_button;
    lv_obj_t *detail_button;
    lv_timer_t *poll_timer;
    files_browse_payload_t *data;
    uint32_t session;
    uint32_t operation_id;
    bool request_pending;
    bool clear_list_pending;
    bool has_batch;
    file_service_cursor_t cursor;
    size_t walk_remaining;
} files_browse_state_t;

_Static_assert(sizeof(files_browse_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Files browse state exceeds the lifecycle arena slot");

static void _files_browse_status(files_browse_state_t *state,
                                 const char *text, app_ui_status_t status)
{
    app_ui_set_status_text(state->status_label, text, status);
}

static void _files_browse_buttons(files_browse_state_t *state)
{
    lv_obj_t *buttons[] = {state->prev_button, state->next_button,
                           state->up_button, state->refresh_button,
                           state->detail_button
                          };
    for (size_t index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); ++index)
    {
        if (state->request_pending)
        {
            lv_obj_add_state(buttons[index], LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_remove_state(buttons[index], LV_STATE_DISABLED);
        }
    }
    if (state->data->directory[0] == '\0')
    {
        lv_obj_set_hidden(state->detail_button, true);
    }
    else
    {
        lv_obj_set_hidden(state->detail_button, false);
    }
}

static void _files_browse_submit(files_browse_state_t *state,
                                 const file_service_cursor_t *cursor)
{
    file_service_result_t result = {0};
    state->has_batch = false;
    state->clear_list_pending = true;
    if (file_service_session_list(state->session, state->data->directory,
                                  cursor, &result) != ESP_OK)
    {
        _files_browse_status(state, files_error_text(result.reason),
                             APP_UI_STATUS_ERROR);
        _files_browse_buttons(state);
        return;
    }
    state->operation_id = result.operation_id;
    state->request_pending = true;
    _files_browse_status(state, "读取中", APP_UI_STATUS_ACCENT);
    _files_browse_buttons(state);
}

static void _files_browse_detail_event(lv_event_t *event)
{
    files_browse_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->request_pending || !state->has_batch ||
            state->data->directory[0] == '\0')
    {
        return;
    }
    file_service_target_t *target = &state->data->target;
    strcpy(target->relative_path, state->data->directory);
    target->generation = state->data->batch.generation;
    target->revision = state->data->batch.revision;
    files_request_detail(target);
}

static void _files_browse_row_event(lv_event_t *event)
{
    files_browse_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->request_pending || !state->has_batch)
    {
        return;
    }
    const size_t encoded = (size_t)(uintptr_t)lv_obj_get_user_data(
                               lv_event_get_current_target(event));
    if (encoded == 0U || encoded > state->data->batch.count)
    {
        return;
    }
    const file_service_entry_t *entry = &state->data->batch.entries[encoded - 1U];
    file_service_target_t *target = &state->data->target;
    if (files_path_join(state->data->directory, entry->name,
                        target->relative_path, sizeof(target->relative_path)) != ESP_OK)
    {
        _files_browse_status(state, "路径过长", APP_UI_STATUS_ERROR);
        return;
    }
    target->generation = state->data->batch.generation;
    target->revision = state->data->batch.revision;
    if (entry->kind == FILE_SERVICE_ENTRY_DIRECTORY)
    {
        /* A row stays alive until the timer rebuilds it after this callback. */
        strcpy(state->data->directory, target->relative_path);
        state->data->page_index = 0U;
        lv_label_set_text(state->path_label, state->data->directory);
        _files_browse_submit(state, NULL);
    }
    else
    {
        files_request_detail(target);
    }
}

static void _files_browse_row(files_browse_state_t *state,
                              const file_service_entry_t *entry, size_t index)
{
    const bool directory = entry->kind == FILE_SERVICE_ENTRY_DIRECTORY;
    lv_obj_t *row = lv_button_create(state->list);
    app_ui_click_only(row);
    lv_obj_set_user_data(row, (void *)(uintptr_t)(index + 1U));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, APP_UI_ROW_HEIGHT, 0);
    lv_obj_set_style_radius(row, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_add_event_cb(row, _files_browse_row_event, LV_EVENT_CLICKED, state);
    lv_obj_t *icon = app_ui_image(row, directory ? APP_IMAGE_UI_FOLDER :
                                  APP_IMAGE_UI_FILE, LV_SYMBOL_IMAGE, 24);
    app_ui_icon_set_color(icon, directory ? APP_UI_COLOR_RAIN : APP_UI_COLOR_MUTED);
    app_ui_make_passive(icon, false);
    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_width(name, 0);
    lv_obj_set_height(name, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BODY), 0);
    app_ui_make_passive(name, false);
    lv_label_set_text(name, entry->name);
    if (!directory)
    {
        char text[24];
        files_format_size(entry->size_bytes, text, sizeof(text));
        lv_obj_t *size = lv_label_create(row);
        lv_obj_set_style_text_color(size, lv_color_hex(APP_UI_COLOR_FAINT), 0);
        lv_obj_set_style_text_font(size, app_ui_font(APP_THEME_FONT_SMALL), 0);
        app_ui_make_passive(size, false);
        lv_label_set_text(size, text);
    }
}

static void _files_browse_poll(files_browse_state_t *state)
{
    if (!state->request_pending)
    {
        return;
    }
    file_service_result_t result = {0};
    if (file_service_session_get_result(state->session, &result) != ESP_OK ||
            result.operation_id != state->operation_id ||
            result.state < FILE_SERVICE_RESULT_DONE)
    {
        return;
    }
    state->request_pending = false;
    if (result.state != FILE_SERVICE_RESULT_DONE ||
            file_service_session_get_batch(state->session, &state->data->batch) != ESP_OK)
    {
        _files_browse_status(state, files_error_text(result.reason), APP_UI_STATUS_WARNING);
        _files_browse_buttons(state);
        return;
    }
    (void)file_service_session_get_cursor(state->session, &state->cursor);
    if (state->walk_remaining > 0U && state->data->batch.has_more && state->cursor.valid)
    {
        --state->walk_remaining;
        ++state->data->page_index;
        _files_browse_submit(state, &state->cursor);
        return;
    }
    state->walk_remaining = 0U;
    lv_obj_clean(state->list);
    for (size_t index = 0U; index < state->data->batch.count; ++index)
    {
        _files_browse_row(state, &state->data->batch.entries[index], index);
    }
    if (state->data->batch.count == 0U)
    {
        (void)app_ui_add_empty_state(state->list, "目录为空", "这里还没有文件");
    }
    state->has_batch = true;
    char text[32];
    (void)snprintf(text, sizeof(text), "第 %u 批 · %u 项",
                   (unsigned)state->data->page_index + 1U,
                   (unsigned)state->data->batch.count);
    _files_browse_status(state, text, APP_UI_STATUS_NEUTRAL);
    _files_browse_buttons(state);
}

static void _files_browse_toolbar_event(lv_event_t *event)
{
    files_browse_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || state->request_pending)
    {
        return;
    }
    lv_obj_t *button = lv_event_get_current_target(event);
    if (button == state->next_button)
    {
        if (!state->has_batch || !state->data->batch.has_more || !state->cursor.valid)
        {
            _files_browse_status(state, "已经是最后一批", APP_UI_STATUS_NEUTRAL);
            return;
        }
        ++state->data->page_index;
        _files_browse_submit(state, &state->cursor);
        return;
    }
    if (button == state->prev_button)
    {
        if (state->data->page_index == 0U)
        {
            _files_browse_status(state, "已经是第一批", APP_UI_STATUS_NEUTRAL);
            return;
        }
        state->walk_remaining = state->data->page_index - 1U;
    }
    else if (button == state->up_button)
    {
        if (state->data->directory[0] == '\0')
        {
            _files_browse_status(state, "已在存储卡根目录", APP_UI_STATUS_NEUTRAL);
            return;
        }
        files_path_parent(state->data->directory);
        lv_label_set_text(state->path_label,
                          state->data->directory[0] ? state->data->directory : "存储卡 /");
    }
    state->data->page_index = 0U;
    _files_browse_submit(state, NULL);
}

static void _files_browse_timer(lv_timer_t *timer)
{
    files_browse_state_t *state = lv_timer_get_user_data(timer);
    if (state->clear_list_pending)
    {
        if (state->list != NULL)
        {
            lv_obj_clean(state->list);
        }
        state->clear_list_pending = false;
    }
    _files_browse_poll(state);
}

static void _files_browse_mount(const app_manager_page_context_t *context)
{
    files_browse_state_t *state = context->state;
    if (state->data == NULL)
    {
        state->data = files_payload_alloc(sizeof(*state->data));
    }
    app_ui_page_create(&state->page, "文件管理", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);
    lv_obj_set_scrollable(state->page.content, true);
    lv_obj_set_scrollbar_mode(state->page.content, LV_SCROLLBAR_MODE_OFF);
    if (state->data == NULL || file_service_session_open(&state->session) != ESP_OK)
    {
        (void)app_ui_add_empty_state(state->page.content, "打开文件服务失败", "请返回后重试");
        return;
    }
    if (files_app_restore_directory(state->data->directory))
    {
        state->data->page_index = 0U;
    }
    state->request_pending = false;
    state->has_batch = false;
    state->walk_remaining = state->data->page_index;
    state->data->page_index = 0U;
    state->path_label = app_ui_add_body_label(state->page.content,
        state->data->directory[0] ? state->data->directory : "存储卡 /");
    lv_obj_t *toolbar = app_ui_button_row_create(state->page.content, APP_UI_BUTTON_HEIGHT);
    state->up_button = app_ui_button_create(toolbar, "上级", _files_browse_toolbar_event, state);
    state->refresh_button = app_ui_button_create(toolbar, "刷新", _files_browse_toolbar_event, state);
    state->prev_button = app_ui_button_create(toolbar, "上一批", _files_browse_toolbar_event, state);
    state->next_button = app_ui_button_create(toolbar, "下一批", _files_browse_toolbar_event, state);
    state->detail_button = app_ui_button_create(state->page.content, "当前目录详情",
        _files_browse_detail_event, state);
    lv_obj_set_flex_grow(state->detail_button, 0);
    lv_obj_set_width(state->detail_button, LV_PCT(100));
    lv_obj_set_height(state->detail_button, APP_UI_BUTTON_HEIGHT);
    state->status_label = app_ui_add_body_label(state->page.content, "读取中");
    state->list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->list);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(state->list, false);
    state->poll_timer = lv_timer_create(_files_browse_timer, 120U, state);
    _files_browse_submit(state, NULL);
}

static esp_err_t _files_browse_pause(const app_manager_page_context_t *context)
{
    files_browse_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_pause(state->poll_timer);
    }
    return ESP_OK;
}

static void _files_browse_resume(const app_manager_page_context_t *context)
{
    files_browse_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_resume(state->poll_timer);
    }
}

static void _files_browse_unmount(const app_manager_page_context_t *context)
{
    files_browse_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_delete(state->poll_timer);
        state->poll_timer = NULL;
    }
    if (state->session != 0U)
    {
        (void)file_service_session_close(state->session);
        state->session = 0U;
    }
    app_ui_page_destroy(&state->page);
    state->path_label = NULL;
    state->status_label = NULL;
    state->list = NULL;
    state->prev_button = NULL;
    state->next_button = NULL;
    state->refresh_button = NULL;
    state->up_button = NULL;
    state->detail_button = NULL;
    state->request_pending = false;
    state->has_batch = false;
}

static esp_err_t _files_browse_stop(const app_manager_page_context_t *context)
{
    files_browse_state_t *state = context->state;
    free(state->data);
    state->data = NULL;
    files_app_clear_selection();
    return ESP_OK;
}

static const app_manager_page_ops_t s_files_browse_ops =
{
    .mount = _files_browse_mount,
    .resume = _files_browse_resume,
    .pause = _files_browse_pause,
    .unmount = _files_browse_unmount,
    .stop = _files_browse_stop,
};

const app_manager_page_definition_t files_browse_page_definition =
{
    .ops = &s_files_browse_ops,
    .memory_size = sizeof(files_browse_state_t),
};
