#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_PLAYER_COVER_SIZE 96
#define MUSIC_PLAYER_TIMEOUT_TICKS 40U
#define MUSIC_PLAYER_MAX_ATTEMPTS 2U

typedef struct music_player_state
{
    app_ui_page_t page;
    lv_obj_t *cover_area;
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *queue_label;
    lv_obj_t *state_pill;
    lv_obj_t *progress_bar;
    lv_obj_t *timeline;
    lv_obj_t *position_label;
    lv_obj_t *duration_label;
    lv_obj_t *previous_button;
    lv_obj_t *play_button;
    lv_obj_t *next_button;
    lv_obj_t *mode_button;
    lv_obj_t *stop_button;
    lv_obj_t *lyrics_button;
    lv_obj_t *guidance_label;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_ui_cover_t cover;
    uint32_t cover_token;
    char cover_url[MUSIC_SERVICE_URL_BYTES];
    uint16_t cover_wait_ticks;
    uint8_t cover_attempts;
    bool cover_requested;
    bool cover_failed;
    uint8_t last_state;
    uint8_t last_failure;
} music_player_state_t;

_Static_assert(sizeof(music_player_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music player state exceeds the page arena");

static void _player_set_control_icon(lv_obj_t *button, uint32_t image_id,
                                     const char *fallback, uint32_t color);

static uint32_t _player_cover_token(const char *url)
{
    uint32_t hash = 2166136261U;
    if (url != NULL)
    {
        for (const unsigned char *cursor = (const unsigned char *)url;
                *cursor != '\0'; ++cursor)
        {
            hash ^= *cursor;
            hash *= 16777619U;
        }
    }
    return hash;
}

static void _player_set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL)
    {
        return;
    }
    if (enabled)
    {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

static void _player_set_guidance(music_player_state_t *state,
                                 const char *text)
{
    if (state->guidance_label == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(state->guidance_label, text);
    lv_obj_set_hidden(state->guidance_label, text == NULL || text[0] == '\0');
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
    int32_t percent = 0;
    if (status->duration_ms > 0U)
    {
        const uint64_t scaled = (uint64_t)status->position_ms * 100U /
                                status->duration_ms;
        percent = scaled > 100U ? 100 : (int32_t)scaled;
    }
    lv_bar_set_value(state->progress_bar, percent, LV_ANIM_OFF);
}

static void _player_update_cover(music_player_state_t *state,
                                 const music_player_status_t *status)
{
    if (state->cover_requested)
    {
        if (state->cover_wait_ticks < UINT16_MAX)
        {
            ++state->cover_wait_ticks;
        }
        if (state->cover_wait_ticks > MUSIC_PLAYER_TIMEOUT_TICKS)
        {
            state->cover_requested = false;
            state->cover_failed =
                state->cover_attempts >= MUSIC_PLAYER_MAX_ATTEMPTS;
        }
    }
    if (status->cover_url[0] == '\0')
    {
        music_ui_cover_release(&state->cover);
        state->cover_url[0] = '\0';
        state->cover_requested = false;
        state->cover_failed = false;
        state->cover_attempts = 0U;
        state->cover_wait_ticks = 0U;
        return;
    }
    if (strcmp(state->cover_url, status->cover_url) != 0)
    {
        music_ui_cover_release(&state->cover);
        (void)snprintf(state->cover_url, sizeof(state->cover_url), "%s",
                       status->cover_url);
        state->cover_requested = false;
        state->cover_failed = false;
        state->cover_attempts = 0U;
        state->cover_wait_ticks = 0U;
    }
    if (state->cover_requested || state->cover_failed ||
            state->cover.image != NULL ||
            state->cover_attempts >= MUSIC_PLAYER_MAX_ATTEMPTS)
    {
        return;
    }
    const music_image_t *image = NULL;
    state->cover_token = _player_cover_token(status->cover_url);
    state->cover_requested = true;
    state->cover_wait_ticks = 0U;
    ++state->cover_attempts;
    const esp_err_t result = music_service_image_acquire(
                                 status->cover_url, MUSIC_PLAYER_COVER_SIZE,
                                 MUSIC_PLAYER_COVER_SIZE, state->cover_token,
                                 &image);
    if (result == ESP_OK && image != NULL)
    {
        state->cover_requested = false;
        state->cover_failed = false;
        state->cover_attempts = 0U;
        music_ui_cover_set_image(&state->cover, (music_image_t *)image);
    }
    else if (result != ESP_ERR_NOT_FOUND)
    {
        state->cover_requested = false;
        state->cover_failed =
            state->cover_attempts >= MUSIC_PLAYER_MAX_ATTEMPTS;
    }
}

static void _player_update(music_player_state_t *state)
{
    music_player_status_t status;
    if (music_service_get_player_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->state_pill, "服务不可用",
                               APP_UI_STATUS_ERROR);
        app_ui_label_set_text_if(state->title_label, "播放服务不可用");
        app_ui_label_set_text_if(state->artist_label,
                                 "请返回首页确认网络与登录状态");
        _player_set_guidance(state, "返回首页后重新打开音乐");
        _player_set_button_enabled(state->play_button, false);
        _player_set_button_enabled(state->previous_button, false);
        _player_set_button_enabled(state->next_button, false);
        _player_set_button_enabled(state->stop_button, false);
        _player_set_button_enabled(state->lyrics_button, false);
        return;
    }

    const bool has_track = status.title[0] != '\0';
    lv_obj_set_hidden(state->timeline, !has_track);
    app_ui_label_set_text_if(state->title_label,
                             has_track ? status.title : "暂无播放");
    app_ui_label_set_text_if(state->artist_label,
                             has_track ?
                             (status.artist[0] == '\0' ? "未知歌手" :
                              status.artist) :
                             "从歌单选择一首歌开始");
    char queue[MUSIC_UI_QUEUE_TEXT_BYTES];
    music_ui_queue_text(&status, queue, sizeof(queue));
    if (has_track)
    {
        char queue_detail[64];
        (void)snprintf(queue_detail, sizeof(queue_detail), "%s · %s",
                       queue, music_ui_quality_text(status.quality));
        app_ui_label_set_text_if(state->queue_label, queue_detail);
    }
    else
    {
        app_ui_label_set_text_if(state->queue_label, "QQ 音乐");
    }
    _player_set_progress(state, &status);
    _player_update_cover(state, &status);

    const bool playback_error = status.state == MUSIC_PLAYBACK_ERROR;
    const bool warning = playback_error ||
                         status.failure != MUSIC_SERVICE_FAILURE_NONE;
    app_ui_pill_set(state->state_pill,
                    music_ui_playback_text(status.state),
                    playback_error ? APP_UI_STATUS_ERROR :
                    status.state == MUSIC_PLAYBACK_PLAYING ?
                    APP_UI_STATUS_SUCCESS :
                    status.state == MUSIC_PLAYBACK_BUFFERING ?
                    APP_UI_STATUS_ACCENT :
                    warning ? APP_UI_STATUS_WARNING :
                    APP_UI_STATUS_NEUTRAL);
    const bool pause_icon = status.state == MUSIC_PLAYBACK_PLAYING;
    _player_set_control_icon(
        state->play_button,
        pause_icon ? APP_IMAGE_MUSIC_PAUSE : APP_IMAGE_MUSIC_PLAY,
        pause_icon ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY,
        music_ui_can_toggle_playback(&status) ?
        APP_UI_COLOR_BACKGROUND : APP_UI_COLOR_FAINT);
    app_ui_button_set_text(state->mode_button,
                           music_ui_mode_text(status.mode));
    _player_set_button_enabled(state->play_button,
                               music_ui_can_toggle_playback(&status));
    _player_set_button_enabled(state->previous_button,
                               music_ui_can_step(&status, false));
    _player_set_button_enabled(state->next_button,
                               music_ui_can_step(&status, true));
    _player_set_button_enabled(state->stop_button, has_track);
    _player_set_button_enabled(state->lyrics_button,
                               has_track && status.songmid[0] != '\0');

    if (playback_error)
    {
        _player_set_guidance(state,
                             music_ui_failure_text(status.failure));
    }
    else if (status.state == MUSIC_PLAYBACK_BUFFERING)
    {
        _player_set_guidance(state, "正在缓冲，请稍候");
    }
    else if (!has_track)
    {
        _player_set_guidance(state, "返回歌单，点选歌曲即可播放");
    }
    else
    {
        _player_set_guidance(state, "");
    }
    state->last_state = status.state;
    state->last_failure = status.failure;
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
            state->cover_wait_ticks = 0U;
            if (event->failure == MUSIC_SERVICE_FAILURE_NONE)
            {
                state->cover_failed = false;
                state->cover_attempts = 0U;
                (void)music_ui_cover_accept_event(&state->cover,
                                                  state->cover_token,
                                                  state->cover_url, event);
            }
            else
            {
                state->cover_failed =
                    state->cover_attempts >= MUSIC_PLAYER_MAX_ATTEMPTS;
            }
        }
    }
    _player_update(state);
}

