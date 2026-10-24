#define DBG_TAG "recorder_app"
#include "mt_log.h"

#include "recorder_app_internal.h"

#include <stdlib.h>
#include <time.h>

#ifdef ESP_PLATFORM
    #include "esp_heap_caps.h"
#endif

void *recorder_payload_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    void *memory = heap_caps_calloc(1U, size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != NULL ? memory : calloc(1U, size);
#else
    return calloc(1U, size);
#endif
}

void recorder_ui_set_visible(lv_obj_t *control, bool visible)
{
    if (visible)
    {
        lv_obj_set_hidden(control, false);
    }
    else
    {
        lv_obj_set_hidden(control, true);
    }
}

void recorder_ui_format_mmss(uint32_t milliseconds, char *output,
                             size_t output_size)
{
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(milliseconds / 60000U),
                   (unsigned)((milliseconds / 1000U) % 60U));
}

void recorder_ui_format_size(uint64_t bytes, char *output, size_t output_size)
{
    if (bytes < 1024U)
    {
        (void)snprintf(output, output_size, "%llu B",
                       (unsigned long long)bytes);
    }
    else if (bytes < 1024U * 1024U)
    {
        (void)snprintf(output, output_size, "%.1f KB", (double)bytes / 1024.0);
    }
    else
    {
        (void)snprintf(output, output_size, "%.1f MB",
                       (double)bytes / (1024.0 * 1024.0));
    }
}

void recorder_ui_format_time(const recorder_service_file_t *file, char *output,
                             size_t output_size)
{
    if (file->has_mtime && file->modified_epoch_s >= RECORDER_UI_MIN_EPOCH_S)
    {
        const time_t stamp = (time_t)file->modified_epoch_s;
        struct tm local_time;
        if (localtime_r(&stamp, &local_time) != NULL &&
                strftime(output, output_size, "%m-%d %H:%M", &local_time) > 0U)
        {
            return;
        }
    }
    /* No usable clock: the boot-counter stem is still an identity, and the
     * detail Page shows the same text so both views agree. */
    const char *base = strrchr(file->target.name, '/');
    base = base != NULL ? base + 1 : file->target.name;
    (void)snprintf(output, output_size, "%s", base);
}

const char *recorder_ui_error_text(esp_err_t error)
{
    switch (error)
    {
    case ESP_OK:
        return "就绪";
    case ESP_ERR_NO_MEM:
        return "剩余空间不足";
    case ESP_ERR_INVALID_STATE:
        return "存储卡或音频不可用";
    case ESP_ERR_NOT_FOUND:
        return "录音已被删除";
    case ESP_ERR_INVALID_SIZE:
        return "剩余空间不足";
    case ESP_FAIL:
        return "存储卡读写失败";
    default:
        return "录音失败";
    }
}

static recorder_service_file_t s_selected;
static bool s_has_selection;

void recorder_app_select(const recorder_service_file_t *file)
{
    if (file == NULL)
    {
        s_has_selection = false;
        return;
    }
    s_selected = *file;
    s_has_selection = true;
}

esp_err_t recorder_app_copy_selection(recorder_service_file_t *file)
{
    if (file == NULL || !s_has_selection)
    {
        return ESP_ERR_NOT_FOUND;
    }
    *file = s_selected;
    return ESP_OK;
}

void recorder_app_clear_selection(void)
{
    s_has_selection = false;
    memset(&s_selected, 0, sizeof(s_selected));
}
