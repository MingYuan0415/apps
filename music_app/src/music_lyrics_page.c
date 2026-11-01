#include "music_app_internal.h"

#include "event_bus.h"

#include <string.h>

#define MUSIC_LYRICS_TIMEOUT_TICKS 20U

typedef struct music_lyrics_state
{
    app_ui_page_t page;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *state_pill;
    lv_obj_t *lyric_panel;
    lv_obj_t *lines[MUSIC_UI_LYRIC_LINES];
    lv_obj_t *status_label;
    lv_obj_t *play_button;
    lv_obj_t *player_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_lyric_snapshot_t *lyric;
    char current_songmid[MUSIC_SERVICE_SONGMID_BYTES];
    uint32_t shown_position;
    size_t window_start;
    size_t current_line;
    uint16_t lyric_wait_ticks;
    bool lyric_loading;
    bool lyric_failed;
    bool has_window;
} music_lyrics_state_t;

_Static_assert(sizeof(music_lyrics_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music lyrics state exceeds the page arena");

static void _lyrics_label(lv_obj_t *label, const char *text, uint32_t color,
                          app_theme_font_id_t font)
{
    if (label == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(label, text != NULL ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
}

static void _lyrics_release_snapshot(music_lyrics_state_t *state)
{
    if (state->lyric != NULL)
    {
        music_service_lyric_release(state->lyric);
        state->lyric = NULL;
    }
    state->window_start = 0U;
    state->current_line = 0U;
    state->has_window = false;
}

static void _lyrics_show_window(music_lyrics_state_t *state, bool force)
{
    if (state->lyric == NULL || state->lyric->line_count == 0U)
    {
        lv_obj_set_hidden(state->lyric_panel, true);
        return;
    }
    const size_t current = music_ui_lyric_index_at(state->lyric,
        state->shown_position);
    const size_t start = music_ui_lyric_window_start(
                             state->lyric->line_count, current);
    const bool moved = start != state->window_start ||
                       current != state->current_line ||
                       !state->has_window || force;
    if (!moved)
    {
        return;
    }
    state->window_start = start;
    state->current_line = current;
    state->has_window = true;
    lv_obj_set_hidden(state->lyric_panel, false);
    for (size_t slot = 0U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
    {
        const size_t line = start + slot;
        lv_obj_t *label = state->lines[slot];
        if (label == NULL)
        {
            continue;
        }
        if (line >= state->lyric->line_count)
        {
            _lyrics_label(label, "", APP_UI_COLOR_MUTED,
                          APP_THEME_FONT_SMALL);
            continue;
        }
        const bool active = line == current;
        _lyrics_label(label,
                      state->lyric->lines[line].text[0] == '\0' ?
                      "·" : state->lyric->lines[line].text,
                      active ? APP_UI_COLOR_RAIN :
                      line < current ? APP_UI_COLOR_FAINT :
                      APP_UI_COLOR_MUTED,
                      active ? APP_THEME_FONT_BODY :
                      APP_THEME_FONT_SMALL);
    }
    app_ui_set_status_text(state->status_label,
                           current + 1U < state->lyric->line_count ?
                           "歌词" : "歌词 · 已到末尾",
                           APP_UI_STATUS_NEUTRAL);
}

static void _lyrics_acquire(music_lyrics_state_t *state)
{
    if (state == NULL || state->current_songmid[0] == '\0')
    {
        return;
    }
    const music_lyric_snapshot_t *lyric = NULL;
    const esp_err_t result = music_service_lyric_acquire(
                                 state->current_songmid, &lyric);
    if (result == ESP_OK && lyric != NULL)
    {
        _lyrics_release_snapshot(state);
        state->lyric = (music_lyric_snapshot_t *)lyric;
        state->lyric_loading = false;
        state->lyric_failed = false;
        state->lyric_wait_ticks = 0U;
        if (state->lyric->line_count == 0U)
        {
            app_ui_set_status_text(state->status_label, "暂无歌词",
                                   APP_UI_STATUS_NEUTRAL);
            return;
        }
        _lyrics_show_window(state, true);
        return;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        state->lyric_loading = true;
        state->lyric_wait_ticks = 0U;
        app_ui_set_status_text(state->status_label, "正在加载歌词…",
                               APP_UI_STATUS_NEUTRAL);
        return;
    }
    state->lyric_loading = false;
    state->lyric_failed = true;
    app_ui_set_status_text(state->status_label,
                           result == ESP_ERR_TIMEOUT ?
                           "歌词加载超时" : "歌词加载失败",
                           APP_UI_STATUS_WARNING);
}

static void _lyrics_poll(music_lyrics_state_t *state)
{
    music_player_status_t status;
    if (state == NULL ||
            music_service_get_player_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "播放服务不可用",
                               APP_UI_STATUS_ERROR);
        lv_obj_add_state(state->play_button, LV_STATE_DISABLED);
        return;
    }
    app_ui_label_set_text_if(state->title_label,
                             status.title[0] == '\0' ? "暂无播放" :
                             status.title);
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_DOT);
    app_ui_label_set_text_if(state->artist_label,
                             status.artist[0] == '\0' ?
                             "从歌单选择歌曲" : status.artist);
    app_ui_pill_set(state->state_pill,
                    music_ui_playback_text(status.state),
                    status.state == MUSIC_PLAYBACK_PLAYING ?
                    APP_UI_STATUS_SUCCESS :
                    status.state == MUSIC_PLAYBACK_ERROR ?
                    APP_UI_STATUS_ERROR :
                    status.state == MUSIC_PLAYBACK_BUFFERING ?
                    APP_UI_STATUS_ACCENT : APP_UI_STATUS_NEUTRAL);
    app_ui_button_set_text(state->play_button,
                           status.state == MUSIC_PLAYBACK_PLAYING ?
                           "暂停" : "播放");
    if (music_ui_can_toggle_playback(&status))
    {
        lv_obj_remove_state(state->play_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->play_button, LV_STATE_DISABLED);
    }
    if (status.songmid[0] == '\0')
    {
        _lyrics_release_snapshot(state);
        state->current_songmid[0] = '\0';
        state->shown_position = 0U;
        lv_obj_set_hidden(state->lyric_panel, true);
        _lyrics_label(state->lines[0], "暂无歌词", APP_UI_COLOR_MUTED,
                      APP_THEME_FONT_BODY);
        for (size_t slot = 1U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
        {
            _lyrics_label(state->lines[slot], "", APP_UI_COLOR_MUTED,
                          APP_THEME_FONT_SMALL);
        }
        app_ui_set_status_text(state->status_label,
                               "播放歌曲后显示歌词",
                               APP_UI_STATUS_NEUTRAL);
        return;
    }
    if (strcmp(state->current_songmid, status.songmid) != 0)
    {
        _lyrics_release_snapshot(state);
        (void)memcpy(state->current_songmid, status.songmid,
                     sizeof(state->current_songmid));
        state->current_songmid[sizeof(state->current_songmid) - 1U] = '\0';
        state->shown_position = 0U;
        state->lyric_failed = false;
        state->lyric_loading = false;
        state->lyric_wait_ticks = 0U;
        lv_obj_set_hidden(state->lyric_panel, true);
        for (size_t slot = 0U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
        {
            _lyrics_label(state->lines[slot], "", APP_UI_COLOR_MUTED,
                          APP_THEME_FONT_SMALL);
        }
    }
    if (state->lyric == NULL && !state->lyric_failed)
    {
        if (!state->lyric_loading)
        {
            _lyrics_acquire(state);
        }
        else if (state->lyric_wait_ticks < MUSIC_LYRICS_TIMEOUT_TICKS)
        {
            ++state->lyric_wait_ticks;
        }
        else
        {
            state->lyric_loading = false;
            state->lyric_wait_ticks = 0U;
        }
    }
    state->shown_position = status.position_ms;
    _lyrics_show_window(state, false);
}

static void _lyrics_timer_cb(lv_timer_t *timer)
{
    _lyrics_poll(lv_timer_get_user_data(timer));
}

static void _lyrics_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    (void)msg_id;
    music_lyrics_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_LYRIC &&
            payload != NULL && payload_size == sizeof(music_service_event_t))
    {
        const music_service_event_t *event = payload;
        if (event->failure == MUSIC_SERVICE_FAILURE_NONE)
        {
            state->lyric_loading = false;
        }
        else
        {
            state->lyric_failed = true;
            state->lyric_loading = false;
            app_ui_set_status_text(state->status_label, "歌词加载失败",
                                   APP_UI_STATUS_WARNING);
        }
    }
    _lyrics_poll(state);
}

static void _lyrics_play_clicked(lv_event_t *event)
{
    music_lyrics_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && music_service_play_pause() != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               "当前无法切换播放状态",
                               APP_UI_STATUS_WARNING);
    }
    if (state != NULL)
    {
        _lyrics_poll(state);
    }
}

static void _lyrics_player_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_player();
}

