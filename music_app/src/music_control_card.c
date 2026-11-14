#define DBG_TAG "music_control_card"
#include "mt_log.h"

#include "music_app_internal.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_CARD_PAD          16
#define MUSIC_CARD_HEADER_H     64
#define MUSIC_CARD_ARTWORK      40
#define MUSIC_CARD_CONTROL      52
#define MUSIC_CARD_BUSY_MS      1500U

typedef struct music_control_card_state
{
    lv_obj_t *title;
    lv_obj_t *artist;
    lv_obj_t *status;
    lv_obj_t *progress;
    lv_obj_t *previous;
    lv_obj_t *play;
    lv_obj_t *next;
    music_playback_state_t shown_state;
    bool shown_state_valid;
    bool shown_retry;
    bool shown_retry_valid;
    uint64_t shown_generation;
    uint64_t busy_generation;
    uint32_t busy_started_ms;
    bool busy;
    bool unavailable;
} music_control_card_state_t;

static music_control_card_state_t s_music_card;
static bool s_music_card_mounted;
static bool s_music_card_last_active;

static void _music_card_format_time(uint32_t milliseconds, char *output,
                                    size_t output_size)
{
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(milliseconds / 60000U),
                   (unsigned)((milliseconds / 1000U) % 60U));
}

static void _music_card_set_actions_enabled(
    music_control_card_state_t *state, bool enabled)
{
    music_ui_set_button_enabled(state->previous, enabled);
    music_ui_set_button_enabled(state->play, enabled);
    music_ui_set_button_enabled(state->next, enabled);
}

static void _music_card_set_unavailable(music_control_card_state_t *state)
{
    state->unavailable = true;
    state->busy = false;
    app_ui_set_status_text(state->status, "音乐服务暂不可用",
                           APP_UI_STATUS_ERROR);
    lv_obj_set_hidden(state->progress, true);
    music_ui_set_button_busy(state->play, false);
    _music_card_set_actions_enabled(state, false);
}

static void _music_card_show_error(const char *text)
{
    app_ui_set_status_text(s_music_card.status, text, APP_UI_STATUS_ERROR);
}

static void _music_card_update_play_button(
    music_control_card_state_t *state, const music_player_status_t *status)
{
    const bool retry = music_ui_can_retry_playback(status);
    const bool enabled = music_ui_can_toggle_playback(status) || retry;
    if (!state->shown_state_valid || state->shown_state != status->state ||
            !state->shown_retry_valid || state->shown_retry != retry)
    {
        music_ui_set_plain_icon_button(
            state->play,
            retry ? APP_IMAGE_UI_REFRESH :
            (status->state == MUSIC_PLAYBACK_PLAYING ?
             APP_IMAGE_MUSIC_PAUSE : APP_IMAGE_MUSIC_PLAY),
            retry ? LV_SYMBOL_REFRESH :
            (status->state == MUSIC_PLAYBACK_PLAYING ?
             LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY),
            34,
            enabled ? APP_UI_COLOR_TEXT : APP_UI_COLOR_FAINT);
        state->shown_state = status->state;
        state->shown_retry = retry;
        state->shown_state_valid = true;
        state->shown_retry_valid = true;
    }
    music_ui_set_button_busy(state->play, state->busy);
    music_ui_set_button_enabled(
        state->play, !state->busy && enabled);
    music_ui_set_button_enabled(
        state->previous, !state->busy && music_ui_can_step(status, false));
    music_ui_set_button_enabled(
        state->next, !state->busy && music_ui_can_step(status, true));
}

