#define DBG_TAG "files_detail"
#include "mt_log.h"

#include "files_app_internal.h"

typedef struct files_detail_payload
{
    file_service_target_t target;
    file_service_detail_t detail;
} files_detail_payload_t;

typedef struct files_detail_state
{
    app_ui_page_t page;
    lv_obj_t *name_value;
    lv_obj_t *path_value;
    lv_obj_t *kind_value;
    lv_obj_t *size_value;
    lv_obj_t *time_value;
    lv_obj_t *status_label;
    lv_obj_t *delete_button;
    lv_obj_t *confirm_sheet;
    lv_timer_t *poll_timer;
    uint32_t session;
    uint32_t operation_id;
    bool request_pending;
    bool deleting;
    bool can_delete;
    files_detail_payload_t *data;
} files_detail_state_t;

_Static_assert(sizeof(files_detail_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Files detail state exceeds the lifecycle arena slot");

static bool _files_detail_selection(files_detail_state_t *state)
{
    if (state->data == NULL)
    {
        state->data = files_payload_alloc(sizeof(*state->data));
    }
    return state->data != NULL &&
           files_app_copy_selection(&state->data->target) == ESP_OK;
}

static void _files_detail_set_status(files_detail_state_t *state,
                                     const char *text, app_ui_status_t status)
{
    if (state->status_label != NULL && state->page.root != NULL)
    {
        app_ui_set_status_text(state->status_label, text, status);
    }
}

static void _files_detail_render(files_detail_state_t *state)
{
    file_service_detail_t *detail = &state->data->detail;
    if (file_service_session_get_detail(state->session, detail) != ESP_OK)
    {
        return;
    }
    char size_text[24];
    files_format_size(detail->size_bytes, size_text, sizeof(size_text));
    char time_text[32];
    files_format_time(detail->has_mtime, detail->modified_epoch_s, time_text,
                      sizeof(time_text));

    lv_label_set_text(state->name_value, detail->name);
    /* The full path wraps in a scrollable block instead of being truncated. */
    lv_label_set_text(state->path_value, detail->relative_path);
    lv_label_set_text(state->kind_value,
                      detail->kind == FILE_SERVICE_ENTRY_DIRECTORY ?
                      "目录" : "文件");
    lv_label_set_text(state->size_value,
                      detail->kind == FILE_SERVICE_ENTRY_DIRECTORY ? "--" :
                      size_text);
    lv_label_set_text(state->time_value, time_text);
    state->can_delete = detail->relative_path[0] != '\0';
    state->data->target.generation = detail->generation;
    state->data->target.revision = detail->revision;
    if (state->delete_button != NULL)
    {
        lv_obj_remove_state(state->delete_button, LV_STATE_DISABLED);
    }
    _files_detail_set_status(state, "可以删除", APP_UI_STATUS_NEUTRAL);
}

static void _files_detail_submit(files_detail_state_t *state)
{
    file_service_result_t result = {0};
    if (file_service_session_detail(state->session, &state->data->target,
                                    &result) != ESP_OK)
    {
        _files_detail_set_status(state, files_error_text(result.reason), APP_UI_STATUS_ERROR);
        return;
    }
    state->operation_id = result.operation_id;
    state->request_pending = true;
    _files_detail_set_status(state, "读取中", APP_UI_STATUS_ACCENT);
}

static void _files_detail_poll(files_detail_state_t *state)
{
    if (state->page.root == NULL || !state->request_pending)
    {
        return;
    }
    file_service_result_t result = {0};
    if (file_service_session_get_result(state->session, &result) != ESP_OK)
    {
        return;
    }
    if (result.operation_id != state->operation_id ||
            (result.state != FILE_SERVICE_RESULT_DONE &&
             result.state != FILE_SERVICE_RESULT_FAILED &&
             result.state != FILE_SERVICE_RESULT_CANCELLED))
    {
        return;
    }
    state->request_pending = false;

    if (!state->deleting)
    {
        if (result.state == FILE_SERVICE_RESULT_DONE)
        {
            _files_detail_render(state);
        }
        else
        {
            _files_detail_set_status(state, files_error_text(result.reason),
                                     APP_UI_STATUS_ERROR);
        }
        return;
    }

    state->deleting = false;
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    if (result.state == FILE_SERVICE_RESULT_DONE)
    {
        _files_detail_set_status(state, "已删除", APP_UI_STATUS_SUCCESS);
        files_app_selection_deleted();
        state->can_delete = false;
        if (state->delete_button != NULL)
        {
            lv_obj_add_state(state->delete_button, LV_STATE_DISABLED);
        }
        /* Return to the directory that held the entry so the list reflects the
         * removal. */
        app_ui_request_back();
        return;
    }
    _files_detail_set_status(state, files_error_text(result.reason),
                             APP_UI_STATUS_WARNING);
    if (result.reason == FILE_SERVICE_ERROR_STALE ||
            result.reason == FILE_SERVICE_ERROR_NOT_FOUND ||
            result.reason == FILE_SERVICE_ERROR_IO)
    {
        state->can_delete = false;
        lv_obj_add_state(state->delete_button, LV_STATE_DISABLED);
    }
}

static void _files_detail_timer(lv_timer_t *timer)
{
    _files_detail_poll(lv_timer_get_user_data(timer));
}

static void _files_detail_confirm_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    files_detail_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->request_pending || !state->can_delete)
    {
        return;
    }
    file_service_result_t result = {0};
    if (file_service_session_delete(state->session, &state->data->target,
                                    &result) != ESP_OK)
    {
        _files_detail_set_status(state, files_error_text(result.reason), APP_UI_STATUS_WARNING);
        return;
    }
    state->operation_id = result.operation_id;
    state->request_pending = true;
    state->deleting = true;
    _files_detail_set_status(state, "正在删除", APP_UI_STATUS_ACCENT);
}

