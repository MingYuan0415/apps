#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_SIDEBAR_MAX_REFS 36U
#define MUSIC_SIDEBAR_IDENTITY_HEIGHT 72
#define MUSIC_SIDEBAR_RADIO_HEIGHT 128
#define MUSIC_SIDEBAR_SHORTCUT_HEIGHT 128

typedef struct music_sidebar_state
{
    app_ui_page_t page;
    lv_obj_t *scroll;
    lv_obj_t *account_label;
    lv_obj_t *status_label;
    lv_obj_t *confirm_sheet;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_playlist_refs_t refs;
    uint64_t shown_generation;
    uint32_t refresh_request_ms;
    uint8_t last_state;
    uint8_t last_failure;
    bool dirty;
    bool logged_in;
    bool logout_pending;
} music_sidebar_state_t;

_Static_assert(sizeof(music_sidebar_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music sidebar state exceeds the page arena");

static void _sidebar_update_service(music_sidebar_state_t *state);
static void _sidebar_refresh(music_sidebar_state_t *state);

static void _sidebar_close_clicked(lv_event_t *event)
{
    (void)event;
    app_ui_request_back();
}

static void _sidebar_login_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_login();
}

static void _sidebar_retry_clicked(lv_event_t *event)
{
    music_sidebar_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    (void)music_service_retry();
    (void)music_service_refresh_home(false);
    state->dirty = true;
    _sidebar_update_service(state);
    _sidebar_refresh(state);
}

static lv_obj_t *_sidebar_card_label(lv_obj_t *parent, const char *text,
                                     app_theme_font_id_t font,
                                     uint32_t color, bool one_line)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, one_line ? LV_LABEL_LONG_DOT :
                           LV_LABEL_LONG_WRAP);
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    if (one_line)
    {
        lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static music_playlist_ref_t *_sidebar_store_ref(music_sidebar_state_t *state,
        const music_playlist_t *playlist)
{
    if (state == NULL || playlist == NULL ||
            !music_ui_playlist_ref_valid(&playlist->ref) ||
            state->refs.count >= MUSIC_SIDEBAR_MAX_REFS)
    {
        return NULL;
    }
    music_playlist_ref_t *ref = &state->refs.items[state->refs.count++];
    *ref = playlist->ref;
    return ref;
}

static void _sidebar_playlist_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    music_playlist_ref_t *ref = lv_event_get_user_data(event);
    if (music_ui_playlist_ref_valid(ref))
    {
        (void)music_ui_open_playlist(ref);
    }
}

static void _sidebar_radio_clicked(lv_event_t *event)
{
    music_sidebar_state_t *state = lv_event_get_user_data(event);
    music_playlist_ref_t *ref = lv_obj_get_user_data(
                                    lv_event_get_target_obj(event));
    if (state == NULL || !music_ui_playlist_ref_valid(ref))
    {
        return;
    }
    music_player_status_t before;
    const bool had_status =
        music_service_get_player_status(&before) == ESP_OK;
    if (music_service_set_mode(MUSIC_PLAY_MODE_SHUFFLE) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "模式切换失败",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    if (music_ui_play_playlist(ref, 0U) != ESP_OK)
    {
        if (had_status)
        {
            (void)music_service_set_mode(before.mode);
        }
        app_ui_set_status_text(state->status_label, "电台启动失败",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
    }
}

static void _sidebar_shortcut_clicked(lv_event_t *event)
{
    (void)event;
    music_playlist_ref_t *ref = lv_event_get_user_data(event);
    if (music_ui_playlist_ref_valid(ref))
    {
        (void)music_ui_open_playlist(ref);
    }
}

static void _sidebar_logout_cancel(lv_event_t *event)
{
    music_sidebar_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _sidebar_logout_confirm(lv_event_t *event)
{
    music_sidebar_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    const esp_err_t result = music_service_logout();
    state->logout_pending = result == ESP_OK;
    app_ui_set_status_text(state->status_label,
                           result == ESP_OK ? "正在退出…" : "退出失败",
                           result == ESP_OK ? APP_UI_STATUS_ACCENT :
                           APP_UI_STATUS_WARNING);
    lv_obj_set_hidden(state->status_label, false);
    state->dirty = true;
}

static void _sidebar_logout_clicked(lv_event_t *event)
{
    music_sidebar_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->confirm_sheet != NULL)
    {
        return;
    }
    state->confirm_sheet = app_ui_sheet_open(
                               state->page.root, "退出 QQ 音乐账号",
                               "本机保存的登录信息将被清除，歌单浏览需要重新登录。");
    if (state->confirm_sheet == NULL)
    {
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "退出登录", true,
                                  _sidebar_logout_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _sidebar_logout_cancel, state);
}

static lv_obj_t *_sidebar_card(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback, const char *title,
                               const char *subtitle, int32_t height,
                               int32_t icon_size,
                               app_theme_font_id_t title_font,
                               lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *card = lv_button_create(parent);
    if (card == NULL)
    {
        return NULL;
    }
    app_ui_click_only(card);
    lv_obj_set_width(card, 0);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, height);
    music_ui_flat_button_reset(card, APP_UI_RADIUS_CARD);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_t *icon = music_ui_image(card, image_id, fallback, icon_size);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, APP_UI_COLOR_RAIN);
    }
    (void)_sidebar_card_label(card, title, title_font,
                              APP_UI_COLOR_TEXT, false);
    if (subtitle != NULL && subtitle[0] != '\0')
    {
        (void)_sidebar_card_label(card, subtitle, APP_THEME_FONT_SMALL,
                                  APP_UI_COLOR_MUTED, true);
    }
    if (callback != NULL)
    {
        lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, user_data);
    }
    return card;
}

