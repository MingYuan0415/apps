#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_LYRIC_VISIBLE_LINES MUSIC_SERVICE_MAX_LYRIC_LINES

typedef struct music_lyrics_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *lines[MUSIC_LYRIC_VISIBLE_LINES];
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_lyric_snapshot_t *lyric;
    char current_songmid[MUSIC_SERVICE_SONGMID_BYTES];
    uint32_t shown_position;
    bool lyric_failed;
} music_lyrics_state_t;

_Static_assert(sizeof(music_lyrics_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music lyrics state exceeds the page arena");

static void _lyrics_clear(music_lyrics_state_t *state)
{
    for (size_t index = 0U; index < MUSIC_LYRIC_VISIBLE_LINES; ++index)
    {
        if (state->lines[index] != NULL)
        {
            lv_obj_delete(state->lines[index]);
            state->lines[index] = NULL;
        }
    }
}

static void _lyrics_build(music_lyrics_state_t *state)
{
    _lyrics_clear(state);
    if (state->lyric == NULL || state->lyric->line_count == 0U)
    {
        app_ui_set_status_text(state->status_label, "暂无歌词",
                               APP_UI_STATUS_NEUTRAL);
        return;
    }
    app_ui_set_status_text(state->status_label, "歌词", APP_UI_STATUS_NEUTRAL);
    const size_t count = state->lyric->line_count < MUSIC_LYRIC_VISIBLE_LINES ?
                         state->lyric->line_count : MUSIC_LYRIC_VISIBLE_LINES;
    for (size_t index = 0U; index < count; ++index)
    {
        lv_obj_t *label = app_ui_add_body_label(state->page.content,
                                                state->lyric->lines[index].text);
        state->lines[index] = label;
    }
}

static void _lyrics_timer_cb(lv_timer_t *timer)
{
    music_lyrics_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        return;
    }
    if (status.songmid[0] == '\0')
    {
        return;
    }
    if (strcmp(state->current_songmid, status.songmid) != 0)
    {
        if (state->lyric != NULL)
        {
            music_service_lyric_release(state->lyric);
            state->lyric = NULL;
        }
        (void)snprintf(state->current_songmid,
                       sizeof(state->current_songmid), "%s", status.songmid);
        state->lyric_failed = false;
        const music_lyric_snapshot_t *lyric = NULL;
        const esp_err_t result = music_service_lyric_acquire(
                                     status.songmid, &lyric);
        if (result == ESP_OK && lyric != NULL)
        {
            state->lyric = (music_lyric_snapshot_t *)lyric;
            _lyrics_build(state);
        }
        else if (result != ESP_ERR_NOT_FOUND)
        {
            state->lyric_failed = true;
            app_ui_set_status_text(state->status_label, "歌词加载失败",
                                   APP_UI_STATUS_WARNING);
        }
    }
    state->shown_position = status.position_ms;
}

static void _lyrics_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    (void)msg_id;
    music_lyrics_state_t *state = user_data;
    if (state == NULL || state->timer == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS &&
            payload != NULL && payload_size == sizeof(music_service_event_t))
    {
        const music_service_event_t *event = payload;
        if (event->failure != MUSIC_SERVICE_FAILURE_NONE &&
                state->lyric == NULL && state->current_songmid[0] != '\0')
        {
            state->lyric_failed = true;
            app_ui_set_status_text(state->status_label, "歌词加载失败",
                                   APP_UI_STATUS_WARNING);
        }
    }
    _lyrics_timer_cb(state->timer);
}

static void _lyrics_mount(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    state->lyric = NULL;
    state->current_songmid[0] = '\0';
    state->shown_position = 0U;
    state->lyric_failed = false;
    memset(state->lines, 0, sizeof(state->lines));
    app_ui_page_create(&state->page, "歌词", true);
    state->status_label = app_ui_add_body_label(state->page.content,
        "正在加载歌词…");
}

static void _lyrics_resume(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _lyrics_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_lyrics_timer_cb, 500U, state);
    _lyrics_timer_cb(state->timer);
}

static esp_err_t _lyrics_pause(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    if (state->timer != NULL)
    {
        lv_timer_delete(state->timer);
        state->timer = NULL;
    }
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_unsubscribe(state->subscription);
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
    return ESP_OK;
}

static void _lyrics_unmount(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    _lyrics_clear(state);
    if (state->lyric != NULL)
    {
        music_service_lyric_release(state->lyric);
        state->lyric = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
}

static const app_manager_page_ops_t s_lyrics_ops =
{
    .mount = _lyrics_mount,
    .resume = _lyrics_resume,
    .pause = _lyrics_pause,
    .unmount = _lyrics_unmount,
};

const app_manager_page_definition_t music_lyrics_page_definition =
{
    .ops = &s_lyrics_ops,
    .memory_size = sizeof(music_lyrics_state_t),
};
