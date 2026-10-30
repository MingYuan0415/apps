#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

typedef struct music_player_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *cover_object;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *progress_label;
    lv_obj_t *play_button;
    lv_obj_t *lyrics_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_image_t *cover_image;
    lv_image_dsc_t cover_descriptor;
    uint32_t cover_token;
    char cover_url[MUSIC_SERVICE_URL_BYTES];
    bool cover_requested;
    bool cover_failed;
} music_player_state_t;

_Static_assert(sizeof(music_player_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music player state exceeds the page arena");

static uint32_t _player_cover_token(const char *url)
{
    uint32_t hash = 2166136261U;
    if (url == NULL)
    {
        return hash;
    }
    for (const unsigned char *cursor = (const unsigned char *)url;
            *cursor != '\0'; ++cursor)
    {
        hash ^= *cursor;
        hash *= 16777619U;
    }
    return hash;
}

static void _player_update(music_player_state_t *state)
{
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "播放服务不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    app_ui_label_set_text_if(state->title_label,
                             status.title[0] == '\0' ? "暂无播放" :
                             status.title);
    app_ui_label_set_text_if(state->artist_label,
                             status.artist[0] == '\0' ? "—" : status.artist);
    if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        app_ui_set_status_text(state->status_label,
                               music_ui_failure_text(status.failure),
                               APP_UI_STATUS_WARNING);
    }
    else if (status.state == MUSIC_PLAYBACK_ERROR)
    {
        app_ui_set_status_text(state->status_label, "播放失败",
                               APP_UI_STATUS_WARNING);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "",
                               APP_UI_STATUS_NEUTRAL);
    }
    if (status.cover_url[0] == '\0' && state->cover_image != NULL)
    {
        if (state->cover_object != NULL)
        {
            lv_image_set_src(state->cover_object, NULL);
            lv_obj_set_hidden(state->cover_object, true);
        }
        music_service_image_release(state->cover_image);
        state->cover_image = NULL;
        state->cover_url[0] = '\0';
        state->cover_requested = false;
        state->cover_failed = false;
    }
    if (status.cover_url[0] != '\0' &&
            strcmp(state->cover_url, status.cover_url) != 0)
    {
        state->cover_requested = false;
        state->cover_failed = false;
    }
    if (status.cover_url[0] != '\0' &&
            strcmp(state->cover_url, status.cover_url) != 0 &&
            !state->cover_requested && !state->cover_failed)
    {
        const music_image_t *image = NULL;
        state->cover_token = _player_cover_token(status.cover_url);
        (void)snprintf(state->cover_url, sizeof(state->cover_url), "%s",
                       status.cover_url);
        state->cover_requested = true;
        const esp_err_t result = music_service_image_acquire(
                                     status.cover_url, 200U, 200U,
                                     state->cover_token, &image);
        if (result == ESP_OK && image != NULL)
        {
            if (state->cover_object != NULL)
            {
                lv_image_set_src(state->cover_object, NULL);
            }
            music_service_image_release(state->cover_image);
            state->cover_image = (music_image_t *)image;
            state->cover_requested = false;
            state->cover_failed = false;
            music_ui_show_image(state->cover_object, image,
                                &state->cover_descriptor);
            lv_obj_set_hidden(state->cover_object, false);
        }
        else if (result != ESP_ERR_NOT_FOUND)
        {
            state->cover_requested = false;
            state->cover_failed = true;
        }
    }
    char progress[64];
    char position[16];
    char duration[16];
    music_ui_format_time(status.position_ms, position, sizeof(position));
    music_ui_format_time(status.duration_ms, duration, sizeof(duration));
    (void)snprintf(progress, sizeof(progress), "%s / %s", position, duration);
    app_ui_label_set_text_if(state->progress_label, progress);
    app_ui_button_set_text(state->play_button,
                           status.state == MUSIC_PLAYBACK_PLAYING ?
                           "暂停" :
                           status.state == MUSIC_PLAYBACK_BUFFERING ?
                           "缓冲中" : "播放");
    if (status.state == MUSIC_PLAYBACK_BUFFERING)
    {
        lv_obj_add_state(state->play_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->play_button, LV_STATE_DISABLED);
    }
}

static void _player_timer_cb(lv_timer_t *timer)
{
    music_player_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL)
    {
        _player_update(state);
    }
}

static void _player_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_player_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_IMAGE &&
            payload != NULL && payload_size == sizeof(music_service_event_t))
    {
        const music_service_event_t *event = payload;
        if (event->token == state->cover_token)
        {
            state->cover_requested = false;
            state->cover_failed = false;
        }
    }
    _player_update(state);
}

static void _player_play_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_service_play_pause();
}

static void _player_prev_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_service_prev();
}

static void _player_next_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_service_next();
}

static void _player_lyrics_clicked(lv_event_t *event)
{
    (void)event;
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LYRICS,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    (void)app_manager_navigate_async(&request, NULL, NULL);
}

static void _player_mount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    state->cover_image = NULL;
    state->cover_object = NULL;
    state->cover_token = 0U;
    state->cover_url[0] = '\0';
    state->cover_requested = false;
    app_ui_page_create(&state->page, "正在播放", true);
    state->cover_object = lv_image_create(state->page.content);
    lv_obj_set_size(state->cover_object, 200, 200);
    lv_obj_set_hidden(state->cover_object, true);
    state->title_label = app_ui_add_body_label(state->page.content,
        "暂无播放");
    state->artist_label = app_ui_add_body_label(state->page.content, "—");
    state->progress_label = app_ui_add_body_label(state->page.content,
        "0:00 / 0:00");
    lv_obj_t *buttons = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    (void)app_ui_button_create(buttons, "上一首", _player_prev_clicked,
                               state);
    state->play_button = app_ui_button_create(buttons, "播放",
        _player_play_clicked, state);
    (void)app_ui_button_create(buttons, "下一首", _player_next_clicked,
                               state);
    lv_obj_t *lyrics_row = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->lyrics_button = app_ui_button_create(lyrics_row, "歌词",
        _player_lyrics_clicked, state);
    state->status_label = app_ui_add_body_label(state->page.content,
        "");
}

static void _player_resume(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _player_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_player_timer_cb, 250U, state);
    _player_update(state);
}

static esp_err_t _player_pause(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
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

static void _player_unmount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    if (state->cover_object != NULL)
    {
        lv_image_set_src(state->cover_object, NULL);
    }
    if (state->cover_image != NULL)
    {
        music_service_image_release(state->cover_image);
        state->cover_image = NULL;
    }
    memset(&state->cover_descriptor, 0, sizeof(state->cover_descriptor));
    app_ui_page_destroy(&state->page);
    state->cover_object = NULL;
    state->title_label = NULL;
    state->artist_label = NULL;
    state->progress_label = NULL;
    state->status_label = NULL;
    state->play_button = NULL;
    state->lyrics_button = NULL;
}

static const app_manager_page_ops_t s_player_ops =
{
    .mount = _player_mount,
    .resume = _player_resume,
    .pause = _player_pause,
    .unmount = _player_unmount,
};

const app_manager_page_definition_t music_player_page_definition =
{
    .ops = &s_player_ops,
    .memory_size = sizeof(music_player_state_t),
};