static void _player_play_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = music_service_play_pause();
    if (state != NULL && result != ESP_OK)
    {
        _player_set_guidance(state,
                             result == ESP_ERR_TIMEOUT ?
                             "播放请求超时，请重试" :
                             "当前无法切换播放状态");
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
        _player_set_guidance(state,
                             had_status &&
                             !music_ui_can_step(&before, false) ?
                             "已经是队列开头" :
                             "暂时无法切换歌曲，请重试");
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
        _player_set_guidance(state,
                             had_status &&
                             !music_ui_can_step(&before, true) ?
                             "已经是队列末尾" :
                             "暂时无法切换歌曲，请重试");
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
    const esp_err_t result = music_service_set_mode(next);
    if (result != ESP_OK)
    {
        _player_set_guidance(state, "播放模式切换失败");
        return;
    }
    app_ui_button_set_text(state->mode_button, music_ui_mode_text(next));
    _player_set_guidance(state, "");
}

static void _player_stop_clicked(lv_event_t *event)
{
    music_player_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = music_service_stop();
    if (state != NULL && result != ESP_OK)
    {
        _player_set_guidance(state, "停止播放失败，请重试");
        return;
    }
    if (state != NULL)
    {
        _player_update(state);
        _player_set_guidance(state,
                             "播放已停止，返回歌单可选择歌曲");
    }
}

