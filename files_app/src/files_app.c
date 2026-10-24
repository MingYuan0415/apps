#define DBG_TAG "files_app"
#include "mt_log.h"

#include "files_app_internal.h"

#ifdef ESP_PLATFORM
    #include "esp_heap_caps.h"
#endif

void *files_payload_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    void *memory = heap_caps_calloc(1U, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != NULL ? memory : calloc(1U, size);
#else
    return calloc(1U, size);
#endif
}

void files_path_parent(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash != NULL)
    {
        *slash = '\0';
    }
    else
    {
        path[0] = '\0';
    }
}

esp_err_t files_path_join(const char *directory, const char *name,
                          char *out, size_t out_size)
{
    const int length = directory[0] == '\0' ?
                       snprintf(out, out_size, "%s", name) :
                       snprintf(out, out_size, "%s/%s", directory, name);
    if (length < 0 || (size_t)length >= out_size)
    {
        if (out_size > 0U)
        {
            out[0] = '\0';
        }
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

void files_format_size(uint64_t bytes, char *out, size_t out_size)
{
    if (bytes < 1024U)
    {
        (void)snprintf(out, out_size, "%llu B",
                       (unsigned long long)bytes);
    }
    else if (bytes < 1024U * 1024U)
    {
        (void)snprintf(out, out_size, "%.1f KB",
                       (double)bytes / 1024.0);
    }
    else if (bytes < 1024ULL * 1024ULL * 1024ULL)
    {
        (void)snprintf(out, out_size, "%.1f MB",
                       (double)bytes / (1024.0 * 1024.0));
    }
    else
    {
        (void)snprintf(out, out_size, "%.2f GB",
                       (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

void files_format_time(bool has_time, uint32_t epoch_s, char *out,
                       size_t out_size)
{
    if (!has_time || epoch_s == 0U)
    {
        /* No invented timestamp: the volume simply did not report one. */
        (void)snprintf(out, out_size, "未知");
        return;
    }
    const time_t stamp = (time_t)epoch_s;
    struct tm local_time;
    if (localtime_r(&stamp, &local_time) == NULL)
    {
        (void)snprintf(out, out_size, "未知");
        return;
    }
    (void)strftime(out, out_size, "%Y-%m-%d %H:%M", &local_time);
}

static file_service_target_t s_selected_target;
static bool s_has_selection;
static bool s_selection_deleted;

esp_err_t files_app_select(const file_service_target_t *target)
{
    if (target == NULL || target->relative_path[0] == '\0' ||
            strnlen(target->relative_path, FILES_PATH_BYTES) >= FILES_PATH_BYTES)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_selected_target = *target;
    s_has_selection = true;
    s_selection_deleted = false;
    return ESP_OK;
}

esp_err_t files_app_copy_selection(file_service_target_t *target)
{
    if (target == NULL || !s_has_selection || s_selection_deleted)
    {
        return ESP_ERR_NOT_FOUND;
    }
    *target = s_selected_target;
    return ESP_OK;
}

void files_app_clear_selection(void)
{
    s_has_selection = false;
    s_selection_deleted = false;
}

void files_app_selection_deleted(void)
{
    s_selection_deleted = true;
}

bool files_app_restore_directory(char *directory)
{
    if (!s_selection_deleted)
    {
        return false;
    }
    if (strcmp(directory, s_selected_target.relative_path) == 0)
    {
        files_path_parent(directory);
    }
    s_selection_deleted = false;
    s_has_selection = false;
    return true;
}

const char *files_error_text(file_service_error_reason_t reason)
{
    switch (reason)
    {
    case FILE_SERVICE_ERROR_UNAVAILABLE:
        return "存储卡未挂载，请前往存储管理重新挂载";
    case FILE_SERVICE_ERROR_BUSY:
        return "正在使用，请稍后重试";
    case FILE_SERVICE_ERROR_STALE:
        return "内容已变化，请刷新后重新确认";
    case FILE_SERVICE_ERROR_NOT_FOUND:
        return "文件或目录不存在";
    case FILE_SERVICE_ERROR_NOT_EMPTY:
        return "目录非空，无法删除";
    case FILE_SERVICE_ERROR_PERMISSION:
        return "无权限执行此操作";
    case FILE_SERVICE_ERROR_PATH_TOO_LONG:
        return "名称或路径过长";
    case FILE_SERVICE_ERROR_NO_SPACE:
        return "存储空间不足";
    case FILE_SERVICE_ERROR_IO:
        return "存储卡读写故障，请重新挂载";
    case FILE_SERVICE_ERROR_CANCELLED:
        return "操作已取消";
    case FILE_SERVICE_ERROR_NO_MEMORY:
        return "内存不足";
    case FILE_SERVICE_ERROR_INVALID:
        return "当前状态无法执行该操作";
    default:
        return "操作失败，请重试";
    }
}

static void _files_saved_navigation(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        LOG_W("files navigation failed: %s", esp_err_to_name(result));
    }
}

void files_request_detail(const file_service_target_t *target)
{
    if (files_app_select(target) != ESP_OK)
    {
        LOG_W("detail selection rejected");
        return;
    }
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_FILES,
        .page_id = FILES_PAGE_DETAIL,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    const esp_err_t result = app_manager_navigate_async(&request,
        _files_saved_navigation, NULL);
    if (result != ESP_OK)
    {
        LOG_W("detail navigation failed: %s", esp_err_to_name(result));
    }
}

static const app_manager_page_route_t s_files_routes[] =
{
    {
        .page_id = FILES_PAGE_BROWSE,
        .definition = &files_browse_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = FILES_PAGE_DETAIL,
        .definition = &files_detail_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(files, APP_IMAGE_FILES_ICON, "文件管理",
                            APP_MANAGER_ID_FILES, FILES_PAGE_BROWSE,
                            APP_MANAGER_APP_FLAG_NONE, s_files_routes, 60U,
                            "浏览存储卡文件");