static void _lyrics_mount(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "歌词", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);

    lv_obj_t *heading = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(heading, 3, 0);
    app_ui_make_passive(heading, false);
    state->title_label = app_ui_add_section(heading, "暂无播放");
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_obj_set_style_pad_top(state->title_label, 0, 0);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->artist_label = app_ui_add_body_label(heading, "从歌单选择歌曲");
    lv_obj_set_style_text_font(state->artist_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->artist_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    state->state_pill = app_ui_add_pill(heading, "空闲",
                                        APP_UI_STATUS_NEUTRAL);

    state->lyric_panel = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->lyric_panel);
    lv_obj_set_width(state->lyric_panel, LV_PCT(100));
    lv_obj_set_height(state->lyric_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->lyric_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->lyric_panel, 10, 0);
    lv_obj_set_scroll_dir(state->lyric_panel, LV_DIR_NONE);
    lv_obj_set_hidden(state->lyric_panel, true);
    app_ui_make_passive(state->lyric_panel, false);
    for (size_t slot = 0U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
    {
        state->lines[slot] = app_ui_add_body_label(state->lyric_panel, "");
        lv_obj_set_width(state->lines[slot], LV_PCT(100));
        lv_label_set_long_mode(state->lines[slot], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(state->lines[slot],
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(state->lines[slot],
                                    lv_color_hex(APP_UI_COLOR_MUTED), 0);
    }
    state->status_label = app_ui_add_body_label(state->page.content,
        "正在加载歌词…");
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->play_button = app_ui_button_create(controls, "播放",
        _lyrics_play_clicked, state);
    app_ui_button_set_kind(state->play_button, APP_UI_BUTTON_PRIMARY);
    state->player_button = app_ui_button_create(controls, "播放器",
        _lyrics_player_clicked, state);
}

static void _lyrics_resume(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _lyrics_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->lyric_loading = false;
    state->lyric_wait_ticks = 0U;
    state->timer = lv_timer_create(_lyrics_timer_cb, 250U, state);
    _lyrics_poll(state);
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
    _lyrics_release_snapshot(state);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
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
