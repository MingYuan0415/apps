#define DBG_TAG "music_root"
#include "mt_log.h"

#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_ROOT_MAX_REFS 36U
#define MUSIC_ROOT_VISIBLE_PLAYLISTS 3U
#define MUSIC_ROOT_RECENT_PLAYLISTS 2U

typedef struct music_root_playlist_refs
{
    music_playlist_ref_t items[MUSIC_ROOT_MAX_REFS];
    size_t count;
} music_root_playlist_refs_t;

typedef struct music_root_state
{
    app_ui_page_t page;
    lv_obj_t *body;
    lv_obj_t *account_label;
    lv_obj_t *account_button;
    lv_obj_t *service_pill;
    lv_obj_t *recommendation_area;
    lv_obj_t *recommendation_cover;
    lv_obj_t *recommendation_title;
    lv_obj_t *recommendation_meta;
    lv_obj_t *recommendation_button;
    lv_obj_t *personal_group;
    lv_obj_t *favorite_group;
    lv_obj_t *recommended_group;
    lv_obj_t *loading_label;
    lv_obj_t *mini_player;
    lv_obj_t *mini_cover_placeholder;
    lv_obj_t *mini_title;
    lv_obj_t *mini_subtitle;
    lv_obj_t *mini_play_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_root_playlist_refs_t refs;
    music_ui_cover_t lead_cover;
    lv_image_dsc_t lead_descriptor;
    uint32_t lead_token;
    char lead_url[MUSIC_SERVICE_URL_BYTES];
    uint32_t lead_requested_at;
    uint64_t shown_generation;
    uint16_t lead_wait_ticks;
    uint8_t lead_attempts;
    bool lead_requested;
    bool lead_failed;
    bool dirty;
} music_root_state_t;

