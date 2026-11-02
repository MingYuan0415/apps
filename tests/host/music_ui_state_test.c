#include "music_ui_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_time_format(void)
{
    char text[16];
    music_ui_format_time(0U, text, sizeof(text));
    assert(strcmp(text, "0:00") == 0);
    music_ui_format_time(59U, text, sizeof(text));
    assert(strcmp(text, "0:00") == 0);
    music_ui_format_time(60U, text, sizeof(text));
    assert(strcmp(text, "0:00") == 0);
    music_ui_format_time(59999U, text, sizeof(text));
    assert(strcmp(text, "0:59") == 0);
    music_ui_format_time(60000U, text, sizeof(text));
    assert(strcmp(text, "1:00") == 0);
    music_ui_format_time(3599999U, text, sizeof(text));
    assert(strcmp(text, "59:59") == 0);
    music_ui_format_time(3600000U, text, sizeof(text));
    assert(strcmp(text, "60:00") == 0);
    music_ui_format_time(1U, text, 1U);
    assert(text[0] == '\0');
}

static void test_service_and_player_copy(void)
{
    music_service_status_t service = {0};
    assert(strcmp(music_ui_state_text(NULL), "音乐服务不可用") == 0);
    service.state = MUSIC_SERVICE_STATE_READY;
    service.logged_in = true;
    assert(strcmp(music_ui_state_text(&service), "已登录") == 0);
    service.logged_in = false;
    assert(strcmp(music_ui_state_text(&service), "未登录") == 0);
    service.state = MUSIC_SERVICE_STATE_QR_SCANNED;
    assert(strcmp(music_ui_state_text(&service), "已扫码，请确认") == 0);
    service.state = MUSIC_SERVICE_STATE_QR_EXPIRED;
    assert(strcmp(music_ui_state_text(&service), "二维码已过期") == 0);
    service.state = MUSIC_SERVICE_STATE_AUTH_REQUIRED;
    assert(strcmp(music_ui_state_text(&service), "登录已过期") == 0);
    assert(strcmp(music_ui_failure_text(MUSIC_SERVICE_FAILURE_AUDIO_BUSY),
                  "音频被其他功能占用") == 0);
    assert(strcmp(music_ui_failure_text(MUSIC_SERVICE_FAILURE_DECODE),
                  "音频解码失败") == 0);
    assert(strcmp(music_ui_failure_text(MUSIC_SERVICE_FAILURE_NONE),
                  "请稍后重试") == 0);

    assert(strcmp(music_ui_playback_text(MUSIC_PLAYBACK_IDLE),
                  "未在播放") == 0);
    assert(strcmp(music_ui_playback_text(MUSIC_PLAYBACK_BUFFERING),
                  "缓冲中") == 0);
    assert(strcmp(music_ui_playback_text(MUSIC_PLAYBACK_PLAYING),
                  "正在播放") == 0);
    assert(strcmp(music_ui_playback_text(MUSIC_PLAYBACK_PAUSED),
                  "已暂停") == 0);
    assert(strcmp(music_ui_playback_text(MUSIC_PLAYBACK_ERROR),
                  "播放失败") == 0);
}

static void test_mode_quality_copy(void)
{
    assert(strcmp(music_ui_mode_text(MUSIC_PLAY_MODE_SEQUENTIAL),
                  "顺序播放") == 0);
    assert(strcmp(music_ui_mode_text(MUSIC_PLAY_MODE_REPEAT_ALL),
                  "列表循环") == 0);
    assert(strcmp(music_ui_mode_text(MUSIC_PLAY_MODE_REPEAT_ONE),
                  "单曲循环") == 0);
    assert(strcmp(music_ui_mode_text(MUSIC_PLAY_MODE_SHUFFLE),
                  "随机播放") == 0);
    assert(strcmp(music_ui_mode_text((music_play_mode_t)99),
                  "顺序播放") == 0);
    assert(music_ui_next_mode(MUSIC_PLAY_MODE_SEQUENTIAL) ==
           MUSIC_PLAY_MODE_REPEAT_ALL);
    assert(music_ui_next_mode(MUSIC_PLAY_MODE_REPEAT_ALL) ==
           MUSIC_PLAY_MODE_REPEAT_ONE);
    assert(music_ui_next_mode(MUSIC_PLAY_MODE_REPEAT_ONE) ==
           MUSIC_PLAY_MODE_SHUFFLE);
    assert(music_ui_next_mode(MUSIC_PLAY_MODE_SHUFFLE) ==
           MUSIC_PLAY_MODE_SEQUENTIAL);
    assert(music_ui_next_mode((music_play_mode_t)99) ==
           MUSIC_PLAY_MODE_SEQUENTIAL);
    assert(strcmp(music_ui_quality_text(MUSIC_QUALITY_STANDARD),
                  "标准") == 0);
    assert(strcmp(music_ui_quality_text(MUSIC_QUALITY_HIGH),
                  "高品质") == 0);
    assert(strcmp(music_ui_quality_text(MUSIC_QUALITY_LOSSLESS),
                  "无损") == 0);
}

