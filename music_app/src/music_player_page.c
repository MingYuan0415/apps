#include "music_app_internal.h"

#include "event_bus.h"
#include "music_lyrics_view.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_PLAYER_LYRIC_HEIGHT 84

typedef struct music_player_state
{
    app_ui_page_t page;
    lv_obj_t *play_view;
    lv_obj_t *context_label;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *meta_label;
    lv_obj_t *lyric_button;
    lv_obj_t *lyric_previous;
    lv_obj_t *lyric_current;
    lv_obj_t *lyric_next;
    lv_obj_t *progress_bar;
    lv_obj_t *timeline;
    lv_obj_t *position_label;
    lv_obj_t *duration_label;
    lv_obj_t *previous_button;
    lv_obj_t *play_button;
    lv_obj_t *next_button;
    lv_obj_t *mode_button;
    lv_obj_t *lyrics_button;
    lv_obj_t *stop_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_lyrics_view_t lyrics;
    uint8_t shown_play_state;
    uint8_t shown_mode;
    int16_t shown_percent;
    bool play_state_valid;
    bool mode_valid;
    bool routed_login;
} music_player_state_t;

_Static_assert(sizeof(music_player_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music player state exceeds the page arena");

static lv_obj_t *_player_label(lv_obj_t *parent, const char *text,
                               app_theme_font_id_t font, uint32_t color,
                               lv_text_align_t align, bool fixed_line)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    if (fixed_line)
    {
        lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static void _player_set_lyric_label(lv_obj_t *label, const char *text,
                                    uint32_t color, app_theme_font_id_t font)
{
    if (label == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(label, text != NULL ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
}

static const char *_player_context_text(const music_player_status_t *status,
                                        bool has_track)
{
    if (!has_track)
    {
        return "QQ 音乐";
    }
    return music_ui_playback_text(status->state);
}

static void _player_set_progress(music_player_state_t *state,
                                 const music_player_status_t *status)
{
    char position[16];
    char duration[16];
    music_ui_format_time(status->position_ms, position, sizeof(position));
    music_ui_format_time(status->duration_ms, duration, sizeof(duration));
    app_ui_label_set_text_if(state->position_label, position);
    app_ui_label_set_text_if(state->duration_label, duration);
    int16_t percent = 0;
    if (status->duration_ms > 0U)
    {
        const uint64_t scaled = (uint64_t)status->position_ms * 100U /
                                status->duration_ms;
        percent = scaled > 100U ? 100 : (int16_t)scaled;
    }
    if (state->shown_percent < 0 || percent != state->shown_percent)
    {
        lv_bar_set_value(state->progress_bar, percent, LV_ANIM_OFF);
        state->shown_percent = percent;
    }
}

static void _player_update_lyrics(music_player_state_t *state,
                                  const music_player_status_t *status)
{
    (void)music_lyrics_view_poll(&state->lyrics, status);
    if (status->title[0] == '\0')
    {
        _player_set_lyric_label(state->lyric_previous, "",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_SMALL);
        _player_set_lyric_label(state->lyric_current, "暂无歌词",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_BODY);
        _player_set_lyric_label(state->lyric_next, "",
                                APP_UI_COLOR_FAINT, APP_THEME_FONT_SMALL);
        return;
    }
    if (state->lyrics.failed)
    {
        _player_set_lyric_label(state->lyric_previous, "",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_SMALL);
        _player_set_lyric_label(state->lyric_current, "暂无歌词",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_BODY);
        _player_set_lyric_label(state->lyric_next, "歌词加载失败",
                                APP_UI_COLOR_FAINT, APP_THEME_FONT_SMALL);
        return;
    }
    if (state->lyrics.snapshot == NULL)
    {
        _player_set_lyric_label(state->lyric_previous, "",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_SMALL);
        _player_set_lyric_label(state->lyric_current, "歌词加载中…",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_BODY);
        _player_set_lyric_label(state->lyric_next, "",
                                APP_UI_COLOR_FAINT, APP_THEME_FONT_SMALL);
        return;
    }
    const char *previous = NULL;
    const char *current = NULL;
    const char *next = NULL;
    if (!music_ui_lyric_triplet(state->lyrics.snapshot, status->position_ms,
                                &previous, &current, &next) ||
            current == NULL)
    {
        _player_set_lyric_label(state->lyric_current, "暂无歌词",
                                APP_UI_COLOR_MUTED, APP_THEME_FONT_BODY);
        return;
    }
    _player_set_lyric_label(state->lyric_previous,
                            previous != NULL ? previous : "",
                            APP_UI_COLOR_MUTED, APP_THEME_FONT_SMALL);
    _player_set_lyric_label(state->lyric_current,
                            current[0] == '\0' ? "·" : current,
                            APP_UI_COLOR_RAIN, APP_THEME_FONT_BODY);
    _player_set_lyric_label(state->lyric_next,
                            next != NULL ? next : "",
                            APP_UI_COLOR_MUTED, APP_THEME_FONT_SMALL);
}

static void _player_update_play_button(music_player_state_t *state,
                                       const music_player_status_t *status)
{
    if (state->play_button == NULL)
    {
        return;
    }
    const bool playing = status->state == MUSIC_PLAYBACK_PLAYING;
    const bool enabled = music_ui_can_toggle_playback(status);
    if (!state->play_state_valid || state->shown_play_state != status->state)
    {
        music_ui_set_plain_icon_button(
            state->play_button,
            playing ? APP_IMAGE_MUSIC_PAUSE : APP_IMAGE_MUSIC_PLAY,
            playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, 38,
            enabled ? APP_UI_COLOR_TEXT : APP_UI_COLOR_FAINT);
        state->shown_play_state = status->state;
        state->play_state_valid = true;
    }
    music_ui_set_button_enabled(state->play_button, enabled);
    lv_obj_set_style_border_color(state->play_button,
                                  lv_color_hex(enabled ? APP_UI_COLOR_RAIN :
                                          APP_UI_COLOR_FAINT), 0);
    lv_obj_set_style_border_width(state->play_button, 3, 0);
}

static void _player_update(music_player_state_t *state)
{
    music_service_status_t account;
    if (music_service_get_status(&account) == ESP_OK &&
            !account.logged_in &&
            (account.state == MUSIC_SERVICE_STATE_LOGGED_OUT ||
             account.state == MUSIC_SERVICE_STATE_AUTH_REQUIRED) &&
            !state->routed_login)
    {
        state->routed_login = true;
        (void)music_ui_reset_to_login();
        return;
    }

    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        app_ui_label_set_text_if(state->title_label, "音乐服务不可用");
        app_ui_label_set_text_if(state->artist_label, "请返回后重新打开音乐");
        app_ui_label_set_text_if(state->context_label, "QQ 音乐");
        app_ui_label_set_text_if(state->meta_label, "");
        music_ui_set_button_enabled(state->play_button, false);
        music_ui_set_button_enabled(state->previous_button, false);
        music_ui_set_button_enabled(state->next_button, false);
        music_ui_set_button_enabled(state->stop_button, false);
        music_ui_set_button_enabled(state->lyrics_button, false);
        return;
    }

    const bool has_track = status.title[0] != '\0';
    lv_obj_set_hidden(state->timeline, !has_track);
    app_ui_label_set_text_if(state->context_label,
                             _player_context_text(&status, has_track));
    app_ui_label_set_text_if(state->title_label,
                             has_track ? status.title : "暂无播放");
    app_ui_label_set_text_if(state->artist_label,
                             has_track ?
                             (status.artist[0] == '\0' ? "未知歌手" :
                              status.artist) :
                             "从侧边栏选择歌单开始播放");

    char queue[MUSIC_UI_QUEUE_TEXT_BYTES];
    music_ui_queue_text(&status, queue, sizeof(queue));
    if (status.state == MUSIC_PLAYBACK_ERROR &&
            status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        app_ui_label_set_text_if(state->meta_label,
                                 music_ui_failure_text(status.failure));
    }
    else if (has_track)
    {
        char meta[96];
        (void)snprintf(meta, sizeof(meta), "%s · %s",
                       queue[0] == '\0' ? "当前队列" : queue,
                       music_ui_quality_text(status.quality));
        app_ui_label_set_text_if(state->meta_label, meta);
    }
    else
    {
        app_ui_label_set_text_if(state->meta_label, "QQ 音乐");
    }
    _player_set_progress(state, &status);
    _player_update_lyrics(state, &status);
    _player_update_play_button(state, &status);

    if (!state->mode_valid || state->shown_mode != status.mode)
    {
        music_ui_set_plain_icon_button(
            state->mode_button, music_ui_mode_icon(status.mode),
            music_ui_mode_icon(status.mode) == APP_IMAGE_UI_LOOP ?
            LV_SYMBOL_LOOP : LV_SYMBOL_RIGHT, 30,
            status.mode == MUSIC_PLAY_MODE_SEQUENTIAL ?
            APP_UI_COLOR_MUTED : APP_UI_COLOR_RAIN);
        state->shown_mode = status.mode;
        state->mode_valid = true;
    }
    music_ui_set_button_enabled(state->previous_button,
                                music_ui_can_step(&status, false));
    music_ui_set_button_enabled(state->next_button,
                                music_ui_can_step(&status, true));
    music_ui_set_button_enabled(state->stop_button, has_track);
    music_ui_set_button_enabled(state->lyrics_button,
                                has_track && status.songmid[0] != '\0');

    uint32_t meta_color = APP_UI_COLOR_FAINT;
    if (status.state == MUSIC_PLAYBACK_ERROR)
    {
        meta_color = APP_UI_COLOR_SUN;
    }
    else if (status.state == MUSIC_PLAYBACK_BUFFERING)
    {
        meta_color = APP_UI_COLOR_RAIN;
    }
    lv_obj_set_style_text_color(state->meta_label,
                                lv_color_hex(meta_color), 0);
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
    if (state != NULL)
    {
        _player_update(state);
    }
}

static void _player_play_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = music_service_play_pause();
    if (state != NULL && result != ESP_OK)
    {
        app_ui_label_set_text_if(state->meta_label,
                                 result == ESP_ERR_TIMEOUT ?
                                 "播放请求超时，请重试" :
                                 "当前无法切换播放状态");
        lv_obj_set_style_text_color(state->meta_label,
                                    lv_color_hex(APP_UI_COLOR_SUN), 0);
    }
    if (state != NULL)
    {
        _player_update(state);
    }
}

static void _player_prev_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    music_player_status_t before;
    const bool had_status =
        state != NULL && music_service_get_player_status(&before) == ESP_OK;
    const esp_err_t result = music_service_prev();
    if (state != NULL && result != ESP_OK)
    {
        app_ui_label_set_text_if(state->meta_label,
                                 had_status &&
                                 !music_ui_can_step(&before, false) ?
                                 "已经是队列开头" :
                                 "暂时无法切换歌曲，请重试");
        lv_obj_set_style_text_color(state->meta_label,
                                    lv_color_hex(APP_UI_COLOR_SUN), 0);
    }
    if (state != NULL)
    {
        _player_update(state);
    }
}

static void _player_next_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    music_player_status_t before;
    const bool had_status =
        state != NULL && music_service_get_player_status(&before) == ESP_OK;
    const esp_err_t result = music_service_next();
    if (state != NULL && result != ESP_OK)
    {
        app_ui_label_set_text_if(state->meta_label,
                                 had_status &&
                                 !music_ui_can_step(&before, true) ?
                                 "已经是队列末尾" :
                                 "暂时无法切换歌曲，请重试");
        lv_obj_set_style_text_color(state->meta_label,
                                    lv_color_hex(APP_UI_COLOR_SUN), 0);
    }
    if (state != NULL)
    {
        _player_update(state);
    }
}

static void _player_mode_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    music_player_status_t status;
    if (state == NULL || music_service_get_player_status(&status) != ESP_OK)
    {
        return;
    }
    const music_play_mode_t next = music_ui_next_mode(status.mode);
    if (music_service_set_mode(next) != ESP_OK)
    {
        app_ui_label_set_text_if(state->meta_label, "播放模式切换失败");
        return;
    }
    _player_update(state);
}

