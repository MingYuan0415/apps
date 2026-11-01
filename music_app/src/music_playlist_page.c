#define DBG_TAG "music_playlist"
#include "mt_log.h"

#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_PLAYLIST_PAGE_STEP 30U
#define MUSIC_PLAYLIST_COVER_SIZE MUSIC_UI_HERO_COVER
#define MUSIC_PLAYLIST_COVER_TIMEOUT_TICKS 40U
#define MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS 2U

typedef struct music_playlist_state
{
    app_ui_page_t page;
    lv_obj_t *hero;
    lv_obj_t *details;
    lv_obj_t *title_label;
    lv_obj_t *creator_label;
    lv_obj_t *track_count_label;
    lv_obj_t *status_label;
    lv_obj_t *play_all_button;
    lv_obj_t *song_list;
    lv_obj_t *more_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_playlist_ref_t ref;
    music_playlist_snapshot_t *snapshot;
    music_ui_cover_t cover;
    uint16_t requested_offset;
    uint16_t visible_count;
    uint16_t cover_wait_ticks;
    uint32_t cover_token;
    uint32_t loading_since_ms;
    uint8_t cover_attempts;
    bool loading;
    bool failed;
    bool cover_requested;
    bool cover_failed;
} music_playlist_state_t;

_Static_assert(sizeof(music_playlist_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music playlist state exceeds the page arena");

static void _playlist_build_songs(music_playlist_state_t *state);
static void _playlist_request(music_playlist_state_t *state);

static uint32_t _playlist_cover_token(const char *url)
{
    uint32_t hash = 2166136261u;
    if (url != NULL)
    {
        for (const unsigned char *cursor = (const unsigned char *)url;
                *cursor != '\0'; ++cursor)
        {
            hash ^= *cursor;
            hash *= 16777619u;
        }
    }
    return hash;
}

static void _playlist_update_cover(music_playlist_state_t *state)
{
    if (state->cover.frame == NULL || state->snapshot == NULL)
    {
        return;
    }
    const char *url = state->snapshot->playlist.cover_url;
    if (url[0] == '\0')
    {
        return;
    }
    if (state->cover_requested)
    {
        if (state->cover_wait_ticks < UINT16_MAX)
        {
            ++state->cover_wait_ticks;
        }
        if (state->cover_wait_ticks <= MUSIC_PLAYLIST_COVER_TIMEOUT_TICKS)
        {
            return;
        }
        state->cover_requested = false;
    }
    if (state->cover.image != NULL || state->cover_failed ||
            state->cover_attempts >= MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS)
    {
        return;
    }
    const music_image_t *image = NULL;
    state->cover_token = _playlist_cover_token(url);
    state->cover_requested = true;
    state->cover_wait_ticks = 0U;
    ++state->cover_attempts;
    const esp_err_t result = music_service_image_acquire(
                                 url, MUSIC_PLAYLIST_COVER_SIZE,
                                 MUSIC_PLAYLIST_COVER_SIZE, state->cover_token,
                                 &image);
    if (result == ESP_OK && image != NULL)
    {
        state->cover_requested = false;
        state->cover_attempts = 0U;
        music_ui_cover_set_image(&state->cover, (music_image_t *)image);
    }
    else if (result != ESP_ERR_NOT_FOUND)
    {
        state->cover_requested = false;
        state->cover_failed =
            state->cover_attempts >= MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS;
    }
}

static void _playlist_song_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->snapshot == NULL)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || encoded > state->snapshot->count)
    {
        return;
    }
    const esp_err_t result = music_service_play_playlist(
                                 &state->ref, (uint16_t)(encoded - 1U));
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "播放请求超时，请重试" :
                               "无法开始播放，请稍后重试",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    app_ui_set_status_text(state->status_label, "正在准备播放…",
                           APP_UI_STATUS_ACCENT);
    lv_obj_set_hidden(state->status_label, false);
    (void)music_ui_back_to_player();
}

static void _playlist_play_all_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->snapshot == NULL ||
            state->snapshot->count == 0U)
    {
        return;
    }
    const esp_err_t result = music_service_play_playlist(&state->ref, 0U);
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "播放请求超时，请重试" :
                               "无法播放歌单，请稍后重试",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    app_ui_set_status_text(state->status_label, "正在准备播放…",
                           APP_UI_STATUS_ACCENT);
    lv_obj_set_hidden(state->status_label, false);
    (void)music_ui_back_to_player();
}

static void _playlist_more_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->snapshot == NULL ||
            lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    state->visible_count = (uint16_t)(state->visible_count +
                                      MUSIC_PLAYLIST_PAGE_STEP);
    _playlist_build_songs(state);
}