static void _files_detail_cancel_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    files_detail_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _files_detail_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    files_detail_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->request_pending || !state->can_delete ||
            state->confirm_sheet != NULL)
    {
        return;
    }
    const bool directory = state->data->detail.kind == FILE_SERVICE_ENTRY_DIRECTORY;
    state->confirm_sheet = app_ui_sheet_open(state->page.root,
        directory ? "删除空目录" : "删除文件",
        directory ? "仅空目录可以删除，目录内的内容不受影响" :
        "此操作无法撤销");
    if (state->confirm_sheet == NULL)
    {
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "删除", true,
                                  _files_detail_confirm_event, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _files_detail_cancel_event, state);
}

/**
 * @brief Add one full-width labelled block that wraps instead of truncating.
 *
 * A wrapped value grows the row, so it sits outside the fixed-height
 * name/value list. Required information — the full name and the full path —
 * must never be hidden behind an ellipsis.
 */
static lv_obj_t *_files_detail_add_wrapped_row(lv_obj_t *parent,
        const char *caption, lv_obj_t **value_out,
        app_theme_font_id_t font)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    app_ui_make_passive(card, false);

    lv_obj_t *caption_label = lv_label_create(card);
    lv_obj_set_style_text_color(caption_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(caption_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(caption_label, caption);
    app_ui_make_passive(caption_label, false);

    lv_obj_t *value = lv_label_create(card);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(value, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(value, app_ui_font(font), 0);
    lv_label_set_text(value, "读取中");
    app_ui_make_passive(value, false);
    *value_out = value;
    return card;
}

static void _files_detail_mount(const app_manager_page_context_t *context)
{
    files_detail_state_t *state = context->state;
    state->request_pending = false;
    state->deleting = false;
    state->can_delete = false;
    app_ui_page_create(&state->page, "文件详情", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);
    lv_obj_set_scrollable(state->page.content, true);
    lv_obj_set_scrollbar_mode(state->page.content, LV_SCROLLBAR_MODE_OFF);

    if (!_files_detail_selection(state))
    {
        (void)app_ui_add_empty_state(state->page.content, "无法打开该文件",
                                     "请从文件列表重新进入");
        return;
    }
    if (file_service_session_open(&state->session) != ESP_OK)
    {
        (void)app_ui_add_empty_state(state->page.content, "打开文件服务失败",
                                     "请返回后重试");
        state->session = 0U;
        return;
    }

    /* The name and the volume path are required information: both wrap
     * instead of being truncated. */
    /* Compact first screen: the delete row and its status must be reachable
     * without scrolling, so the long fields wrap in one shared card and the
     * fixed facts use the single-line value rows. */
    lv_obj_t *action_group = app_ui_group_create(state->page.content);
    state->delete_button = app_ui_group_add_danger(action_group,
        LV_SYMBOL_TRASH, "删除",
        "从存储卡移除该项", _files_detail_delete_event,
        state);
    lv_obj_add_state(state->delete_button, LV_STATE_DISABLED);
    state->status_label = app_ui_add_body_label(state->page.content,
        "读取中");

    lv_obj_t *group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_value(group, "类型", "读取中",
                                 &state->kind_value);
    (void)app_ui_group_add_value(group, "大小", "读取中",
                                 &state->size_value);

    (void)_files_detail_add_wrapped_row(state->page.content, "名称",
                                        &state->name_value,
                                        APP_THEME_FONT_BODY);
    (void)_files_detail_add_wrapped_row(state->page.content, "位置",
                                        &state->path_value,
                                        APP_THEME_FONT_SMALL);
    (void)_files_detail_add_wrapped_row(state->page.content, "修改时间",
                                        &state->time_value,
                                        APP_THEME_FONT_SMALL);

    state->poll_timer = lv_timer_create(_files_detail_timer, 120U, state);
    _files_detail_submit(state);
}

static esp_err_t _files_detail_pause(
    const app_manager_page_context_t *context)
{
    files_detail_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_pause(state->poll_timer);
    }
    return ESP_OK;
}

static void _files_detail_resume(const app_manager_page_context_t *context)
{
    files_detail_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_resume(state->poll_timer);
    }
}

static void _files_detail_unmount(const app_manager_page_context_t *context)
{
    files_detail_state_t *state = context->state;
    if (state->poll_timer != NULL)
    {
        lv_timer_delete(state->poll_timer);
        state->poll_timer = NULL;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    if (state->session != 0U)
    {
        (void)file_service_session_close(state->session);
        state->session = 0U;
    }
    app_ui_page_destroy(&state->page);
    state->name_value = NULL;
    state->path_value = NULL;
    state->kind_value = NULL;
    state->size_value = NULL;
    state->time_value = NULL;
    state->status_label = NULL;
    state->delete_button = NULL;
}

static esp_err_t _files_detail_stop(const app_manager_page_context_t *context)
{
    files_detail_state_t *state = context->state;
    free(state->data);
    state->data = NULL;
    return ESP_OK;
}

static const app_manager_page_ops_t s_files_detail_ops =
{
    .mount = _files_detail_mount,
    .resume = _files_detail_resume,
    .pause = _files_detail_pause,
    .unmount = _files_detail_unmount,
    .stop = _files_detail_stop,
};

const app_manager_page_definition_t files_detail_page_definition =
{
    .ops = &s_files_detail_ops,
    .memory_size = sizeof(files_detail_state_t),
};
