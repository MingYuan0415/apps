#define DBG_TAG "settings_storage"
#include "mt_log.h"

#include "settings_app_internal.h"
#include "file_service.h"

typedef struct settings_storage_state
{
    app_ui_page_t page;
    lv_obj_t *state_value;
    lv_obj_t *capacity_value;
    lv_obj_t *free_value;
    lv_obj_t *operation_value;
    lv_obj_t *status_label;
    lv_obj_t *mount_button;
    lv_obj_t *refresh_button;
    lv_obj_t *unmount_button;
    lv_obj_t *format_button;
    lv_obj_t *recover_button;
    lv_obj_t *confirm_sheet;
    lv_timer_t *refresh_timer;
    bool operation_pending;
    bool confirming;
    file_service_storage_action_t pending_action;
    uint32_t generation;
    uint32_t confirmation_generation;
} settings_storage_state_t;

_Static_assert(sizeof(settings_storage_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Settings storage state exceeds the lifecycle arena slot");

static const char *_storage_volume_text(file_service_volume_state_t volume)
{
    switch (volume)
    {
    case FILE_SERVICE_VOLUME_READY:
        return "已挂载";
    case FILE_SERVICE_VOLUME_BUSY:
        return "正在使用";
    case FILE_SERVICE_VOLUME_MAINTENANCE:
        return "维护中";
    default:
        return "未挂载";
    }
}

static const char *_storage_error_text(file_service_error_reason_t reason)
{
    switch (reason)
    {
    case FILE_SERVICE_ERROR_UNAVAILABLE:
        return "存储卡未挂载，请前往存储管理重新挂载";
    case FILE_SERVICE_ERROR_BUSY:
        return "正在使用，请稍后重试";
    case FILE_SERVICE_ERROR_STALE:
        return "存储卡已变化，请重新确认";
    case FILE_SERVICE_ERROR_PERMISSION:
        return "存储卡无写入权限";
    case FILE_SERVICE_ERROR_NO_SPACE:
        return "存储空间不足";
    case FILE_SERVICE_ERROR_IO:
        return "存储卡读写故障，请重新挂载";
    case FILE_SERVICE_ERROR_NO_MEMORY:
        return "内存不足";
    default:
        return "操作失败，可重新挂载后重试";
    }
}

static void _storage_set_status(settings_storage_state_t *state,
                                const char *text, app_ui_status_t status)
{
    if (state->status_label != NULL && state->page.root != NULL)
    {
        app_ui_set_status_text(state->status_label, text, status);
    }
}

static void _storage_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

static void _storage_render(settings_storage_state_t *state)
{
    file_service_snapshot_t snapshot;
    const bool available = file_service_get_snapshot(&snapshot) == ESP_OK;
    const file_service_volume_state_t volume = available ? snapshot.volume :
        FILE_SERVICE_VOLUME_UNAVAILABLE;
    const bool mounted = volume == FILE_SERVICE_VOLUME_READY ||
                         volume == FILE_SERVICE_VOLUME_BUSY;
    const bool idle = available && !snapshot.busy &&
                      volume != FILE_SERVICE_VOLUME_MAINTENANCE;
    const bool busy_with_user = volume == FILE_SERVICE_VOLUME_BUSY;
    if (available)
    {
        state->generation = snapshot.generation;
    }

    app_ui_set_status_text(state->state_value,
                           available ? (snapshot.faulted ? "存储故障" : _storage_volume_text(volume)) :
                           "服务不可用",
                           !available ? APP_UI_STATUS_ERROR :
                           mounted ? (busy_with_user ? APP_UI_STATUS_WARNING :
                                      APP_UI_STATUS_SUCCESS) :
                           APP_UI_STATUS_WARNING);

    if (available && snapshot.space.valid)
    {
        char text[32];
        (void)snprintf(text, sizeof(text), "%llu MB",
                       (unsigned long long)(snapshot.space.total_bytes /
                                            (1024ULL * 1024ULL)));
        lv_label_set_text(state->capacity_value, text);
        (void)snprintf(text, sizeof(text), "%llu MB",
                       (unsigned long long)(snapshot.space.free_bytes /
                                            (1024ULL * 1024ULL)));
        lv_label_set_text(state->free_value, text);
    }
    else
    {
        /* A failed query is unknown capacity, not zero bytes. */
        lv_label_set_text(state->capacity_value, "未知");
        lv_label_set_text(state->free_value, "未知");
    }

    const char *operation = "空闲";
    if (available)
    {
        switch (snapshot.storage.state)
        {
        case FILE_SERVICE_RESULT_ACCEPTED:
        case FILE_SERVICE_RESULT_RUNNING:
            operation = "进行中";
            break;
        case FILE_SERVICE_RESULT_FAILED:
            operation = "上次操作失败";
            break;
        default:
            operation = "空闲";
            break;
        }
    }
    lv_label_set_text(state->operation_value, operation);

    state->operation_pending = available && snapshot.busy;
    _storage_button_enabled(state->mount_button, idle && !mounted);
    /* Refresh is non-destructive; errors retain unknown rather than zero. */
    _storage_button_enabled(state->refresh_button, true);
    _storage_button_enabled(state->unmount_button, idle && mounted &&
                            !busy_with_user);
    _storage_button_enabled(state->format_button, idle && mounted &&
                            !busy_with_user);
    _storage_button_enabled(state->recover_button, idle && !mounted);
    lv_obj_set_hidden(state->recover_button, mounted);
    lv_obj_set_hidden(state->format_button, !mounted);

    if (!available)
    {
        _storage_set_status(state, "文件服务不可用", APP_UI_STATUS_ERROR);
    }
    else if (state->operation_pending)
    {
        _storage_set_status(state, "正在执行，请稍候", APP_UI_STATUS_ACCENT);
    }
    else if (busy_with_user)
    {
        _storage_set_status(state, "正在使用，停止录音或播放后可操作",
                            APP_UI_STATUS_WARNING);
    }
    else if (snapshot.storage.state == FILE_SERVICE_RESULT_FAILED)
    {
        _storage_set_status(state, _storage_error_text(snapshot.storage.reason),
                            APP_UI_STATUS_ERROR);
    }
    else if (snapshot.volume == FILE_SERVICE_VOLUME_UNAVAILABLE)
    {
        _storage_set_status(state, snapshot.faulted ?
                            "存储故障，可重新挂载后重试" : "可重新挂载或格式化恢复",
                            APP_UI_STATUS_WARNING);
    }
    else
    {
        _storage_set_status(state, "存储卡可用", APP_UI_STATUS_SUCCESS);
    }
}

static void _storage_submit(settings_storage_state_t *state,
                            file_service_storage_action_t action, uint32_t generation)
{
    if (state->operation_pending)
    {
        _storage_set_status(state, "正在执行，请稍候", APP_UI_STATUS_ACCENT);
        return;
    }
    file_service_result_t result = {0};
    const esp_err_t submit = file_service_request_storage(action, generation, &result);
    if (submit != ESP_OK)
    {
        _storage_set_status(state, _storage_error_text(result.reason),
                            APP_UI_STATUS_WARNING);
        return;
    }
    state->operation_pending = true;
    _storage_set_status(state, "正在执行，请稍候", APP_UI_STATUS_ACCENT);
    _storage_render(state);
}

static void _storage_mount_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _storage_submit(state, FILE_SERVICE_STORAGE_MOUNT, state->generation);
    }
}