static void _playlist_build_hero(music_playlist_state_t *state)
{
    if (state->hero == NULL || state->snapshot == NULL)
    {
        return;
    }
    const music_playlist_t *playlist = &state->snapshot->playlist;
    app_ui_label_set_text_if(state->title_label, playlist->title);
    app_ui_label_set_text_if(state->creator_label,
                             playlist->creator[0] == '\0' ?
                             "QQ 音乐" : playlist->creator);
    char count[48];
    (void)snprintf(count, sizeof(count), "共 %u 首",
                   (unsigned)state->snapshot->total);
    app_ui_label_set_text_if(state->track_count_label, count);
    state->cover_token = _playlist_cover_token(playlist->cover_url);
    lv_obj_set_hidden(state->details, false);
    lv_obj_set_hidden(state->status_label, true);
    if (state->snapshot->count == 0U)
    {
        lv_obj_add_state(state->play_all_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->play_all_button, LV_STATE_DISABLED);
    }
}

static lv_obj_t *_playlist_add_notice(lv_obj_t *parent, const char *text,
                                      app_ui_status_t status)
{
    lv_obj_t *label = app_ui_add_body_label(parent, text);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(label, text, status);
    return label;
}

static void _playlist_build_songs(music_playlist_state_t *state)
{
    if (state->song_list == NULL)
    {
        return;
    }
    lv_obj_clean(state->song_list);
    state->more_button = NULL;
    if (state->snapshot == NULL)
    {
        (void)_playlist_add_notice(state->song_list,
                                   state->failed ? "歌单加载失败" :
                                   "正在加载歌单…",
                                   state->failed ? APP_UI_STATUS_WARNING :
                                   APP_UI_STATUS_NEUTRAL);
        return;
    }
    const uint16_t count = state->snapshot->count;
    const uint16_t visible = state->visible_count < count ?
                             state->visible_count : count;
    for (uint16_t index = 0U; index < visible; ++index)
    {
        lv_obj_t *row = music_ui_add_song_row(
                            state->song_list, &state->snapshot->songs[index],
                            index, _playlist_song_clicked, state);
        if (row != NULL)
        {
            lv_obj_set_user_data(row, (void *)(uintptr_t)(index + 1U));
        }
    }
    if (count == 0U)
    {
        (void)_playlist_add_notice(state->song_list,
                                   "这个歌单还没有歌曲",
                                   APP_UI_STATUS_NEUTRAL);
        return;
    }
    if (state->snapshot->total > MUSIC_SERVICE_MAX_SONGS)
    {
        char note[96];
        (void)snprintf(note, sizeof(note),
                       "当前版本最多显示前 %u 首",
                       (unsigned)MUSIC_SERVICE_MAX_SONGS);
        (void)_playlist_add_notice(state->song_list, note,
                                   APP_UI_STATUS_WARNING);
    }
    if (visible < count)
    {
        state->more_button = app_ui_add_action(state->song_list,
                                               LV_SYMBOL_DOWN,
                                               "加载更多",
                                               "每次增加 30 首",
                                               _playlist_more_clicked, state);
    }
    else if (count < state->snapshot->total)
    {
        (void)_playlist_add_notice(state->song_list,
                                   "其余歌曲暂未由服务提供",
                                   APP_UI_STATUS_NEUTRAL);
    }
}

static void _playlist_request(music_playlist_state_t *state)
{
    if (state == NULL || state->snapshot != NULL || state->failed)
    {
        return;
    }
    if (state->loading &&
            lv_tick_get() - state->loading_since_ms < 10000U)
    {
        return;
    }
    state->loading = true;
    state->loading_since_ms = lv_tick_get();
    const music_playlist_snapshot_t *snapshot = NULL;
    const esp_err_t result = music_service_playlist_acquire(
                                 &state->ref, state->requested_offset,
                                 MUSIC_SERVICE_MAX_SONGS, &snapshot);
    if (result == ESP_OK && snapshot != NULL)
    {
        state->snapshot = (music_playlist_snapshot_t *)snapshot;
        state->loading = false;
        const uint16_t count = state->snapshot->count;
        state->visible_count = count < MUSIC_PLAYLIST_PAGE_STEP ?
                               count : MUSIC_PLAYLIST_PAGE_STEP;
        _playlist_build_hero(state);
        _playlist_build_songs(state);
        return;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        return;
    }
    state->loading = false;
    state->failed = true;
    app_ui_set_status_text(state->status_label,
                           result == ESP_ERR_TIMEOUT ?
                           "加载超时，返回后重试" :
                           "歌单加载失败", APP_UI_STATUS_WARNING);
    lv_obj_set_hidden(state->status_label, false);
    _playlist_build_songs(state);
}

static void _playlist_timer_cb(lv_timer_t *timer)
{
    music_playlist_state_t *state = lv_timer_get_user_data(timer);
    _playlist_update_cover(state);
    _playlist_request(state);
}

static void _playlist_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                               const void *payload, size_t payload_size,
                               void *user_data)
{
    (void)msg_id;
    music_playlist_state_t *state = user_data;
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
            state->cover_wait_ticks = 0U;
            if (event->failure == MUSIC_SERVICE_FAILURE_NONE)
            {
                state->cover_failed = false;
                state->cover_attempts = 0U;
                (void)music_ui_cover_accept_event(
                    &state->cover, state->cover_token,
                    state->snapshot != NULL ?
                    state->snapshot->playlist.cover_url : "", event);
            }
            else
            {
                state->cover_failed = state->cover_attempts >=
                                      MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS;
            }
        }
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_PLAYLIST)
    {
        if (state->snapshot != NULL)
        {
            music_service_playlist_release(state->snapshot);
            state->snapshot = NULL;
            state->failed = false;
            state->loading = false;
            state->loading_since_ms = 0U;
        }
        _playlist_request(state);
    }
    else if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS &&
             state->snapshot == NULL && !state->failed)
    {
        state->loading = false;
        _playlist_request(state);
    }
    _playlist_update_cover(state);
}