_Static_assert(sizeof(music_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music root state exceeds the page arena");

static void _root_playlist_clicked(lv_event_t *event);
static void _root_open_player_clicked(lv_event_t *event);
static void _root_login_clicked(lv_event_t *event);
static void _root_refresh_clicked(lv_event_t *event);
static void _root_recommendation_clicked(lv_event_t *event);
static void _root_open_player_clicked(lv_event_t *event);

static uint32_t _root_cover_token(const char *url)
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

static void _root_set_hidden(lv_obj_t *object, bool hidden)
{
    if (object != NULL)
    {
        lv_obj_set_hidden(object, hidden);
    }
}

static music_playlist_ref_t *_root_store_ref(music_root_state_t *state,
        const music_playlist_t *playlist)
{
    if (state->refs.count >= MUSIC_ROOT_MAX_REFS)
    {
        return NULL;
    }
    music_playlist_ref_t *ref = &state->refs.items[state->refs.count++];
    *ref = playlist->ref;
    return ref;
}

static void _root_add_group_row(music_root_state_t *state, lv_obj_t *group,
                                const music_playlist_t *playlist)
{
    music_playlist_ref_t *ref = _root_store_ref(state, playlist);
    if (group == NULL || ref == NULL)
    {
        return;
    }
    char subtitle[96];
    (void)snprintf(subtitle, sizeof(subtitle), "%s · %u 首",
                   playlist->creator[0] == '\0' ? "QQ 音乐" :
                   playlist->creator,
                   (unsigned)playlist->track_count);
    lv_obj_t *row = app_ui_group_add_nav(group, LV_SYMBOL_AUDIO,
                                         playlist->title, subtitle,
                                         _root_playlist_clicked, ref);
    if (row != NULL)
    {
        lv_obj_set_user_data(row, ref);
    }
}

static void _root_build_recommendation(music_root_state_t *state,
                                       const music_home_snapshot_t *home)
{
    if (state->recommendation_area == NULL)
    {
        return;
    }
    music_ui_cover_release(&state->lead_cover);
    lv_obj_clean(state->recommendation_area);
    state->recommendation_cover = NULL;
    state->recommendation_title = NULL;
    state->recommendation_meta = NULL;
    state->recommendation_button = NULL;
    memset(&state->lead_cover, 0, sizeof(state->lead_cover));
    state->lead_url[0] = '\0';
    state->lead_requested = false;
    state->lead_failed = false;
    state->lead_attempts = 0U;

    lv_obj_t *card = lv_button_create(state->recommendation_area);
    if (card == NULL)
    {
        return;
    }
    app_ui_click_only(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, MUSIC_UI_HERO_COVER + 2 * APP_UI_CARD_PAD);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_column(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(card, _root_recommendation_clicked,
                        LV_EVENT_CLICKED, state);

    music_ui_cover_create(&state->lead_cover, card, LV_SYMBOL_AUDIO,
                          MUSIC_UI_HERO_COVER);
    state->recommendation_cover = state->lead_cover.frame;
    lv_obj_t *details = lv_obj_create(card);
    if (details == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(details);
    lv_obj_set_width(details, 0);
    lv_obj_set_flex_grow(details, 1);
    lv_obj_set_height(details, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details, 4, 0);
    app_ui_make_passive(details, false);

    if (home->login_required)
    {
        state->recommendation_title = app_ui_add_section(details, "登录后更懂你");
        app_ui_label_set_text_if(state->recommendation_title, "每天发现新音乐");
        lv_obj_set_width(state->recommendation_title, LV_PCT(100));
        lv_obj_set_style_text_color(state->recommendation_title,
                                    lv_color_hex(APP_UI_COLOR_TEXT), 0);
        state->recommendation_meta = app_ui_add_body_label(details,
            "登录后同步收藏与个性化推荐");
        state->recommendation_button = app_ui_add_pill(details, "去登录",
            APP_UI_STATUS_ACCENT);
        app_ui_make_passive(state->recommendation_button, false);
        return;
    }
    if (!home->recommended_ok)
    {
        state->recommendation_title = app_ui_add_section(details, "推荐暂不可用");
        app_ui_label_set_text_if(state->recommendation_title, "稍后刷新");
        lv_obj_set_width(state->recommendation_title, LV_PCT(100));
        lv_obj_set_style_text_color(state->recommendation_title,
                                    lv_color_hex(APP_UI_COLOR_TEXT), 0);
        state->recommendation_meta = app_ui_add_body_label(details,
            "网络波动时不会清空已有歌单");
        return;
    }
    if (home->recommended_count == 0U)
    {
        state->recommendation_title = app_ui_add_section(details, "今日推荐");
        app_ui_label_set_text_if(state->recommendation_title, "暂无推荐");
        lv_obj_set_width(state->recommendation_title, LV_PCT(100));
        lv_obj_set_style_text_color(state->recommendation_title,
                                    lv_color_hex(APP_UI_COLOR_TEXT), 0);
        state->recommendation_meta = app_ui_add_body_label(details,
            "去收藏歌单里发现喜欢的音乐");
        return;
    }
    const music_playlist_t *playlist = &home->recommended[0];
    music_playlist_ref_t *ref = _root_store_ref(state, playlist);
    lv_obj_set_user_data(card, ref);
    (void)app_ui_add_section(details, "今日推荐");
    state->recommendation_title = app_ui_add_section(details, playlist->title);
    lv_obj_set_width(state->recommendation_title, LV_PCT(100));
    lv_obj_set_style_pad_top(state->recommendation_title, 0, 0);
    lv_obj_set_style_text_color(state->recommendation_title,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    char meta[96];
    (void)snprintf(meta, sizeof(meta), "%s · %u 首",
                   playlist->creator[0] == '\0' ? "QQ 音乐" : playlist->creator,
                   (unsigned)playlist->track_count);
    state->recommendation_meta = app_ui_add_body_label(details, meta);
    state->recommendation_button = app_ui_add_pill(details, "打开歌单",
        APP_UI_STATUS_ACCENT);
    app_ui_make_passive(state->recommendation_button, false);
    if (playlist->cover_url[0] != '\0' &&
            strlen(playlist->cover_url) < sizeof(state->lead_url))
    {
        (void)snprintf(state->lead_url, sizeof(state->lead_url), "%s",
                       playlist->cover_url);
        state->lead_token = _root_cover_token(state->lead_url);
    }
}

static void _root_build_group(lv_obj_t *group, const char *title,
                              const music_playlist_t *playlists,
                              size_t count, music_root_state_t *state)
{
    if (group == NULL)
    {
        return;
    }
    lv_obj_clean(group);
    lv_obj_t *section = lv_obj_get_parent(group);
    if (count == 0U)
    {
        lv_obj_set_hidden(group, true);
        if (section != NULL)
        {
            lv_obj_set_hidden(section, true);
        }
        return;
    }
    lv_obj_set_hidden(group, false);
    if (section != NULL)
    {
        lv_obj_set_hidden(section, false);
    }
    const size_t visible = count < MUSIC_ROOT_RECENT_PLAYLISTS ?
                           count : MUSIC_ROOT_RECENT_PLAYLISTS;
    for (size_t index = 0U; index < visible; ++index)
    {
        _root_add_group_row(state, group, &playlists[index]);
    }
    (void)title;
}

static void _root_build_home(music_root_state_t *state,
                             const music_home_snapshot_t *home)
{
    state->refs.count = 0U;
    _root_build_recommendation(state, home);
    _root_build_group(state->personal_group, "我创建的",
                      home->my_created, home->my_created_count, state);
    _root_build_group(state->favorite_group, "我的收藏",
                      home->my_favorite, home->my_favorite_count, state);
    _root_build_group(state->recommended_group, "推荐歌单",
                      home->recommended_count > 0U ?
                      &home->recommended[1] : home->recommended,
                      home->recommended_count > 0U ?
                      home->recommended_count - 1U : 0U, state);
    state->shown_generation = home->generation;
    _root_set_hidden(state->loading_label, true);
}

static void _root_update_account(music_root_state_t *state,
                                 const music_service_status_t *status)
{
    char account[96];
    if (status->logged_in && status->user_name[0] != '\0')
    {
        (void)snprintf(account, sizeof(account), "Hi, %s", status->user_name);
    }
    else if (status->logged_in)
    {
        (void)snprintf(account, sizeof(account), "已登录 QQ 音乐");
    }
    else
    {
        (void)snprintf(account, sizeof(account), "未登录");
    }
    app_ui_label_set_text_if(state->account_label, account);
}

static void _root_update_service(music_root_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        app_ui_pill_set(state->service_pill, "服务不可用",
                        APP_UI_STATUS_ERROR);
        app_ui_label_set_text_if(state->account_label, "音乐服务不可用");
        return;
    }
    _root_update_account(state, &status);
    const bool warning = status.failure != MUSIC_SERVICE_FAILURE_NONE ||
                         status.state == MUSIC_SERVICE_STATE_ERROR;
    app_ui_pill_set(state->service_pill, music_ui_state_text(&status),
                    warning ? APP_UI_STATUS_WARNING :
                    status.state == MUSIC_SERVICE_STATE_READY ?
                    APP_UI_STATUS_SUCCESS : APP_UI_STATUS_NEUTRAL);
    char subtitle[96];
    if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        (void)snprintf(subtitle, sizeof(subtitle), "%s",
                       music_ui_failure_text(status.failure));
    }
    else if (!status.network_ready)
    {
        (void)snprintf(subtitle, sizeof(subtitle), "等待网络连接");
    }
    else if (!status.time_ready)
    {
        (void)snprintf(subtitle, sizeof(subtitle), "等待时间同步");
    }
    else
    {
        (void)snprintf(subtitle, sizeof(subtitle), "我的音乐");
    }
    app_ui_page_set_subtitle(&state->page, subtitle);
}

static void _root_update_lead_cover(music_root_state_t *state)
{
    if (state->lead_url[0] == '\0' || state->lead_cover.image != NULL ||
            state->lead_failed)
    {
        return;
    }
    if (state->lead_requested)
    {
        if (state->lead_wait_ticks < UINT16_MAX)
        {
            ++state->lead_wait_ticks;
        }
        if (state->lead_wait_ticks <= 20U)
        {
            return;
        }
        state->lead_requested = false;
    }
    if (state->lead_attempts >= 2U)
    {
        state->lead_failed = true;
        return;
    }
    const music_image_t *image = NULL;
    ++state->lead_attempts;
    state->lead_requested = true;
    state->lead_requested_at = lv_tick_get();
    const esp_err_t result = music_service_image_acquire(
                                 state->lead_url, MUSIC_UI_HERO_COVER,
                                 MUSIC_UI_HERO_COVER, state->lead_token, &image);
    if (result == ESP_OK && image != NULL)
    {
        state->lead_requested = false;
        state->lead_attempts = 0U;
        music_ui_cover_set_image(&state->lead_cover, (music_image_t *)image);
    }
    else if (result != ESP_ERR_NOT_FOUND)
    {
        state->lead_requested = false;
        if (state->lead_attempts >= 2U)
        {
            state->lead_failed = true;
        }
    }
}

static void _root_update_mini_player(music_root_state_t *state)
{
    music_player_status_t player;
    if (music_service_get_player_status(&player) != ESP_OK ||
            player.title[0] == '\0')
    {
        _root_set_hidden(state->mini_player, true);
        return;
    }
    _root_set_hidden(state->mini_player, false);
    app_ui_label_set_text_if(state->mini_title, player.title);
    char suffix[96];
    if (player.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        (void)snprintf(suffix, sizeof(suffix), " · %s",
                       music_ui_failure_text(player.failure));
    }
    else if (player.state != MUSIC_PLAYBACK_PLAYING)
    {
        (void)snprintf(suffix, sizeof(suffix), " · %s",
                       music_ui_playback_text(player.state));
    }
    else
    {
        suffix[0] = '\0';
    }
    char subtitle[256];
    (void)snprintf(subtitle, sizeof(subtitle), "%s%s",
                   player.artist[0] == '\0' ? "未知歌手" : player.artist,
                   suffix);
    app_ui_label_set_text_if(state->mini_subtitle, subtitle);
    app_ui_button_set_text(state->mini_play_button,
                           player.state == MUSIC_PLAYBACK_PLAYING ? "暂停" :
                           player.state == MUSIC_PLAYBACK_BUFFERING ? "缓冲" :
                           "播放");
    if (!music_ui_can_toggle_playback(&player))
    {
        lv_obj_add_state(state->mini_play_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->mini_play_button, LV_STATE_DISABLED);
    }
}

static void _root_update_all(music_root_state_t *state)
{
    _root_update_service(state);
    _root_update_lead_cover(state);
    _root_update_mini_player(state);
}

static void _root_refresh_home(music_root_state_t *state)
{
    const music_home_snapshot_t *home = NULL;
    const esp_err_t result = music_service_home_acquire(&home);
    if (result == ESP_OK && home != NULL)
    {
        if (home->generation != state->shown_generation || state->dirty)
        {
            _root_build_home(state, home);
        }
        state->dirty = false;
        music_service_home_release(home);
    }
    else if (result != ESP_ERR_NOT_FOUND)
    {
        app_ui_pill_set(state->service_pill, "歌单加载失败",
                        APP_UI_STATUS_WARNING);
    }
}

static void _root_timer_cb(lv_timer_t *timer)
{
    music_root_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    _root_update_all(state);
    if (state->dirty)
    {
        _root_refresh_home(state);
    }
    else
    {
        music_player_status_t player;
        if (music_service_get_player_status(&player) == ESP_OK &&
                player.generation != 0U)
        {
            _root_update_mini_player(state);
        }
    }
}

static void _root_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                           const void *payload, size_t payload_size,
                           void *user_data)
{
    (void)msg_id;
    music_root_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_IMAGE &&
            payload != NULL && payload_size == sizeof(music_service_event_t))
    {
        const music_service_event_t *event = payload;
        if (event->token == state->lead_token)
        {
            state->lead_requested = false;
            state->lead_wait_ticks = 0U;
            if (event->failure == MUSIC_SERVICE_FAILURE_NONE)
            {
                state->lead_failed = false;
                state->lead_attempts = 0U;
                (void)music_ui_cover_accept_event(&state->lead_cover,
                                                  state->lead_token,
                                                  state->lead_url, event);
            }
            else
            {
                state->lead_failed = state->lead_attempts >= 2U;
            }
        }
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS ||
            sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_HOME ||
            sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_PLAYER)
    {
        state->dirty = true;
        _root_refresh_home(state);
        _root_update_all(state);
    }
}

static void _root_playlist_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    music_playlist_ref_t *ref = lv_event_get_user_data(event);
    if (ref != NULL &&
            music_ui_open_playlist(ref) != ESP_OK)
    {
        LOG_W("music playlist navigation failed");
    }
}