static void _storage_refresh_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _storage_submit(state, FILE_SERVICE_STORAGE_REFRESH_SPACE, state->generation);
    }
}

static void _storage_unmount_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _storage_submit(state, FILE_SERVICE_STORAGE_UNMOUNT, state->generation);
    }
}

static void _storage_format_confirm(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    state->confirming = false;
    _storage_submit(state, state->pending_action, state->confirmation_generation);
}

static void _storage_format_cancel(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    state->confirming = false;
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _storage_open_confirm(settings_storage_state_t *state,
                                  file_service_storage_action_t action)
{
    if (state->operation_pending || state->confirming ||
            state->confirm_sheet != NULL)
    {
        return;
    }
    const bool recovery = action == FILE_SERVICE_STORAGE_RECOVER_FORMAT;
    file_service_snapshot_t snapshot;
    if (file_service_get_snapshot(&snapshot) != ESP_OK || snapshot.busy ||
            snapshot.volume == FILE_SERVICE_VOLUME_BUSY ||
            (recovery && snapshot.volume != FILE_SERVICE_VOLUME_UNAVAILABLE) ||
            (!recovery && snapshot.volume != FILE_SERVICE_VOLUME_READY))
    {
        _storage_set_status(state, "当前状态无法执行该操作", APP_UI_STATUS_WARNING);
        return;
    }
    state->confirmation_generation = snapshot.generation;
    state->confirming = true;
    state->pending_action = action;
    state->confirm_sheet = app_ui_sheet_open(
                               state->page.root,
                               recovery ? "格式化并重新挂载" : "格式化存储卡",
                               recovery ?
                               "将尝试重建文件系统并重新挂载。存储卡上的全部文件会被清除，且无法撤销。"
                               :
                               "存储卡上的全部用户内容将被清除，且无法撤销。此操作不影响本机设置与授权。");
    if (state->confirm_sheet == NULL)
    {
        state->confirming = false;
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "格式化", true,
                                  _storage_format_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _storage_format_cancel, state);
}

static void _storage_format_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _storage_open_confirm(state, FILE_SERVICE_STORAGE_FORMAT);
    }
}