static void _player_lyrics_clicked(lv_event_t *event)
{
    (void)event;
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LYRICS,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    (void)app_manager_navigate_async(&request, NULL, NULL);
}

static lv_obj_t *_player_icon_control(lv_obj_t *parent, uint32_t image_id,
                                      const char *fallback,
                                      lv_event_cb_t callback,
                                      music_player_state_t *state,
                                      int32_t width, uint32_t icon_color)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    lv_obj_set_size(button, width, MUSIC_UI_CONTROL_SIZE);
    lv_obj_set_style_radius(button, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *glyph = app_ui_image(button, image_id, fallback, 28);
    if (glyph != NULL)
    {
        app_ui_icon_set_color(glyph, icon_color);
        lv_obj_center(glyph);
    }
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
    return button;
}

static lv_obj_t *_player_text_control(lv_obj_t *parent, const char *text,
                                      lv_event_cb_t callback,
                                      music_player_state_t *state,
                                      int32_t width)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    lv_obj_set_size(button, width, MUSIC_UI_CONTROL_SIZE);
    lv_obj_set_style_radius(button, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, state);
    return button;
}

static void _player_set_control_icon(lv_obj_t *button, uint32_t image_id,
                                     const char *fallback, uint32_t color)
{
    if (button == NULL)
    {
        return;
    }
    lv_obj_t *old_icon = lv_obj_get_child(button, 0);
    if (old_icon != NULL)
    {
        lv_obj_delete(old_icon);
    }
    lv_obj_t *icon = app_ui_image(button, image_id, fallback, 28);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, color);
        lv_obj_center(icon);
    }
}

