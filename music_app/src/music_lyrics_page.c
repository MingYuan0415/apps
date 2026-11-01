#include "music_app_internal.h"

#include "event_bus.h"
#include "music_lyrics_view.h"

#include <string.h>

typedef struct music_lyrics_state
{
    app_ui_page_t page;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *lyric_panel;
    lv_obj_t *lines[MUSIC_UI_LYRIC_LINES];
    lv_obj_t *status_label;
    lv_obj_t *play_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_lyrics_view_t view;
    size_t window_start;
    size_t current_line;
    uint8_t shown_play_state;
    bool has_window;
    bool play_state_valid;
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
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
}

static void _lyrics_clear_window(music_lyrics_state_t *state)
{
    state->window_start = 0U;
    state->current_line = 0U;
    state->has_window = false;
    lv_obj_set_hidden(state->lyric_panel, true);
    for (size_t slot = 0U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
    {
        _lyrics_label(state->lines[slot], "", APP_UI_COLOR_MUTED,
                      APP_THEME_FONT_SMALL);
    }
}

static void _lyrics_show_window(music_lyrics_state_t *state, bool force)
{
    if (state->view.snapshot == NULL ||
            state->view.snapshot->line_count == 0U)
    {
        lv_obj_set_hidden(state->lyric_panel, true);
        return;
    }
    music_player_status_t status;
    const uint32_t position =
        music_service_get_player_status(&status) == ESP_OK ?
        status.position_ms : 0U;
    const size_t current = music_ui_lyric_index_at(state->view.snapshot,
        position);
    const size_t start = music_ui_lyric_window_start(
                             state->view.snapshot->line_count, current);
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
        if (line >= state->view.snapshot->line_count)
        {
            _lyrics_label(label, "", APP_UI_COLOR_MUTED,
                          APP_THEME_FONT_SMALL);
            continue;
        }
        const bool active = line == current;
        _lyrics_label(label,
                      state->view.snapshot->lines[line].text[0] == '\0' ?
                      "·" : state->view.snapshot->lines[line].text,
                      active ? APP_UI_COLOR_RAIN :
                      line < current ? APP_UI_COLOR_FAINT :
                      APP_UI_COLOR_MUTED,
                      active ? APP_THEME_FONT_HEAD :
                      APP_THEME_FONT_SMALL);
    }
}

static void _lyrics_set_status(music_lyrics_state_t *state, const char *text,
                               app_ui_status_t status)
{
    app_ui_set_status_text(state->status_label, text, status);
    lv_obj_set_hidden(state->status_label, text == NULL || text[0] == '\0');
}

static void _lyrics_poll(music_lyrics_state_t *state)
{
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        _lyrics_set_status(state, "播放服务不可用", APP_UI_STATUS_ERROR);
        music_ui_set_button_enabled(state->play_button, false);
        _lyrics_clear_window(state);
        return;
    }
    app_ui_label_set_text_if(state->title_label,
                             status.title[0] == '\0' ? "暂无播放" :
                             status.title);
    app_ui_label_set_text_if(state->artist_label,
                             status.artist[0] == '\0' ?
                             "从侧边栏选择歌单开始播放" : status.artist);
    const bool playing = status.state == MUSIC_PLAYBACK_PLAYING;
    if (!state->play_state_valid || state->shown_play_state != status.state)
    {
        music_ui_set_plain_icon_button(
            state->play_button,
            playing ? APP_IMAGE_MUSIC_PAUSE : APP_IMAGE_MUSIC_PLAY,
            playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, 30,
            music_ui_can_toggle_playback(&status) ?
            APP_UI_COLOR_TEXT : APP_UI_COLOR_FAINT);
        state->shown_play_state = status.state;
        state->play_state_valid = true;
    }
    music_ui_set_button_enabled(state->play_button,
                                music_ui_can_toggle_playback(&status));
    lv_obj_set_style_border_color(
        state->play_button,
        lv_color_hex(music_ui_can_toggle_playback(&status) ?
                     APP_UI_COLOR_RAIN : APP_UI_COLOR_FAINT), 0);
    if (status.title[0] == '\0' || status.songmid[0] == '\0')
    {
        music_lyrics_view_reset(&state->view);
        _lyrics_clear_window(state);
        _lyrics_set_status(state, "播放歌曲后显示歌词", APP_UI_STATUS_NEUTRAL);
        return;
    }
    const bool changed = music_lyrics_view_poll(&state->view, &status);
    if (state->view.failed)
    {
        _lyrics_clear_window(state);
        _lyrics_set_status(state, "歌词加载失败", APP_UI_STATUS_WARNING);
        return;
    }
    if (state->view.snapshot == NULL)
    {
        _lyrics_clear_window(state);
        _lyrics_set_status(state, "正在加载歌词…", APP_UI_STATUS_NEUTRAL);
        return;
    }
    _lyrics_show_window(state, changed);
    if (state->view.snapshot->line_count == 0U)
    {
        _lyrics_clear_window(state);
        _lyrics_set_status(state, "暂无歌词", APP_UI_STATUS_NEUTRAL);
    }
    else
    {
        _lyrics_set_status(state, "", APP_UI_STATUS_NEUTRAL);
    }
}