static void _music_card_refresh(void *opaque)
{
    music_control_card_state_t *state = opaque;
    music_player_status_t status;
    if (state == NULL ||
            music_service_get_player_status(&status) != ESP_OK)
    {
        if (state != NULL)
        {
            _music_card_set_unavailable(state);
        }
        return;
    }
    state->unavailable = false;
    if (state->busy &&
            (status.generation != state->busy_generation ||
             (uint32_t)(lv_tick_get() - state->busy_started_ms) >=
             MUSIC_CARD_BUSY_MS))
    {
        state->busy = false;
    }
    state->shown_generation = status.generation;
    app_ui_label_set_text_if(
        state->title, status.title[0] != '\0' ? status.title : "QQ 音乐");
    app_ui_label_set_text_if(
        state->artist, status.artist[0] != '\0' ? status.artist : "音乐");
    char timeline[64];
    char position[12];
    char duration[12];
    _music_card_format_time(status.position_ms, position, sizeof(position));
    _music_card_format_time(status.duration_ms, duration, sizeof(duration));
    if (status.duration_ms > 0U)
    {
        (void)snprintf(timeline, sizeof(timeline), "%s · %s / %s",
                       music_ui_playback_text(status.state), position, duration);
        lv_obj_set_hidden(state->progress, false);
        lv_bar_set_value(state->progress,
                         (int32_t)((uint64_t)status.position_ms * 1000U /
                                   status.duration_ms),
                         LV_ANIM_OFF);
    }
    else
    {
        (void)snprintf(timeline, sizeof(timeline), "%s",
                       music_ui_playback_text(status.state));
        lv_obj_set_hidden(state->progress, true);
    }
    app_ui_set_status_text(
        state->status, timeline,
        status.state == MUSIC_PLAYBACK_ERROR ?
        APP_UI_STATUS_ERROR :
        status.state == MUSIC_PLAYBACK_PLAYING ?
        APP_UI_STATUS_ACCENT : APP_UI_STATUS_NEUTRAL);
    _music_card_update_play_button(state, &status);
}

static void _music_card_begin_command(music_control_card_state_t *state)
{
    music_player_status_t status;
    state->busy = true;
    state->busy_started_ms = lv_tick_get();
    state->busy_generation = music_service_get_player_status(&status) == ESP_OK ?
                             status.generation : state->shown_generation;
    app_ui_label_set_text_if(state->status, "正在处理");
    lv_obj_set_style_text_color(state->status,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    _music_card_set_actions_enabled(state, false);
    music_ui_set_button_busy(state->play, true);
}

static void _music_card_finish_command(music_control_card_state_t *state,
                                       esp_err_t result)
{
    if (result == ESP_OK)
    {
        _music_card_refresh(state);
        return;
    }
    LOG_W("music command rejected: %s", esp_err_to_name(result));
    state->busy = false;
    music_ui_set_button_busy(state->play, false);
    _music_card_show_error("操作失败");
    music_player_status_t status;
    if (music_service_get_player_status(&status) == ESP_OK)
    {
        state->unavailable = false;
        _music_card_update_play_button(state, &status);
    }
    else
    {
        _music_card_set_unavailable(state);
    }
}

static void _music_card_play_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    music_control_card_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->busy || state->unavailable)
    {
        return;
    }
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        _music_card_set_unavailable(state);
        return;
    }
    _music_card_begin_command(state);
    const esp_err_t result = music_ui_can_retry_playback(&status) ?
                             music_service_queue_play(status.queue_index) :
                             music_service_play_pause();
    if (result != ESP_OK)
    {
        _music_card_finish_command(state, result);
        return;
    }
    _music_card_refresh(state);
}

static void _music_card_step(music_control_card_state_t *state, bool forward)
{
    if (state == NULL || state->busy || state->unavailable)
    {
        return;
    }
    _music_card_begin_command(state);
    const esp_err_t result = forward ? music_service_next() :
                             music_service_prev();
    _music_card_finish_command(state, result);
}

static void _music_card_previous_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _music_card_step(lv_event_get_user_data(event), false);
    }
}

static void _music_card_next_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        _music_card_step(lv_event_get_user_data(event), true);
    }
}

static void _music_card_open_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_MUSIC, MUSIC_PAGE_PLAYER);
    }
}