static void _player_mount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "正在播放", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);

    state->cover_area = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->cover_area);
    lv_obj_set_width(state->cover_area, LV_PCT(100));
    lv_obj_set_height(state->cover_area, MUSIC_PLAYER_COVER_SIZE);
    lv_obj_set_flex_flow(state->cover_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->cover_area, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(state->cover_area, LV_DIR_NONE);
    app_ui_make_passive(state->cover_area, false);
    music_ui_cover_create(&state->cover, state->cover_area, LV_SYMBOL_AUDIO,
                          MUSIC_PLAYER_COVER_SIZE);

    lv_obj_t *heading = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(heading, 3, 0);
    app_ui_make_passive(heading, false);
    state->title_label = app_ui_add_section(heading, "暂无播放");
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_top(state->title_label, 0, 0);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->artist_label = app_ui_add_body_label(heading, "从歌单选择歌曲");
    lv_obj_set_style_text_font(state->artist_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    lv_obj_t *context_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(context_row);
    lv_obj_set_width(context_row, LV_PCT(100));
    lv_obj_set_height(context_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(context_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(context_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(context_row, false);
    state->queue_label = app_ui_add_body_label(context_row, "QQ 音乐");
    lv_obj_set_width(state->queue_label, 0);
    lv_obj_set_flex_grow(state->queue_label, 1);
    lv_obj_set_style_text_font(state->queue_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->queue_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    state->state_pill = app_ui_add_pill(context_row, "空闲",
                                        APP_UI_STATUS_NEUTRAL);

    lv_obj_t *timeline = lv_obj_create(state->page.content);
    state->timeline = timeline;
    lv_obj_remove_style_all(timeline);
    lv_obj_set_width(timeline, LV_PCT(100));
    lv_obj_set_height(timeline, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(timeline, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(timeline, 3, 0);
    app_ui_make_passive(timeline, false);
    state->progress_bar = lv_bar_create(timeline);
    lv_obj_set_width(state->progress_bar, LV_PCT(100));
    lv_obj_set_height(state->progress_bar, 6);
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
    state->position_label = app_ui_add_body_label(time_row, "0:00");
    state->duration_label = app_ui_add_body_label(time_row, "0:00");
    lv_obj_set_width(state->position_label, LV_SIZE_CONTENT);
    lv_obj_set_width(state->duration_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(state->position_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_font(state->duration_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->position_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_color(state->duration_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);

    lv_obj_t *transport = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(transport);
    lv_obj_set_width(transport, LV_PCT(100));
    lv_obj_set_height(transport, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(transport, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transport, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(transport, false);
    state->previous_button = _player_icon_control(
                                 transport, APP_IMAGE_MUSIC_PREV,
                                 LV_SYMBOL_PREV, _player_prev_clicked, state,
                                 MUSIC_UI_CONTROL_SIZE, APP_UI_COLOR_TEXT);
    state->play_button = _player_icon_control(
                             transport, APP_IMAGE_MUSIC_PLAY, LV_SYMBOL_PLAY,
                             _player_play_clicked, state, 72,
                             APP_UI_COLOR_BACKGROUND);
    state->next_button = _player_icon_control(
                             transport, APP_IMAGE_MUSIC_NEXT, LV_SYMBOL_NEXT,
                             _player_next_clicked, state,
                             MUSIC_UI_CONTROL_SIZE, APP_UI_COLOR_TEXT);
    if (state->play_button != NULL)
    {
        lv_obj_set_style_bg_color(state->play_button,
                                  lv_color_hex(APP_UI_COLOR_RAIN), 0);
    }

    lv_obj_t *secondary = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(secondary);
    lv_obj_set_width(secondary, LV_PCT(100));
    lv_obj_set_height(secondary, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(secondary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(secondary, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(secondary, false);
    state->mode_button = _player_text_control(secondary, "顺序播放",
        _player_mode_clicked, state, 86);
    state->lyrics_button = _player_text_control(secondary, "歌词",
        _player_lyrics_clicked, state, 68);
    state->stop_button = _player_text_control(secondary, "停止",
        _player_stop_clicked, state, 68);
    state->guidance_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->guidance_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->guidance_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->guidance_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->guidance_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_hidden(state->guidance_label, true);
}

static void _player_resume(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _player_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_player_timer_cb, 500U, state);
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
    music_ui_cover_release(&state->cover);
    state->cover_requested = false;
    state->cover_attempts = 0U;
    state->cover_wait_ticks = 0U;
    return ESP_OK;
}

static void _player_unmount(const app_manager_page_context_t *context)
{
    music_player_state_t *state = context->state;
    music_ui_cover_release(&state->cover);
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