static void _root_recommendation_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    music_playlist_ref_t *ref = lv_obj_get_user_data(
                                    lv_event_get_target_obj(event));
    if (state != NULL && ref != NULL && ref->id != 0U)
    {
        (void)music_ui_open_playlist(ref);
        return;
    }
    _root_login_clicked(event);
}

static void _root_login_clicked(lv_event_t *event)
{
    (void)event;
    const app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LOGIN,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    (void)app_manager_navigate_async(&request, NULL, NULL);
}

static void _root_open_player_clicked(lv_event_t *event)
{
    (void)event;
    (void)music_ui_open_player();
}

static void _root_refresh_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && music_service_refresh_home(true) != ESP_OK)
    {
        app_ui_pill_set(state->service_pill, "刷新失败",
                        APP_UI_STATUS_WARNING);
    }
}

static void _root_mini_play_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    const esp_err_t result = music_service_play_pause();
    if (result != ESP_OK)
    {
        app_ui_pill_set(state->service_pill, "播放操作失败",
                        APP_UI_STATUS_WARNING);
    }
    _root_update_mini_player(state);
}

static void _root_mini_next_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && music_service_next() != ESP_OK)
    {
        app_ui_pill_set(state->service_pill, "没有下一首",
                        APP_UI_STATUS_NEUTRAL);
    }
    if (state != NULL)
    {
        _root_update_mini_player(state);
    }
}

