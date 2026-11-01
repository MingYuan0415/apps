#include "music_lyrics_view.h"

#include <string.h>

#define MUSIC_LYRIC_WAIT_TICKS 20U

void music_lyrics_view_reset(music_lyrics_view_t *view)
{
    if (view == NULL)
    {
        return;
    }
    music_lyrics_view_release(view);
    view->songmid[0] = '\0';
    view->wait_ticks = 0U;
    view->loading = false;
    view->failed = false;
}

void music_lyrics_view_release(music_lyrics_view_t *view)
{
    if (view == NULL)
    {
        return;
    }
    if (view->snapshot != NULL)
    {
        music_service_lyric_release(view->snapshot);
        view->snapshot = NULL;
    }
}

bool music_lyrics_view_poll(music_lyrics_view_t *view,
                            const music_player_status_t *status)
{
    if (view == NULL)
    {
        return false;
    }
    if (status == NULL || status->songmid[0] == '\0')
    {
        const bool changed = view->snapshot != NULL ||
                             view->songmid[0] != '\0';
        music_lyrics_view_reset(view);
        return changed;
    }
    if (strcmp(view->songmid, status->songmid) != 0)
    {
        music_lyrics_view_release(view);
        (void)memcpy(view->songmid, status->songmid,
                     sizeof(view->songmid));
        view->songmid[sizeof(view->songmid) - 1U] = '\0';
        view->wait_ticks = 0U;
        view->loading = false;
        view->failed = false;
    }
    if (view->snapshot != NULL)
    {
        return false;
    }
    if (view->failed)
    {
        return false;
    }
    if (view->loading)
    {
        if (view->wait_ticks < MUSIC_LYRIC_WAIT_TICKS)
        {
            ++view->wait_ticks;
        }
        else
        {
            view->loading = false;
            view->wait_ticks = 0U;
        }
        return false;
    }
    const music_lyric_snapshot_t *snapshot = NULL;
    const esp_err_t result = music_service_lyric_acquire(
                                 view->songmid, &snapshot);
    if (result == ESP_OK && snapshot != NULL)
    {
        view->snapshot = (music_lyric_snapshot_t *)snapshot;
        view->loading = false;
        view->failed = false;
        view->wait_ticks = 0U;
        return true;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        view->loading = true;
        view->wait_ticks = 0U;
    }
    else
    {
        view->failed = true;
    }
    return false;
}