static void test_control_availability(void)
{
    music_player_status_t status = {0};
    assert(!music_ui_can_toggle_playback(NULL));
    assert(!music_ui_can_toggle_playback(&status));
    (void)strcpy(status.title, "track");
    status.state = MUSIC_PLAYBACK_BUFFERING;
    assert(!music_ui_can_toggle_playback(&status));
    status.state = MUSIC_PLAYBACK_PLAYING;
    assert(music_ui_can_toggle_playback(&status));
    status.state = MUSIC_PLAYBACK_PAUSED;
    assert(music_ui_can_toggle_playback(&status));
    status.state = MUSIC_PLAYBACK_ERROR;
    assert(!music_ui_can_toggle_playback(&status));

    status.queue_count = 3U;
    status.queue_index = 0U;
    status.mode = MUSIC_PLAY_MODE_SEQUENTIAL;
    status.state = MUSIC_PLAYBACK_PLAYING;
    assert(music_ui_can_step(&status, true));
    assert(!music_ui_can_step(&status, false));
    status.queue_index = 2U;
    assert(!music_ui_can_step(&status, true));
    assert(music_ui_can_step(&status, false));
    status.mode = MUSIC_PLAY_MODE_REPEAT_ALL;
    assert(music_ui_can_step(&status, true));
    status.mode = MUSIC_PLAY_MODE_REPEAT_ONE;
    status.queue_index = 1U;
    assert(music_ui_can_step(&status, true));
    assert(!music_ui_can_step(&status, false));
    status.mode = MUSIC_PLAY_MODE_SHUFFLE;
    status.queue_count = 1U;
    status.queue_index = 0U;
    assert(music_ui_can_step(&status, true));
    assert(!music_ui_can_step(&status, false));
    status.state = MUSIC_PLAYBACK_ERROR;
    assert(!music_ui_can_step(&status, true));
}

static void test_repeat_one_prev_semantics(void)
{
    music_player_status_t status = {0};
    (void)strcpy(status.title, "track");
    status.state = MUSIC_PLAYBACK_PLAYING;
    status.mode = MUSIC_PLAY_MODE_REPEAT_ONE;
    status.queue_count = 4U;
    status.queue_index = 2U;
    assert(music_ui_can_step(&status, true));
    assert(!music_ui_can_step(&status, false));
}

static void test_retry_policy(void)
{
    music_player_status_t status = {0};
    (void)strcpy(status.title, "track");
    status.queue_count = 3U;
    status.queue_index = 1U;
    status.state = MUSIC_PLAYBACK_ERROR;
    status.failure = MUSIC_SERVICE_FAILURE_DECODE;
    assert(music_ui_can_retry_playback(&status));
    status.failure = MUSIC_SERVICE_FAILURE_NO_PERMISSION;
    assert(!music_ui_can_retry_playback(&status));
    status.failure = MUSIC_SERVICE_FAILURE_AUTH;
    assert(!music_ui_can_retry_playback(&status));
    status.failure = MUSIC_SERVICE_FAILURE_STORAGE;
    assert(!music_ui_can_retry_playback(&status));
    status.failure = MUSIC_SERVICE_FAILURE_DECODE;
    status.state = MUSIC_PLAYBACK_PAUSED;
    assert(!music_ui_can_retry_playback(&status));
    status.state = MUSIC_PLAYBACK_ERROR;
    status.queue_count = 0U;
    assert(!music_ui_can_retry_playback(&status));
    assert(music_ui_failure_is_retryable(MUSIC_SERVICE_FAILURE_NETWORK));
    assert(music_ui_failure_is_retryable(MUSIC_SERVICE_FAILURE_AUDIO_BUSY));
    assert(!music_ui_failure_is_retryable(MUSIC_SERVICE_FAILURE_NO_PERMISSION));
    assert(!music_ui_failure_is_retryable(MUSIC_SERVICE_FAILURE_AUTH));
    assert(!music_ui_failure_is_retryable(MUSIC_SERVICE_FAILURE_NONE));
}