static void _playlist_copy_arguments(music_playlist_state_t *state,
                                     const app_manager_typed_blob_t *arguments)
{
    if (arguments == NULL ||
            arguments->version != APP_MANAGER_TYPED_BLOB_VERSION ||
            arguments->type != MUSIC_ARGUMENT_PLAYLIST ||
            arguments->size != sizeof(music_playlist_arguments_t))
    {
        return;
    }
    music_playlist_arguments_t payload;
    memcpy(&payload, arguments->payload, sizeof(payload));
    state->ref = payload.ref;
}

static lv_obj_t *_playlist_build_hero_area(music_playlist_state_t *state)
{
    lv_obj_t *hero = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, APP_UI_GAP, 0);

    music_ui_cover_create(&state->cover, hero, LV_SYMBOL_AUDIO,
                          MUSIC_PLAYLIST_COVER_SIZE);
    lv_obj_t *details = lv_obj_create(hero);
    lv_obj_remove_style_all(details);
    lv_obj_set_width(details, 0);
    lv_obj_set_flex_grow(details, 1);
    lv_obj_set_height(details, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details, 4, 0);
    app_ui_make_passive(details, false);
    state->details = details;
    state->title_label = app_ui_add_section(details, "歌单");
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_obj_set_height(state->title_label,
                      lv_font_get_line_height(app_ui_font(
                                  APP_THEME_FONT_BIGL)));
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_top(state->title_label, 0, 0);
    lv_obj_set_style_text_font(state->title_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->creator_label = app_ui_add_body_label(details, "QQ 音乐");
    lv_obj_set_style_text_font(state->creator_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    state->track_count_label = app_ui_add_body_label(details, "共 0 首");
    lv_obj_set_style_text_font(state->track_count_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(state->track_count_label, "共 0 首",
                           APP_UI_STATUS_ACCENT);
    state->status_label = app_ui_add_body_label(details, "");
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_hidden(state->status_label, true);
    lv_obj_set_hidden(details, true);
    return hero;
}

static void _playlist_mount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    _playlist_copy_arguments(state, context->arguments);
    app_ui_page_create(&state->page, "歌单", true);
    music_ui_add_header_back(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_row(state->page.content, 12, 0);
    state->play_all_button = music_ui_icon_button(
                                 state->page.header, APP_IMAGE_MUSIC_PLAY,
                                 LV_SYMBOL_PLAY, MUSIC_UI_CONTROL_SIZE,
                                 _playlist_play_all_clicked, state);
    if (state->play_all_button != NULL)
    {
        lv_obj_set_ignore_layout(state->play_all_button, true);
        lv_obj_align(state->play_all_button, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
        lv_obj_set_style_bg_color(state->play_all_button,
                                  lv_color_hex(APP_UI_COLOR_RAIN), 0);
        music_ui_set_icon_button(state->play_all_button,
                                 APP_IMAGE_MUSIC_PLAY, LV_SYMBOL_PLAY,
                                 APP_UI_COLOR_BACKGROUND);
        lv_obj_add_state(state->play_all_button, LV_STATE_DISABLED);
    }
    state->hero = _playlist_build_hero_area(state);
    state->song_list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->song_list);
    lv_obj_set_width(state->song_list, LV_PCT(100));
    lv_obj_set_height(state->song_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->song_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->song_list, 4, 0);
    lv_obj_set_scroll_dir(state->song_list, LV_DIR_NONE);
    app_ui_make_passive(state->song_list, false);
    _playlist_build_songs(state);
}

static void _playlist_resume(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _playlist_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_playlist_timer_cb, 500U, state);
    _playlist_update_cover(state);
    _playlist_request(state);
}

static esp_err_t _playlist_pause(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
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
    music_ui_cover_release(&state->cover);
    if (state->snapshot != NULL)
    {
        music_service_playlist_release(state->snapshot);
        state->snapshot = NULL;
    }
    state->failed = false;
    state->loading = false;
    state->loading_since_ms = 0U;
    state->cover_requested = false;
    state->cover_wait_ticks = 0U;
    state->cover_attempts = 0U;
    return ESP_OK;
}

static void _playlist_unmount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    music_ui_cover_release(&state->cover);
    if (state->snapshot != NULL)
    {
        music_service_playlist_release(state->snapshot);
        state->snapshot = NULL;
    }
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_playlist_ops =
{
    .mount = _playlist_mount,
    .resume = _playlist_resume,
    .pause = _playlist_pause,
    .unmount = _playlist_unmount,
};

const app_manager_page_definition_t music_playlist_page_definition =
{
    .ops = &s_playlist_ops,
    .memory_size = sizeof(music_playlist_state_t),
};