static void _storage_recover_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_storage_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _storage_open_confirm(state, FILE_SERVICE_STORAGE_RECOVER_FORMAT);
    }
}

static void _storage_open_files_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_run(APP_MANAGER_ID_FILES);
    }
}

static void _storage_timer(lv_timer_t *timer)
{
    settings_storage_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL || state->page.root == NULL)
    {
        return;
    }
    // Re-enable local buttons once the submitted operation reached a final
    // state; the result itself stays readable from the snapshot.
    _storage_render(state);
}

static void _storage_mount(const app_manager_page_context_t *context)
{
    settings_storage_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "存储管理", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);
    lv_obj_set_scrollable(state->page.content, true);
    lv_obj_set_scrollbar_mode(state->page.content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_value(group, "挂载状态", "读取中",
                                 &state->state_value);
    (void)app_ui_group_add_value(group, "总容量", "读取中",
                                 &state->capacity_value);
    (void)app_ui_group_add_value(group, "剩余空间", "读取中",
                                 &state->free_value);
    (void)app_ui_group_add_value(group, "当前操作", "读取中",
                                 &state->operation_value);

    lv_obj_t *action_group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_nav_image(action_group, APP_IMAGE_UI_FOLDER,
                                     LV_SYMBOL_IMAGE, "文件管理",
                                     "浏览存储卡内容",
                                     _storage_open_files_event, NULL);
    state->mount_button = app_ui_group_add_command(action_group,
        LV_SYMBOL_REFRESH, "重新挂载",
        "不会格式化", _storage_mount_event,
        state);
    state->refresh_button = app_ui_group_add_command(action_group,
        LV_SYMBOL_REFRESH, "刷新容量", NULL,
        _storage_refresh_event, state);
    state->unmount_button = app_ui_group_add_command(action_group,
        LV_SYMBOL_CLOSE, "安全卸载",
        "拔出前先停止使用", _storage_unmount_event,
        state);

    lv_obj_t *format_group = app_ui_group_create(state->page.content);
    state->recover_button = app_ui_group_add_danger(format_group,
        LV_SYMBOL_WARNING, "格式化并恢复挂载",
        "会清除全部内容",
        _storage_recover_event, state);
    state->format_button = app_ui_group_add_danger(format_group,
        LV_SYMBOL_TRASH, "格式化存储卡",
        "清除全部用户内容",
        _storage_format_event, state);

    state->status_label = app_ui_add_body_label(state->page.content,
        "读取中");

    state->refresh_timer = lv_timer_create(_storage_timer, 500U, state);
    _storage_render(state);
}

static esp_err_t _storage_pause(const app_manager_page_context_t *context)
{
    settings_storage_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _storage_resume(const app_manager_page_context_t *context)
{
    settings_storage_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _storage_render(state);
}

static void _storage_unmount(const app_manager_page_context_t *context)
{
    settings_storage_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->state_value = NULL;
    state->capacity_value = NULL;
    state->free_value = NULL;
    state->operation_value = NULL;
    state->status_label = NULL;
    state->mount_button = NULL;
    state->refresh_button = NULL;
    state->unmount_button = NULL;
    state->format_button = NULL;
    state->recover_button = NULL;
}

static const app_manager_page_ops_t s_settings_storage_ops =
{
    .mount = _storage_mount,
    .resume = _storage_resume,
    .pause = _storage_pause,
    .unmount = _storage_unmount,
};

const app_manager_page_definition_t settings_storage_page_definition =
{
    .ops = &s_settings_storage_ops,
    .memory_size = sizeof(settings_storage_state_t),
};