static void test_url_token(void)
{
    const uint32_t first = music_ui_url_token("https://example.test/a.jpg");
    assert(first == music_ui_url_token("https://example.test/a.jpg"));
    assert(first != music_ui_url_token("https://example.test/b.jpg"));
    assert(first != music_ui_url_token(NULL));
}

static void test_queue_text(void)
{
    char text[24];
    music_player_status_t status = {0};
    music_ui_queue_text(NULL, text, sizeof(text));
    assert(text[0] == '\0');
    music_ui_queue_text(&status, text, sizeof(text));
    assert(text[0] == '\0');
    status.queue_count = 12U;
    status.queue_index = 0U;
    music_ui_queue_text(&status, text, sizeof(text));
    assert(strcmp(text, "1 / 12") == 0);
    status.queue_index = 11U;
    music_ui_queue_text(&status, text, sizeof(text));
    assert(strcmp(text, "12 / 12") == 0);
    status.queue_index = 12U;
    music_ui_queue_text(&status, text, sizeof(text));
    assert(text[0] == '\0');
    music_ui_queue_text(&status, NULL, sizeof(text));
    music_ui_queue_text(&status, text, 0U);
}

static void test_lyric_window(void)
{
    assert(music_ui_lyric_window_start(0U, 0U) == 0U);
    assert(music_ui_lyric_window_start(4U, 2U) == 0U);
    assert(music_ui_lyric_window_start(7U, 6U) == 0U);
    assert(music_ui_lyric_window_start(10U, 0U) == 0U);
    assert(music_ui_lyric_window_start(10U, 2U) == 0U);
    assert(music_ui_lyric_window_start(10U, 3U) == 0U);
    assert(music_ui_lyric_window_start(10U, 4U) == 1U);
    assert(music_ui_lyric_window_start(10U, 8U) == 3U);
    assert(music_ui_lyric_window_start(10U, 9U) == 3U);
    assert(music_ui_lyric_window_start(10U, 99U) == 3U);
}

static void test_lyric_lookup(void)
{
    assert(music_ui_lyric_index_at(NULL, 100U) == 0U);
    uint8_t storage[sizeof(music_lyric_snapshot_t) +
                    (4U * sizeof(music_lyric_line_t))] = {0};
    music_lyric_snapshot_t *snapshot = (music_lyric_snapshot_t *)storage;
    snapshot->line_count = 4U;
    snapshot->lines[0].offset_ms = 0U;
    snapshot->lines[1].offset_ms = 1000U;
    snapshot->lines[2].offset_ms = 2000U;
    snapshot->lines[3].offset_ms = 2000U;
    assert(music_ui_lyric_index_at(snapshot, 0U) == 0U);
    assert(music_ui_lyric_index_at(snapshot, 999U) == 0U);
    assert(music_ui_lyric_index_at(snapshot, 1000U) == 1U);
    assert(music_ui_lyric_index_at(snapshot, 1999U) == 1U);
    assert(music_ui_lyric_index_at(snapshot, 2000U) == 3U);
    assert(music_ui_lyric_index_at(snapshot, UINT32_MAX) == 3U);
}