static void _player_stop_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    if (music_service_stop() != ESP_OK)
    {
        app_ui_label_set_text_if(state->meta_label, "停止播放失败，请重试");
        return;
    }
    _player_update(state);
}

static void _player_sidebar_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_sidebar();
}

static void _player_queue_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_queue();
}

static void _player_lyrics_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_lyrics();
}

static lv_obj_t *_player_play_button(lv_obj_t *parent,
                                     music_player_state_t *state)
{
    lv_obj_t *button = music_ui_plain_icon_button(
                           parent, APP_IMAGE_MUSIC_PLAY, LV_SYMBOL_PLAY,
                           MUSIC_UI_PLAY_SIZE, 50, APP_UI_COLOR_TEXT,
                           _player_play_clicked, state);
    if (button != NULL)
    {
        lv_obj_set_style_border_color(button,
                                      lv_color_hex(APP_UI_COLOR_RAIN), 0);
        lv_obj_set_style_border_width(button, 3, 0);
    }
    return button;
}

static void _player_mount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->shown_percent = -1;
    app_ui_page_create_home(&state->page);
    lv_obj_set_style_pad_all(state->page.content, 0, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);

    state->play_view = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->play_view);
    lv_obj_set_size(state->play_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(state->play_view, 12, 0);
    lv_obj_set_style_pad_bottom(state->play_view, 8, 0);
    lv_obj_set_style_pad_left(state->play_view, 16, 0);
    lv_obj_set_style_pad_right(state->play_view, 16, 0);
    lv_obj_set_style_pad_row(state->play_view, 6, 0);
    lv_obj_set_flex_flow(state->play_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->play_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(state->play_view, false);

    lv_obj_t *top = lv_obj_create(state->play_view);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, MUSIC_UI_CONTROL_SIZE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(top, false);
    (void)music_ui_plain_icon_button(
        top, APP_IMAGE_MUSIC_MENU, LV_SYMBOL_LIST,
        MUSIC_UI_CONTROL_SIZE, 30, APP_UI_COLOR_TEXT,
        _player_sidebar_clicked, state);
    state->context_label = lv_label_create(top);
    if (state->context_label != NULL)
    {
        lv_obj_set_width(state->context_label, 0);
        lv_obj_set_flex_grow(state->context_label, 1);
        lv_obj_set_style_text_font(state->context_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(state->context_label,
                                    lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_obj_set_style_text_align(state->context_label,
                                    LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(state->context_label, "QQ 音乐");
        app_ui_make_passive(state->context_label, false);
    }
    (void)music_ui_plain_icon_button(
        top, APP_IMAGE_MUSIC_QUEUE, LV_SYMBOL_LIST,
        MUSIC_UI_CONTROL_SIZE, 30, APP_UI_COLOR_TEXT,
        _player_queue_clicked, state);

    lv_obj_t *heading = lv_obj_create(state->play_view);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, 68);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(heading, 4, 0);
    app_ui_make_passive(heading, false);
    state->title_label = _player_label(heading, "暂无播放",
                                       APP_THEME_FONT_BIGL,
                                       APP_UI_COLOR_TEXT,
                                       LV_TEXT_ALIGN_CENTER, true);
    state->artist_label = _player_label(heading, "从侧边栏选择歌单开始播放",
                                        APP_THEME_FONT_SMALL,
                                        APP_UI_COLOR_MUTED,
                                        LV_TEXT_ALIGN_CENTER, true);

    state->lyric_button = lv_button_create(state->play_view);
    if (state->lyric_button != NULL)
    {
        app_ui_click_only(state->lyric_button);
        lv_obj_set_width(state->lyric_button, LV_PCT(100));
        lv_obj_set_height(state->lyric_button, MUSIC_PLAYER_LYRIC_HEIGHT);
        music_ui_flat_button_reset(state->lyric_button,
                                   APP_UI_RADIUS_CONTROL);
        lv_obj_set_style_pad_all(state->lyric_button, 4, 0);
        lv_obj_set_style_pad_row(state->lyric_button, 0, 0);
        lv_obj_set_style_bg_color(state->lyric_button,
                                  lv_color_hex(APP_UI_COLOR_SURFACE),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(state->lyric_button, LV_OPA_COVER,
                                LV_STATE_PRESSED);
        lv_obj_set_flex_flow(state->lyric_button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(state->lyric_button, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(state->lyric_button, _player_lyrics_clicked,
                            LV_EVENT_CLICKED, state);
        state->lyric_previous = _player_label(
                                    state->lyric_button, "",
                                    APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED,
                                    LV_TEXT_ALIGN_CENTER, true);
        state->lyric_current = _player_label(
                                   state->lyric_button, "暂无歌词",
                                   APP_THEME_FONT_BODY, APP_UI_COLOR_MUTED,
                                   LV_TEXT_ALIGN_CENTER, true);
        state->lyric_next = _player_label(
                                state->lyric_button, "",
                                APP_THEME_FONT_SMALL, APP_UI_COLOR_FAINT,
                                LV_TEXT_ALIGN_CENTER, true);
    }

    lv_obj_t *transport = lv_obj_create(state->play_view);
    lv_obj_remove_style_all(transport);
    lv_obj_set_width(transport, LV_PCT(100));
    lv_obj_set_height(transport, MUSIC_UI_PLAY_SIZE);
    lv_obj_set_flex_flow(transport, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transport, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(transport, false);
    state->previous_button = music_ui_plain_icon_button(
                                 transport, APP_IMAGE_MUSIC_PREV,
                                 LV_SYMBOL_PREV, MUSIC_UI_TRANSPORT_SIZE, 54,
                                 APP_UI_COLOR_TEXT, _player_prev_clicked,
                                 state);
    state->play_button = _player_play_button(transport, state);
    state->next_button = music_ui_plain_icon_button(
                             transport, APP_IMAGE_MUSIC_NEXT,
                             LV_SYMBOL_NEXT, MUSIC_UI_TRANSPORT_SIZE, 54,
                             APP_UI_COLOR_TEXT, _player_next_clicked,
                             state);

    state->timeline = lv_obj_create(state->play_view);
    lv_obj_t *timeline = state->timeline;
    lv_obj_remove_style_all(timeline);
    lv_obj_set_width(timeline, LV_PCT(100));
    lv_obj_set_height(timeline, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(timeline, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(timeline, 4, 0);
    app_ui_make_passive(timeline, false);
    state->progress_bar = lv_bar_create(timeline);
    lv_obj_set_width(state->progress_bar, LV_PCT(100));
    lv_obj_set_height(state->progress_bar, 4);
    lv_bar_set_range(state->progress_bar, 0, 100);
    lv_obj_set_style_bg_color(state->progress_bar,
                              lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_color(state->progress_bar,
                              lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(state->progress_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(state->progress_bar, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    app_ui_make_passive(state->progress_bar, false);
    lv_obj_t *time_row = lv_obj_create(timeline);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_width(time_row, LV_PCT(100));
    lv_obj_set_height(time_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(time_row, false);
    state->position_label = _player_label(time_row, "0:00",
                                          APP_THEME_FONT_SMALL,
                                          APP_UI_COLOR_MUTED,
                                          LV_TEXT_ALIGN_LEFT, true);
    lv_obj_set_width(state->position_label, LV_SIZE_CONTENT);
    state->duration_label = _player_label(time_row, "0:00",
                                          APP_THEME_FONT_SMALL,
                                          APP_UI_COLOR_MUTED,
                                          LV_TEXT_ALIGN_RIGHT, true);
    lv_obj_set_width(state->duration_label, LV_SIZE_CONTENT);

    state->meta_label = _player_label(timeline, "QQ 音乐",
                                      APP_THEME_FONT_SMALL,
                                      APP_UI_COLOR_FAINT,
                                      LV_TEXT_ALIGN_CENTER, true);

    lv_obj_t *secondary = lv_obj_create(state->play_view);
    lv_obj_remove_style_all(secondary);
    lv_obj_set_width(secondary, LV_PCT(100));
    lv_obj_set_height(secondary, MUSIC_UI_CONTROL_SIZE);
    lv_obj_set_flex_flow(secondary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(secondary, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(secondary, false);
    state->mode_button = music_ui_plain_icon_button(
                             secondary, APP_IMAGE_MUSIC_SEQUENTIAL,
                             LV_SYMBOL_LOOP, MUSIC_UI_CONTROL_SIZE, 32,
                             APP_UI_COLOR_TEXT, _player_mode_clicked, state);
    state->lyrics_button = music_ui_plain_text_button(
                               secondary, "词", MUSIC_UI_CONTROL_SIZE,
                               APP_THEME_FONT_HEAD, APP_UI_COLOR_TEXT,
                               _player_lyrics_clicked, state);
    state->stop_button = music_ui_plain_icon_button(
                             secondary, APP_IMAGE_MUSIC_STOP,
                             LV_SYMBOL_STOP, MUSIC_UI_CONTROL_SIZE, 32,
                             APP_UI_COLOR_TEXT, _player_stop_clicked, state);
}

static void _player_resume(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->routed_login = false;
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
    music_lyrics_view_reset(&state->lyrics);
    return ESP_OK;
}

static void _player_unmount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    music_lyrics_view_reset(&state->lyrics);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
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
