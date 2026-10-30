#include "music_app_internal.h"

#include <stdio.h>
#include <string.h>

void music_ui_show_image(lv_obj_t *object, const music_image_t *image,
                         lv_image_dsc_t *descriptor)
{
#ifdef HOST_TEST
    (void)object;
    (void)image;
    (void)descriptor;
#else
    if (object == NULL || image == NULL || descriptor == NULL)
    {
        return;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor->header.w = image->width;
    descriptor->header.h = image->height;
    descriptor->header.stride = (uint32_t)image->width * 2U;
    descriptor->data_size = image->data_size;
    descriptor->data = image->pixels;
    lv_image_set_src(object, descriptor);
#endif
}

void music_ui_format_time(uint32_t milliseconds, char *output,
                          size_t output_size)
{
    if (output == NULL || output_size == 0U)
    {
        return;
    }
    const uint32_t seconds = milliseconds / 1000U;
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
}

const char *music_ui_state_text(const music_service_status_t *status)
{
    if (status == NULL)
    {
        return "音乐服务不可用";
    }
    switch (status->state)
    {
    case MUSIC_SERVICE_STATE_WAITING_NETWORK:
        return "等待网络";
    case MUSIC_SERVICE_STATE_WAITING_TIME:
        return "正在同步时间";
    case MUSIC_SERVICE_STATE_LOGGED_OUT:
        return "未登录";
    case MUSIC_SERVICE_STATE_QR_READY:
        return "请扫码登录";
    case MUSIC_SERVICE_STATE_QR_SCANNED:
        return "已扫码，请确认";
    case MUSIC_SERVICE_STATE_AUTHENTICATING:
        return "正在登录";
    case MUSIC_SERVICE_STATE_READY:
        return "已登录";
    case MUSIC_SERVICE_STATE_AUTH_REQUIRED:
        return "登录已过期";
    case MUSIC_SERVICE_STATE_ERROR:
        return "服务异常";
    default:
        return "音乐服务不可用";
    }
}

const char *music_ui_failure_text(music_service_failure_t failure)
{
    switch (failure)
    {
    case MUSIC_SERVICE_FAILURE_NETWORK:
        return "网络不可用";
    case MUSIC_SERVICE_FAILURE_TIME:
        return "系统时间未同步";
    case MUSIC_SERVICE_FAILURE_AUTH:
        return "登录态已失效";
    case MUSIC_SERVICE_FAILURE_RATE_LIMIT:
        return "请求过于频繁";
    case MUSIC_SERVICE_FAILURE_API:
        return "音乐服务返回异常";
    case MUSIC_SERVICE_FAILURE_PARSE:
        return "音乐数据解析失败";
    case MUSIC_SERVICE_FAILURE_NO_PERMISSION:
        return "该歌曲暂无播放权限";
    case MUSIC_SERVICE_FAILURE_AUDIO_BUSY:
        return "音频被其他功能占用";
    case MUSIC_SERVICE_FAILURE_DECODE:
        return "音频解码失败";
    case MUSIC_SERVICE_FAILURE_STORAGE:
        return "登录态保存失败";
    case MUSIC_SERVICE_FAILURE_INTERNAL:
        return "内部错误";
    default:
        return "请稍后重试";
    }
}

lv_obj_t *music_ui_add_playlist_row(lv_obj_t *parent,
                                    const music_playlist_t *playlist,
                                    lv_event_cb_t callback,
                                    void *user_data)
{
    if (parent == NULL || playlist == NULL)
    {
        return NULL;
    }
    char subtitle[96];
    (void)snprintf(subtitle, sizeof(subtitle), "%s · %u 首",
                   playlist->creator[0] == '\0' ? "QQ 音乐" : playlist->creator,
                   (unsigned)playlist->track_count);
    return app_ui_add_action(parent, LV_SYMBOL_AUDIO, playlist->title,
                             subtitle, callback, user_data);
}

lv_obj_t *music_ui_add_song_row(lv_obj_t *parent, const music_song_t *song,
                                uint16_t index, lv_event_cb_t callback,
                                void *user_data)
{
    if (parent == NULL || song == NULL)
    {
        return NULL;
    }
    char title[160];
    (void)snprintf(title, sizeof(title), "%u. %s", (unsigned)(index + 1U),
                   song->title);
    return app_ui_add_action(parent, LV_SYMBOL_PLAY, title,
                             song->artist, callback, user_data);
}

static void _music_navigation_complete(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        (void)result;
    }
}

esp_err_t music_ui_open_playlist(const music_playlist_ref_t *ref)
{
    if (ref == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const music_playlist_arguments_t arguments =
    {
        .ref = *ref,
    };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYLIST,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = MUSIC_ARGUMENT_PLAYLIST,
            .size = sizeof(arguments),
        },
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    memcpy(request.arguments.payload, &arguments, sizeof(arguments));
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}

esp_err_t music_ui_open_player(void)
{
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_PLAYER,
        .has_arguments = false,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    return app_manager_navigate_async(&request, _music_navigation_complete,
                                      NULL);
}
