#include "music_lyrics_view.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static _Alignas(max_align_t) uint8_t
s_lyric_storage[sizeof(music_lyric_snapshot_t) + sizeof(music_lyric_line_t)];
static music_lyric_snapshot_t *s_lyric;
static esp_err_t s_acquire_result;
static bool s_failed;
static unsigned s_acquire_calls;
static unsigned s_retry_calls;

static void _reset_fake(void)
{
    memset(s_lyric_storage, 0, sizeof(s_lyric_storage));
    s_lyric = (music_lyric_snapshot_t *)s_lyric_storage;
    s_lyric->line_count = 1U;
    (void)snprintf(s_lyric->lines[0].text,
                   sizeof(s_lyric->lines[0].text), "%s", "test lyric");
    s_acquire_result = ESP_ERR_NOT_FOUND;
    s_failed = false;
    s_acquire_calls = 0U;
    s_retry_calls = 0U;
}

esp_err_t music_service_lyric_acquire(const char *songmid,
                                      const music_lyric_snapshot_t **snapshot)
{
    ++s_acquire_calls;
    if (songmid == NULL || snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *snapshot = NULL;
    if (s_failed)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (s_acquire_result == ESP_OK)
    {
        *snapshot = s_lyric;
    }
    return s_acquire_result;
}

void music_service_lyric_release(const music_lyric_snapshot_t *snapshot)
{
    (void)snapshot;
}

bool music_service_lyric_failed(const char *songmid)
{
    (void)songmid;
    return s_failed;
}

void music_service_lyric_retry(const char *songmid)
{
    (void)songmid;
    ++s_retry_calls;
    s_failed = false;
}

static void _status(music_player_status_t *status, const char *songmid)
{
    memset(status, 0, sizeof(*status));
    (void)snprintf(status->songmid, sizeof(status->songmid), "%s", songmid);
    (void)snprintf(status->title, sizeof(status->title), "%s", "test song");
}

static void test_pending_then_ready(void)
{
    _reset_fake();
    music_lyrics_view_t view;
    memset(&view, 0, sizeof(view));
    music_player_status_t status;
    _status(&status, "song-1");

    assert(!music_lyrics_view_poll(&view, &status));
    assert(view.loading);
    assert(!view.failed);

    s_acquire_result = ESP_OK;
    assert(music_lyrics_view_poll(&view, &status));
    assert(view.snapshot == s_lyric);
    assert(!view.loading);
    assert(!view.failed);
    assert(s_acquire_calls == 2U);

    music_lyrics_view_reset(&view);
}

static void test_failure_and_retry(void)
{
    _reset_fake();
    music_lyrics_view_t view;
    memset(&view, 0, sizeof(view));
    music_player_status_t status;
    _status(&status, "song-1");

    s_failed = true;
    assert(!music_lyrics_view_poll(&view, &status));
    assert(view.failed);
    assert(!view.loading);

    music_lyrics_view_retry(&view);
    assert(s_retry_calls == 1U);
    assert(!view.failed);
    s_acquire_result = ESP_OK;
    assert(music_lyrics_view_poll(&view, &status));
    assert(view.snapshot == s_lyric);

    music_lyrics_view_reset(&view);
}

static void test_song_change_discards_result(void)
{
    _reset_fake();
    music_lyrics_view_t view;
    memset(&view, 0, sizeof(view));
    music_player_status_t status;
    _status(&status, "song-1");

    assert(!music_lyrics_view_poll(&view, &status));
    assert(view.loading);
    s_acquire_result = ESP_OK;
    assert(music_lyrics_view_poll(&view, &status));
    assert(view.snapshot == s_lyric);

    _status(&status, "song-2");
    s_acquire_result = ESP_ERR_NOT_FOUND;
    assert(music_lyrics_view_poll(&view, &status) == false);
    assert(view.snapshot == NULL);
    assert(strcmp(view.songmid, "song-2") == 0);

    music_lyrics_view_reset(&view);
}

int main(void)
{
    test_pending_then_ready();
    test_failure_and_retry();
    test_song_change_discards_result();
    (void)printf("music_lyrics_view_test: ok\n");
    return 0;
}