static void _lyrics_timer_cb(lv_timer_t *timer)
{
    music_lyrics_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL)
    {
        _lyrics_poll(state);
    }
}

static void _lyrics_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_lyrics_state_t *state = user_data;
    if (state != NULL)
    {
        _lyrics_poll(state);
    }
}

static void _lyrics_play_clicked(lv_event_t *event)
{
    music_lyrics_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (music_service_play_pause() != ESP_OK)
    {
        _lyrics_set_status(state, "当前无法切换播放状态",
                           APP_UI_STATUS_WARNING);
    }
    _lyrics_poll(state);
}

static void _lyrics_mount(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "歌词", true);
    music_ui_add_header_back(&state->page);
    state->play_button = music_ui_plain_icon_button(
                             state->page.header, APP_IMAGE_MUSIC_PLAY,
                             LV_SYMBOL_PLAY, MUSIC_UI_CONTROL_SIZE, 30,
                             APP_UI_COLOR_TEXT, _lyrics_play_clicked, state);
    if (state->play_button != NULL)
    {
        lv_obj_set_style_border_color(state->play_button,
                                      lv_color_hex(APP_UI_COLOR_RAIN), 0);
        lv_obj_set_style_border_width(state->play_button, 3, 0);
    }
    lv_obj_set_style_pad_top(state->page.content, 8, 0);
    lv_obj_set_style_pad_row(state->page.content, 4, 0);

    lv_obj_t *heading = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(heading, 0, 0);
    app_ui_make_passive(heading, false);
    state->title_label = app_ui_add_section(heading, "暂无播放");
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_top(state->title_label, 0, 0);
    lv_obj_set_style_text_font(state->title_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_obj_set_style_text_align(state->title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->artist_label = app_ui_add_body_label(heading,
        "从侧边栏选择歌单开始播放");
    lv_obj_set_style_text_font(state->artist_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->artist_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_align(state->artist_label, LV_TEXT_ALIGN_CENTER, 0);

    state->lyric_panel = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->lyric_panel);
    lv_obj_set_width(state->lyric_panel, LV_PCT(100));
    lv_obj_set_height(state->lyric_panel, 0);
    lv_obj_set_flex_grow(state->lyric_panel, 1);
    lv_obj_set_flex_flow(state->lyric_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->lyric_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->lyric_panel, 6, 0);
    lv_obj_set_scroll_dir(state->lyric_panel, LV_DIR_NONE);
    lv_obj_set_hidden(state->lyric_panel, true);
    app_ui_make_passive(state->lyric_panel, false);
    for (size_t slot = 0U; slot < MUSIC_UI_LYRIC_LINES; ++slot)
    {
        state->lines[slot] = app_ui_add_body_label(state->lyric_panel, "");
        lv_obj_set_width(state->lines[slot], LV_PCT(100));
        lv_label_set_long_mode(state->lines[slot], LV_LABEL_LONG_DOT);
        lv_obj_set_height(state->lines[slot],
                          lv_font_get_line_height(app_ui_font(
                                      APP_THEME_FONT_SMALL)));
        lv_obj_set_style_text_font(state->lines[slot],
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(state->lines[slot],
                                    lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_obj_set_style_text_align(state->lines[slot],
                                    LV_TEXT_ALIGN_CENTER, 0);
    }
    state->status_label = app_ui_add_body_label(state->page.content,
        "正在加载歌词…");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

}

static void _lyrics_resume(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _lyrics_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
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
    music_lyrics_view_reset(&state->view);
    return ESP_OK;
}

static void _lyrics_unmount(const app_manager_page_context_t *context)
{
    music_lyrics_state_t *state = context->state;
    music_lyrics_view_reset(&state->view);
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
