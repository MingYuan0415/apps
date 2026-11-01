#include "music_ui_state.h"

#include <stdio.h>

#define MUSIC_LYRIC_WINDOW_LINES 7U

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
        return status->logged_in ? "已登录" : "未登录";
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

const char *music_ui_playback_text(music_playback_state_t state)
{
    switch (state)
    {
    case MUSIC_PLAYBACK_BUFFERING:
        return "缓冲中";
    case MUSIC_PLAYBACK_PLAYING:
        return "正在播放";
    case MUSIC_PLAYBACK_PAUSED:
        return "已暂停";
    case MUSIC_PLAYBACK_ERROR:
        return "播放失败";
    default:
        return "未在播放";
    }
}

const char *music_ui_mode_text(music_play_mode_t mode)
{
    switch (mode)
    {
    case MUSIC_PLAY_MODE_REPEAT_ALL:
        return "列表循环";
    case MUSIC_PLAY_MODE_REPEAT_ONE:
        return "单曲循环";
    case MUSIC_PLAY_MODE_SHUFFLE:
        return "随机播放";
    default:
        return "顺序播放";
    }
}

music_play_mode_t music_ui_next_mode(music_play_mode_t mode)
{
    switch (mode)
    {
    case MUSIC_PLAY_MODE_SEQUENTIAL:
        return MUSIC_PLAY_MODE_REPEAT_ALL;
    case MUSIC_PLAY_MODE_REPEAT_ALL:
        return MUSIC_PLAY_MODE_REPEAT_ONE;
    case MUSIC_PLAY_MODE_REPEAT_ONE:
        return MUSIC_PLAY_MODE_SHUFFLE;
    default:
        return MUSIC_PLAY_MODE_SEQUENTIAL;
    }
}

const char *music_ui_quality_text(music_quality_t quality)
{
    switch (quality)
    {
    case MUSIC_QUALITY_LOSSLESS:
        return "无损";
    case MUSIC_QUALITY_HIGH:
        return "高品质";
    default:
        return "标准";
    }
}

bool music_ui_can_toggle_playback(const music_player_status_t *status)
{
    return status != NULL &&
           status->title[0] != '\0' &&
           (status->state == MUSIC_PLAYBACK_PLAYING ||
            status->state == MUSIC_PLAYBACK_PAUSED);
}

bool music_ui_can_step(const music_player_status_t *status, bool forward)
{
    if (status == NULL || status->title[0] == '\0' ||
            status->queue_count == 0U ||
            status->state == MUSIC_PLAYBACK_IDLE ||
            status->state == MUSIC_PLAYBACK_ERROR)
    {
        return false;
    }
    if (status->mode == MUSIC_PLAY_MODE_REPEAT_ONE)
    {
        return forward;
    }
    if (status->mode == MUSIC_PLAY_MODE_SHUFFLE)
    {
        return forward || status->queue_count > 1U;
    }
    if (status->mode == MUSIC_PLAY_MODE_REPEAT_ALL)
    {
        return true;
    }
    if (status->queue_index >= status->queue_count)
    {
        return false;
    }
    return forward ? status->queue_index + 1U < status->queue_count :
           status->queue_index > 0U;
}

void music_ui_queue_text(const music_player_status_t *status, char *output,
                         size_t output_size)
{
    if (output == NULL || output_size == 0U)
    {
        return;
    }
    if (status == NULL || status->queue_count == 0U ||
            status->queue_index >= status->queue_count)
    {
        output[0] = '\0';
        return;
    }
    (void)snprintf(output, output_size, "%u / %u",
                   (unsigned)(status->queue_index + 1U),
                   (unsigned)status->queue_count);
}

size_t music_ui_lyric_window_start(size_t line_count, size_t current_line)
{
    if (line_count == 0U)
    {
        return 0U;
    }
    if (current_line >= line_count)
    {
        current_line = line_count - 1U;
    }
    if (line_count <= MUSIC_LYRIC_WINDOW_LINES)
    {
        return 0U;
    }
    const size_t half = MUSIC_LYRIC_WINDOW_LINES / 2U;
    if (current_line <= half)
    {
        return 0U;
    }
    const size_t last_start = line_count - MUSIC_LYRIC_WINDOW_LINES;
    return current_line - half < last_start ? current_line - half :
           last_start;
}

size_t music_ui_lyric_index_at(const music_lyric_snapshot_t *lyric,
                               uint32_t position_ms)
{
    if (lyric == NULL || lyric->line_count == 0U)
    {
        return 0U;
    }
    size_t low = 0U;
    size_t high = lyric->line_count;
    while (low < high)
    {
        const size_t middle = low + (high - low) / 2U;
        if (lyric->lines[middle].offset_ms <= position_ms)
        {
            low = middle + 1U;
        }
        else
        {
            high = middle;
        }
    }
    return low == 0U ? 0U : low - 1U;
}