static lv_obj_t *_root_create_group_block(music_root_state_t *state,
        const char *title, lv_obj_t **group_out)
{
    lv_obj_t *section = lv_obj_create(state->body);
    if (section == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(section);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(section, false);
    (void)app_ui_add_section(section, title);
    lv_obj_t *group = app_ui_group_create(section);
    if (group_out != NULL)
    {
        *group_out = group;
    }
    return section;
}

static void _root_mount(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->dirty = true;
    app_ui_page_create(&state->page, "音乐", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);

    lv_obj_t *identity = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(identity);
    lv_obj_set_width(identity, LV_PCT(100));
    lv_obj_set_height(identity, 56);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(identity, APP_UI_GAP_TIGHT, 0);
    app_ui_make_passive(identity, false);
    state->account_button = lv_button_create(identity);
    app_ui_click_only(state->account_button);
    lv_obj_set_width(state->account_button, 0);
    lv_obj_set_flex_grow(state->account_button, 1);
    lv_obj_set_height(state->account_button, LV_PCT(100));
    lv_obj_set_style_radius(state->account_button, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_bg_color(state->account_button,
                              lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(state->account_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->account_button, 0, 0);
    lv_obj_set_style_pad_hor(state->account_button, APP_UI_GAP, 0);
    lv_obj_set_style_pad_column(state->account_button, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(state->account_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->account_button, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(state->account_button, _root_login_clicked,
                        LV_EVENT_CLICKED, state);
    lv_obj_t *account_icon = app_ui_image(state->account_button,
                                          APP_IMAGE_MUSIC_ICON,
                                          LV_SYMBOL_AUDIO, 24);
    if (account_icon != NULL)
    {
        app_ui_icon_set_color(account_icon, APP_UI_COLOR_RAIN);
    }
    state->account_label = app_ui_add_section(state->account_button, "未登录");
    lv_obj_set_width(state->account_label, 0);
    lv_obj_set_flex_grow(state->account_label, 1);
    lv_obj_set_style_pad_top(state->account_label, 0, 0);
    lv_obj_set_style_text_color(state->account_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->service_pill = app_ui_add_pill(identity, "连接中",
                                          APP_UI_STATUS_NEUTRAL);

    state->recommendation_area = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->recommendation_area);
    lv_obj_set_width(state->recommendation_area, LV_PCT(100));
    lv_obj_set_height(state->recommendation_area, LV_SIZE_CONTENT);
    app_ui_make_passive(state->recommendation_area, false);

    state->body = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->body);
    lv_obj_set_width(state->body, LV_PCT(100));
    lv_obj_set_height(state->body, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->body, APP_UI_GAP, 0);
    app_ui_make_passive(state->body, false);

    state->loading_label = app_ui_add_body_label(state->body,
        "正在读取歌单与最近播放");
    lv_obj_set_width(state->loading_label, LV_PCT(100));
    lv_obj_set_style_text_color(state->loading_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);

    lv_obj_t *personal = _root_create_group_block(
                             state, "我创建的", &state->personal_group);
    lv_obj_set_hidden(personal, true);
    lv_obj_t *favorite = _root_create_group_block(
                             state, "我的收藏", &state->favorite_group);
    lv_obj_set_hidden(favorite, true);
    lv_obj_t *recommended = _root_create_group_block(
                                state, "推荐歌单", &state->recommended_group);
    lv_obj_set_hidden(recommended, true);

    lv_obj_t *tools = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    (void)app_ui_button_create(tools, "刷新推荐", _root_refresh_clicked, state);

    state->mini_player = lv_button_create(state->page.content);
    if (state->mini_player != NULL)
    {
        app_ui_click_only(state->mini_player);
        lv_obj_set_width(state->mini_player, LV_PCT(100));
        lv_obj_set_height(state->mini_player, MUSIC_UI_ROW_HEIGHT);
        lv_obj_set_style_radius(state->mini_player, APP_UI_RADIUS_CONTROL, 0);
        lv_obj_set_style_bg_color(state->mini_player,
                                  lv_color_hex(APP_UI_COLOR_SURFACE_HI), 0);
        lv_obj_set_style_bg_opa(state->mini_player, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(state->mini_player, 0, 0);
        lv_obj_set_style_pad_all(state->mini_player, APP_UI_GAP_TIGHT, 0);
        lv_obj_set_style_pad_column(state->mini_player,
                                    APP_UI_GAP_TIGHT, 0);
        lv_obj_set_flex_flow(state->mini_player, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(state->mini_player, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(state->mini_player, _root_open_player_clicked,
                            LV_EVENT_CLICKED, state);
        lv_obj_t *mini_cover = lv_obj_create(state->mini_player);
        lv_obj_remove_style_all(mini_cover);
        lv_obj_set_size(mini_cover, 48, 48);
        lv_obj_set_style_radius(mini_cover, 10, 0);
        lv_obj_set_style_bg_color(mini_cover,
                                  lv_color_hex(APP_UI_COLOR_RAIN_TINT), 0);
        lv_obj_set_style_bg_opa(mini_cover, LV_OPA_COVER, 0);
        app_ui_make_passive(mini_cover, false);
        state->mini_cover_placeholder = app_ui_image(mini_cover,
            APP_IMAGE_MUSIC_ICON,
            LV_SYMBOL_AUDIO, 24);
        if (state->mini_cover_placeholder != NULL)
        {
            app_ui_icon_set_color(state->mini_cover_placeholder,
                                  APP_UI_COLOR_RAIN);
            lv_obj_center(state->mini_cover_placeholder);
        }
        lv_obj_t *mini_text = lv_obj_create(state->mini_player);
        lv_obj_remove_style_all(mini_text);
        lv_obj_set_width(mini_text, 0);
        lv_obj_set_flex_grow(mini_text, 1);
        lv_obj_set_height(mini_text, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(mini_text, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(mini_text, 1, 0);
        app_ui_make_passive(mini_text, false);
        state->mini_title = app_ui_add_section(mini_text, "正在播放");
        lv_obj_set_width(state->mini_title, LV_PCT(100));
        lv_label_set_long_mode(state->mini_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_pad_top(state->mini_title, 0, 0);
        lv_obj_set_style_text_color(state->mini_title,
                                    lv_color_hex(APP_UI_COLOR_TEXT), 0);
        state->mini_subtitle = app_ui_add_body_label(mini_text, "");
        lv_obj_set_width(state->mini_subtitle, LV_PCT(100));
        lv_label_set_long_mode(state->mini_subtitle, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(state->mini_subtitle,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        state->mini_play_button = lv_button_create(state->mini_player);
        if (state->mini_play_button != NULL)
        {
            app_ui_click_only(state->mini_play_button);
            lv_obj_set_size(state->mini_play_button, 44, 44);
            lv_obj_set_style_radius(state->mini_play_button,
                                    LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(state->mini_play_button,
                                      lv_color_hex(APP_UI_COLOR_TRACK), 0);
            lv_obj_set_style_bg_opa(state->mini_play_button, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(state->mini_play_button, 0, 0);
            lv_obj_t *glyph = lv_label_create(state->mini_play_button);
            lv_label_set_text(glyph, LV_SYMBOL_PAUSE);
            lv_obj_center(glyph);
            lv_obj_add_event_cb(state->mini_play_button,
                                _root_mini_play_clicked,
                                LV_EVENT_CLICKED, state);
            lv_obj_set_clickable(state->mini_play_button, true);
        }
        lv_obj_t *next = app_ui_image(state->mini_player,
                                      APP_IMAGE_UI_CHEVRON_RIGHT,
                                      LV_SYMBOL_NEXT, 24);
        if (next != NULL)
        {
            app_ui_icon_set_color(next, APP_UI_COLOR_MUTED);
            lv_obj_set_clickable(next, true);
            lv_obj_add_event_cb(next, _root_mini_next_clicked,
                                LV_EVENT_CLICKED, state);
        }
        _root_set_hidden(state->mini_player, true);
    }
}

static void _root_resume(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _root_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_root_timer_cb, 1000U, state);
    state->dirty = true;
    _root_update_all(state);
    _root_refresh_home(state);
    (void)music_service_refresh_home(false);
}

static esp_err_t _root_pause(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
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

static void _root_unmount(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
    music_ui_cover_release(&state->lead_cover);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_root_ops =
{
    .mount = _root_mount,
    .resume = _root_resume,
    .pause = _root_pause,
    .unmount = _root_unmount,
};

const app_manager_page_definition_t music_root_page_definition =
{
    .ops = &s_root_ops,
    .memory_size = sizeof(music_root_state_t),
};