static void _sidebar_add_playlist_section(music_sidebar_state_t *state,
        const char *title, const music_playlist_t *playlists, size_t count,
        bool collected_ok)
{
    if (!collected_ok)
    {
        return;
    }
    lv_obj_t *section = lv_obj_create(state->scroll);
    lv_obj_remove_style_all(section);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(section, false);
    lv_obj_t *heading = app_ui_add_section(section, title);
    lv_obj_set_style_text_font(heading, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_obj_set_style_pad_top(heading, 10, 0);
    if (count == 0U)
    {
        lv_obj_t *notice = app_ui_add_body_label(section, "暂无歌单");
        lv_obj_set_width(notice, LV_PCT(100));
        lv_obj_set_style_text_font(notice, app_ui_font(APP_THEME_FONT_SMALL),
                                   0);
        return;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        music_playlist_ref_t *ref = _sidebar_store_ref(state,
            &playlists[index]);
        char subtitle[96];
        (void)snprintf(subtitle, sizeof(subtitle), "%s · %u 首",
                       playlists[index].creator[0] == '\0' ? "QQ 音乐" :
                       playlists[index].creator,
                       (unsigned)playlists[index].track_count);
        (void)music_ui_add_nav_row(section, APP_IMAGE_MUSIC_PLAYLIST,
                                   LV_SYMBOL_AUDIO, playlists[index].title,
                                   subtitle, _sidebar_playlist_clicked,
                                   ref, false);
    }
}

static void _sidebar_rebuild(music_sidebar_state_t *state,
                             const music_home_snapshot_t *home)
{
    if (state->scroll == NULL)
    {
        return;
    }
    const int32_t scroll_y = lv_obj_get_scroll_y(state->scroll);
    state->refs.count = 0U;
    lv_obj_clean(state->scroll);
    if (home == NULL)
    {
        if (state->logged_in)
        {
            (void)app_ui_add_empty_state(state->scroll, "正在同步歌单…",
                                         "登录后同步自建歌单、收藏与每日推荐");
        }
        else if (state->last_state != MUSIC_SERVICE_STATE_ERROR)
        {
            lv_obj_t *login_card = _sidebar_card(
                                       state->scroll, APP_IMAGE_MUSIC_ICON,
                                       LV_SYMBOL_AUDIO, "扫码登录",
                                       "登录后同步歌单", 128, 32,
                                       APP_THEME_FONT_HEAD,
                                       _sidebar_login_clicked, state);
            if (login_card != NULL)
            {
                lv_obj_set_width(login_card, LV_PCT(100));
                lv_obj_set_flex_grow(login_card, 0);
            }
            lv_obj_t *benefit = app_ui_add_body_label(
                                    state->scroll,
                                    "登录后同步歌单、收藏与每日推荐");
            lv_obj_set_width(benefit, LV_PCT(100));
            lv_obj_set_style_text_font(benefit,
                                       app_ui_font(APP_THEME_FONT_SMALL), 0);
            lv_obj_set_style_text_color(benefit,
                                        lv_color_hex(APP_UI_COLOR_MUTED), 0);
            lv_obj_set_style_text_align(benefit, LV_TEXT_ALIGN_CENTER, 0);
        }
        else
        {
            (void)music_ui_add_nav_row(state->scroll, APP_IMAGE_UI_REFRESH,
                                       LV_SYMBOL_REFRESH, "重新检查",
                                       "音乐服务暂不可用",
                                       _sidebar_retry_clicked, state, false);
        }
        lv_obj_scroll_to_y(state->scroll, scroll_y, LV_ANIM_OFF);
        return;
    }

    const music_playlist_t *radio = music_ui_radio_playlist(home);
    const music_playlist_t *daily = home != NULL &&
                                    home->recommended_count > 0U ?
                                    &home->recommended[0] : NULL;
    const music_playlist_t *liked = music_ui_liked_playlist(home);
    music_playlist_ref_t *radio_ref = _sidebar_store_ref(state, radio);
    music_playlist_ref_t *daily_ref = _sidebar_store_ref(state, daily);
    music_playlist_ref_t *liked_ref = _sidebar_store_ref(state, liked);

    lv_obj_t *radio_card = _sidebar_card(
                               state->scroll, APP_IMAGE_MUSIC_RADIO,
                               LV_SYMBOL_AUDIO, "个性电台",
                               "推荐随机播放", MUSIC_SIDEBAR_RADIO_HEIGHT, 28,
                               APP_THEME_FONT_HEAD,
                               radio_ref != NULL ? _sidebar_radio_clicked :
                               NULL, state);
    if (radio_card != NULL)
    {
        lv_obj_set_width(radio_card, LV_PCT(100));
        lv_obj_set_flex_grow(radio_card, 0);
        lv_obj_set_user_data(radio_card, radio_ref);
    }
    lv_obj_t *shortcuts = lv_obj_create(state->scroll);
    lv_obj_remove_style_all(shortcuts);
    lv_obj_set_width(shortcuts, LV_PCT(100));
    lv_obj_set_height(shortcuts, MUSIC_SIDEBAR_SHORTCUT_HEIGHT);
    lv_obj_set_flex_flow(shortcuts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(shortcuts, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(shortcuts, false);
    lv_obj_t *daily_card = _sidebar_card(
                               shortcuts, APP_IMAGE_MUSIC_DAILY,
                               LV_SYMBOL_IMAGE, "每日推荐",
                               daily != NULL ? daily->title : "暂无推荐",
                               MUSIC_SIDEBAR_SHORTCUT_HEIGHT, 30,
                               APP_THEME_FONT_HEAD,
                               daily_ref != NULL ? _sidebar_shortcut_clicked :
                               NULL, daily_ref);
    lv_obj_t *liked_card = _sidebar_card(
                               shortcuts, APP_IMAGE_MUSIC_HEART,
                               LV_SYMBOL_AUDIO, "喜欢",
                               liked != NULL ? liked->title : "暂无喜欢的歌曲",
                               MUSIC_SIDEBAR_SHORTCUT_HEIGHT, 30,
                               APP_THEME_FONT_HEAD,
                               liked_ref != NULL ? _sidebar_shortcut_clicked :
                               NULL, liked_ref);
    (void)daily_card;
    (void)liked_card;

    if (home != NULL)
    {
        _sidebar_add_playlist_section(state, "自建歌单", home->my_created,
                                      home->my_created_count, true);
        _sidebar_add_playlist_section(state, "收藏歌单", home->my_collected,
                                      home->my_collected_count,
                                      home->collected_ok);
    }

    if (state->last_state == MUSIC_SERVICE_STATE_READY && state->last_failure ==
            MUSIC_SERVICE_FAILURE_NONE)
    {
        (void)music_ui_add_nav_row(state->scroll, APP_IMAGE_UI_POWER,
                                   LV_SYMBOL_POWER, "退出登录",
                                   "清除本机登录状态",
                                   _sidebar_logout_clicked, state, true);
    }
    else
    {
        (void)music_ui_add_nav_row(state->scroll, APP_IMAGE_MUSIC_ICON,
                                   LV_SYMBOL_AUDIO, "扫码登录",
                                   "登录后同步收藏与歌单",
                                   _sidebar_login_clicked, state, false);
    }
    lv_obj_scroll_to_y(state->scroll, scroll_y, LV_ANIM_OFF);
}

static void _sidebar_update_service(music_sidebar_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        app_ui_label_set_text_if(state->account_label, "未登录");
        app_ui_set_status_text(state->status_label, "音乐服务暂不可用",
                               APP_UI_STATUS_ERROR);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    if (status.state == MUSIC_SERVICE_STATE_LOGGED_OUT &&
            state->logout_pending)
    {
        state->logout_pending = false;
        (void)music_ui_reset_to_login();
        return;
    }
    if (status.state == MUSIC_SERVICE_STATE_ERROR)
    {
        app_ui_label_set_text_if(state->account_label, "未登录");
        app_ui_set_status_text(state->status_label, "音乐服务暂不可用",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        state->logged_in = false;
        const bool changed = status.state != state->last_state ||
                             status.failure != state->last_failure;
        state->last_state = status.state;
        state->last_failure = status.failure;
        if (changed)
        {
            state->dirty = true;
        }
        return;
    }
    state->logged_in = status.logged_in;
    char account[96];
    if (status.logged_in && status.user_name[0] != '\0')
    {
        (void)snprintf(account, sizeof(account), "Hi, %s", status.user_name);
    }
    else if (status.logged_in)
    {
        (void)snprintf(account, sizeof(account), "已登录 QQ 音乐");
    }
    else
    {
        (void)snprintf(account, sizeof(account), "未登录");
    }
    if (account[0] != '\0')
    {
        app_ui_label_set_text_if(state->account_label, account);
    }
    const bool warning = status.failure != MUSIC_SERVICE_FAILURE_NONE ||
                         status.state == MUSIC_SERVICE_STATE_ERROR;
    app_ui_set_status_text(state->status_label, music_ui_state_text(&status),
                           warning ? APP_UI_STATUS_WARNING :
                           status.state == MUSIC_SERVICE_STATE_READY ?
                           APP_UI_STATUS_SUCCESS : APP_UI_STATUS_NEUTRAL);
    char detail[96];
    if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        (void)snprintf(detail, sizeof(detail), "%s",
                       music_ui_failure_text(status.failure));
    }
    else if (!status.network_ready)
    {
        (void)snprintf(detail, sizeof(detail), "等待网络连接");
    }
    else if (!status.time_ready)
    {
        (void)snprintf(detail, sizeof(detail), "等待时间同步");
    }
    else
    {
        (void)snprintf(detail, sizeof(detail), "我的音乐");
    }
    if (status.failure != MUSIC_SERVICE_FAILURE_NONE ||
            !status.network_ready || !status.time_ready)
    {
        app_ui_label_set_text_if(state->status_label, detail);
    }
    lv_obj_set_hidden(state->status_label,
                      status.failure == MUSIC_SERVICE_FAILURE_NONE &&
                      status.network_ready && status.time_ready);
    const bool changed = status.state != state->last_state ||
                         status.failure != state->last_failure;
    state->last_state = status.state;
    state->last_failure = status.failure;
    if (changed)
    {
        state->dirty = true;
    }
}

static void _sidebar_refresh(music_sidebar_state_t *state)
{
    const music_home_snapshot_t *home = NULL;
    const esp_err_t result = music_service_home_acquire(&home);
    if (result == ESP_OK && home != NULL)
    {
        if (home->generation != state->shown_generation || state->dirty)
        {
            _sidebar_rebuild(state, home);
            state->shown_generation = home->generation;
        }
        state->dirty = false;
        music_service_home_release(home);
    }
    else if (result == ESP_ERR_NOT_FOUND)
    {
        if (state->shown_generation != 0U || state->dirty)
        {
            _sidebar_rebuild(state, NULL);
            state->shown_generation = 0U;
        }
        state->dirty = false;
    }
    else
    {
        app_ui_set_status_text(state->status_label, "歌单加载失败",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
    }
}

static void _sidebar_timer_cb(lv_timer_t *timer)
{
    music_sidebar_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    _sidebar_update_service(state);
    _sidebar_refresh(state);
    if (state->shown_generation == 0U && state->logged_in &&
            lv_tick_get() - state->refresh_request_ms >= 5000U)
    {
        state->refresh_request_ms = lv_tick_get();
        (void)music_service_refresh_home(false);
    }
}

static void _sidebar_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                              const void *payload, size_t payload_size,
                              void *user_data)
{
    (void)msg_id;
    (void)payload;
    (void)payload_size;
    music_sidebar_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS ||
            sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_HOME)
    {
        state->dirty = true;
        _sidebar_update_service(state);
        _sidebar_refresh(state);
    }
}

static void _sidebar_mount(const app_manager_page_context_t *context)
{
    music_sidebar_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->dirty = true;
    app_ui_page_create_home(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 8, 0);
    lv_obj_set_style_pad_row(state->page.content, 0, 0);
    lv_obj_set_style_pad_bottom(state->page.content, 0, 0);

    lv_obj_t *identity = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(identity);
    lv_obj_set_width(identity, LV_PCT(100));
    lv_obj_set_height(identity, MUSIC_SIDEBAR_IDENTITY_HEIGHT);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(identity, 10, 0);
    app_ui_make_passive(identity, false);
    lv_obj_t *icon = music_ui_image(identity, APP_IMAGE_MUSIC_ICON,
                                    LV_SYMBOL_AUDIO, 40);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, APP_UI_COLOR_RAIN);
    }
    lv_obj_t *text = lv_obj_create(identity);
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 0, 0);
    app_ui_make_passive(text, false);
    lv_obj_t *title = app_ui_add_section(text, "QQ 音乐");
    lv_obj_set_style_pad_top(title, 0, 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_RAIN), 0);
    state->account_label = app_ui_add_body_label(text, "正在连接…");
    lv_obj_set_style_text_font(state->account_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    (void)music_ui_plain_icon_button(identity, APP_IMAGE_UI_CLOSE,
                                     LV_SYMBOL_CLOSE, MUSIC_UI_CONTROL_SIZE,
                                     26, APP_UI_COLOR_MUTED,
                                     _sidebar_close_clicked, state);

    state->scroll = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->scroll);
    lv_obj_set_width(state->scroll, LV_PCT(100));
    lv_obj_set_height(state->scroll, 0);
    lv_obj_set_flex_grow(state->scroll, 1);
    lv_obj_set_flex_flow(state->scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->scroll, APP_UI_GAP, 0);
    lv_obj_set_scroll_dir(state->scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state->scroll, LV_SCROLLBAR_MODE_AUTO);
    app_ui_make_passive(state->scroll, true);

    lv_obj_t *footer = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(footer);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, 28);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(footer, false);
    state->status_label = app_ui_add_body_label(footer, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(state->status_label, true);
    _sidebar_rebuild(state, NULL);
}

static void _sidebar_resume(const app_manager_page_context_t *context)
{
    music_sidebar_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _sidebar_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_sidebar_timer_cb, 1000U, state);
    _sidebar_update_service(state);
    _sidebar_refresh(state);
    state->refresh_request_ms = lv_tick_get();
    (void)music_service_refresh_home(false);
}

static esp_err_t _sidebar_pause(const app_manager_page_context_t *context)
{
    music_sidebar_state_t *state = context->state;
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
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    return ESP_OK;
}

static void _sidebar_unmount(const app_manager_page_context_t *context)
{
    music_sidebar_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_sidebar_ops =
{
    .mount = _sidebar_mount,
    .resume = _sidebar_resume,
    .pause = _sidebar_pause,
    .unmount = _sidebar_unmount,
};

const app_manager_page_definition_t music_sidebar_page_definition =
{
    .ops = &s_sidebar_ops,
    .memory_size = sizeof(music_sidebar_state_t),
};