static void test_lyric_triplet(void)
{
    uint8_t storage[sizeof(music_lyric_snapshot_t) +
                    (3U * sizeof(music_lyric_line_t))] = {0};
    music_lyric_snapshot_t *snapshot = (music_lyric_snapshot_t *)storage;
    snapshot->line_count = 3U;
    snapshot->lines[0].offset_ms = 0U;
    (void)strcpy(snapshot->lines[0].text, "上一句");
    snapshot->lines[1].offset_ms = 1000U;
    (void)strcpy(snapshot->lines[1].text, "当前句");
    snapshot->lines[2].offset_ms = 2000U;
    (void)strcpy(snapshot->lines[2].text, "下一句");

    const char *previous = NULL;
    const char *current = NULL;
    const char *next = NULL;
    assert(music_ui_lyric_triplet(snapshot, 0U, &previous, &current, &next));
    assert(previous == NULL);
    assert(strcmp(current, "上一句") == 0);
    assert(strcmp(next, "当前句") == 0);

    assert(music_ui_lyric_triplet(snapshot, 1500U, &previous, &current,
                                  &next));
    assert(strcmp(previous, "上一句") == 0);
    assert(strcmp(current, "当前句") == 0);
    assert(strcmp(next, "下一句") == 0);

    assert(music_ui_lyric_triplet(snapshot, 9000U, &previous, &current,
                                  &next));
    assert(strcmp(previous, "当前句") == 0);
    assert(strcmp(current, "下一句") == 0);
    assert(next == NULL);

    assert(!music_ui_lyric_triplet(NULL, 0U, &previous, &current, &next));
    assert(previous == NULL && current == NULL && next == NULL);
}

static void test_playlist_identity_and_pages(void)
{
    music_playlist_ref_t ref = {0};
    assert(!music_ui_playlist_ref_valid(NULL));
    assert(!music_ui_playlist_ref_valid(&ref));
    ref.kind = MUSIC_PLAYLIST_KIND_MY_FAVORITE;
    ref.dirid = 201U;
    assert(music_ui_playlist_ref_valid(&ref));
    ref.kind = MUSIC_PLAYLIST_KIND_MY_CREATED;
    assert(!music_ui_playlist_ref_valid(&ref));
    ref.id = 42U;
    assert(music_ui_playlist_ref_valid(&ref));

    assert(music_ui_playlist_page_count(0U, 0U, 0U) == 0U);
    assert(music_ui_playlist_page_count(1U, 0U, 0U) == 1U);
    assert(music_ui_playlist_page_count(10U, 10U, 1U) == 1U);
    assert(music_ui_playlist_page_count(11U, 10U, 1U) == 2U);
    assert(music_ui_playlist_page_count(137U, 10U, 1U) == 14U);
    assert(music_ui_playlist_page_count(137U, 2U, 1U) == 15U);
    assert(music_ui_playlist_page_count(137U, 12U, 2U) == 15U);
    assert(music_ui_playlist_page_count(12U, 2U, 1U) == 2U);
    assert(music_ui_playlist_page_count(0U, 0U, 1U) == 1U);
    assert(music_ui_playlist_page_count(12U, 12U, 2U) == 2U);

}

static void test_home_shortcuts(void)
{
    music_home_snapshot_t home = {0};
    assert(music_ui_liked_playlist(NULL) == NULL);
    assert(music_ui_radio_playlist(NULL) == NULL);
    home.recommended_count = 2U;
    home.recommended[0].ref.id = 1U;
    home.recommended[1].ref.id = 2U;
    assert(music_ui_radio_playlist(&home) == &home.recommended[1]);
    home.my_favorite_count = 1U;
    home.my_favorite[0].ref.id = 3U;
    home.my_favorite[0].ref.dirid = 201U;
    assert(music_ui_liked_playlist(&home) == &home.my_favorite[0]);
    home.my_favorite_count = 0U;
    assert(music_ui_liked_playlist(&home) == NULL);
}

int main(void)
{
    test_time_format();
    test_service_and_player_copy();
    test_mode_quality_copy();
    test_control_availability();
    test_repeat_one_prev_semantics();
    test_retry_policy();
    test_url_token();
    test_queue_text();
    test_lyric_window();
    test_lyric_lookup();
    test_lyric_triplet();
    test_playlist_identity_and_pages();
    test_home_shortcuts();
    (void)printf("music_ui_state_test: ok\n");
    return 0;
}