static lv_obj_t *_music_card_label(lv_obj_t *parent, const char *text,
                                   app_theme_font_id_t font, uint32_t color,
                                   bool grow)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    if (grow)
    {
        lv_obj_set_width(label, 0);
        lv_obj_set_flex_grow(label, 1);
    }
    else
    {
        lv_obj_set_width(label, LV_PCT(100));
    }
    lv_obj_set_height(label, lv_font_get_line_height(app_ui_font(font)));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static esp_err_t _music_card_mount(lv_obj_t *parent, void **out_state)
{
    if (parent == NULL || out_state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_music_card, 0, sizeof(s_music_card));
    *out_state = &s_music_card;

    lv_obj_t *card = lv_obj_create(parent);
    if (card == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, MUSIC_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(card, false);

    lv_obj_t *header = lv_button_create(card);
    if (header == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    app_ui_click_only(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, MUSIC_CARD_HEADER_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, APP_UI_GAP, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(header, _music_card_open_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *artwork = app_ui_image(header, APP_IMAGE_MUSIC_ICON,
                                     LV_SYMBOL_AUDIO, MUSIC_CARD_ARTWORK);
    if (artwork != NULL)
    {
        app_ui_icon_set_color(artwork, APP_UI_COLOR_RAIN);
    }
    lv_obj_t *identity = lv_obj_create(header);
    if (identity == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(identity);
    lv_obj_set_width(identity, 0);
    lv_obj_set_flex_grow(identity, 1);
    lv_obj_set_height(identity, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(identity, 2, 0);
    app_ui_make_passive(identity, false);
    s_music_card.title = _music_card_label(identity, "QQ 音乐",
                                           APP_THEME_FONT_BODY,
                                           APP_UI_COLOR_TEXT, false);
    s_music_card.artist = _music_card_label(identity, "音乐",
                                            APP_THEME_FONT_SMALL,
                                            APP_UI_COLOR_MUTED, false);
    lv_obj_t *open = lv_label_create(header);
    if (open != NULL)
    {
        lv_obj_set_style_text_font(open, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(open, lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_label_set_text(open, LV_SYMBOL_RIGHT);
        app_ui_make_passive(open, false);
    }

    s_music_card.status = _music_card_label(card, "未在播放",
                                            APP_THEME_FONT_SMALL,
                                            APP_UI_COLOR_MUTED, false);
    if (s_music_card.status != NULL)
    {
        lv_obj_set_style_text_align(s_music_card.status,
                                    LV_TEXT_ALIGN_CENTER, 0);
    }

    s_music_card.progress = lv_bar_create(card);
    if (s_music_card.progress == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_width(s_music_card.progress, LV_PCT(100));
    lv_obj_set_height(s_music_card.progress, 4);
    lv_bar_set_range(s_music_card.progress, 0, 1000);
    lv_bar_set_value(s_music_card.progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_music_card.progress,
                              lv_color_hex(APP_UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_music_card.progress, LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music_card.progress,
                              lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_music_card.progress, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN);
    lv_obj_set_style_radius(s_music_card.progress, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    app_ui_make_passive(s_music_card.progress, false);

    lv_obj_t *controls = lv_obj_create(card);
    if (controls == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(controls);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, MUSIC_CARD_CONTROL);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 20, 0);
    app_ui_make_passive(controls, false);

    s_music_card.previous = music_ui_plain_icon_button(
                                controls, APP_IMAGE_MUSIC_PREV,
                                LV_SYMBOL_PREV, MUSIC_CARD_CONTROL, 30,
                                APP_UI_COLOR_TEXT, _music_card_previous_event,
                                &s_music_card);
    s_music_card.play = music_ui_plain_icon_button(
                            controls, APP_IMAGE_MUSIC_PLAY, LV_SYMBOL_PLAY,
                            MUSIC_CARD_CONTROL, 34, APP_UI_COLOR_TEXT,
                            _music_card_play_event, &s_music_card);
    s_music_card.next = music_ui_plain_icon_button(
                            controls, APP_IMAGE_MUSIC_NEXT, LV_SYMBOL_NEXT,
                            MUSIC_CARD_CONTROL, 30, APP_UI_COLOR_TEXT,
                            _music_card_next_event, &s_music_card);
    if (s_music_card.previous == NULL || s_music_card.play == NULL ||
            s_music_card.next == NULL || s_music_card.title == NULL ||
            s_music_card.artist == NULL || s_music_card.status == NULL ||
            s_music_card.progress == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    _music_card_refresh(&s_music_card);
    s_music_card_mounted = true;
    return ESP_OK;
}

static void _music_card_unmount(void *opaque)
{
    (void)opaque;
    memset(&s_music_card, 0, sizeof(s_music_card));
    s_music_card_mounted = false;
    s_music_card_last_active = false;
}

static bool _music_card_is_active(void)
{
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        /* A mounted card must survive a transient snapshot failure so its
         * refresh() can render the unavailable state. A cold failure stays
         * inactive and therefore never mounts a card. */
        return s_music_card_mounted && s_music_card_last_active;
    }
    const bool active = status.title[0] != '\0' &&
                        (status.state == MUSIC_PLAYBACK_BUFFERING ||
                         status.state == MUSIC_PLAYBACK_PLAYING ||
                         status.state == MUSIC_PLAYBACK_PAUSED ||
                         status.state == MUSIC_PLAYBACK_ERROR);
    s_music_card_last_active = active;
    return active;
}

const app_manager_control_center_card_ops_t music_control_center_card_ops =
{
    .is_active = _music_card_is_active,
    .mount = _music_card_mount,
    .refresh = _music_card_refresh,
    .unmount = _music_card_unmount,
};
