#define DBG_TAG "music_root"
#include "mt_log.h"

#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_ROOT_MAX_REFS 36U

typedef struct music_root_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *list;
    lv_obj_t *login_button;
    lv_obj_t *logout_button;
    lv_obj_t *refresh_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_playlist_ref_t refs[MUSIC_ROOT_MAX_REFS];
    size_t ref_count;
    uint64_t shown_generation;
    bool dirty;
} music_root_state_t;

_Static_assert(sizeof(music_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music root state exceeds the page arena");

static void _root_playlist_clicked(lv_event_t *event);

static void _root_update_status(music_root_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "音乐服务不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    char text[160];
    (void)snprintf(text, sizeof(text), "%s%s%s",
                   music_ui_state_text(&status),
                   status.failure == MUSIC_SERVICE_FAILURE_NONE ? "" : " · ",
                   status.failure == MUSIC_SERVICE_FAILURE_NONE ? "" :
                   music_ui_failure_text(status.failure));
    app_ui_set_status_text(state->status_label, text,
                           status.state == MUSIC_SERVICE_STATE_READY ?
                           APP_UI_STATUS_SUCCESS :
                           status.failure == MUSIC_SERVICE_FAILURE_NONE ?
                           APP_UI_STATUS_NEUTRAL : APP_UI_STATUS_WARNING);
    const bool logged_in = status.logged_in;
    if (state->login_button != NULL)
    {
        lv_obj_t *button = logged_in ? state->logout_button :
                           state->login_button;
        lv_obj_t *other = logged_in ? state->login_button :
                          state->logout_button;
        if (button != NULL)
        {
            lv_obj_set_hidden(button, false);
        }
        if (other != NULL)
        {
            lv_obj_set_hidden(other, true);
        }
    }
}

static void _root_add_playlist(music_root_state_t *state,
                               const music_playlist_t *playlist)
{
    if (state->ref_count >= MUSIC_ROOT_MAX_REFS)
    {
        return;
    }
    music_playlist_ref_t *ref = &state->refs[state->ref_count++];
    *ref = playlist->ref;
    (void)music_ui_add_playlist_row(state->list, playlist,
                                    _root_playlist_clicked, ref);
}

static void _root_build_list(music_root_state_t *state,
                             const music_home_snapshot_t *home)
{
    lv_obj_clean(state->list);
    state->ref_count = 0U;
    if (home->my_created_count > 0U)
    {
        (void)app_ui_add_section(state->list, "我的歌单");
        for (size_t index = 0U; index < home->my_created_count; ++index)
        {
            _root_add_playlist(state, &home->my_created[index]);
        }
    }
    if (home->my_favorite_count > 0U)
    {
        (void)app_ui_add_section(state->list, "收藏歌单");
        for (size_t index = 0U; index < home->my_favorite_count; ++index)
        {
            _root_add_playlist(state, &home->my_favorite[index]);
        }
    }
    if (home->recommended_count > 0U)
    {
        (void)app_ui_add_section(state->list, "推荐歌单");
        for (size_t index = 0U; index < home->recommended_count; ++index)
        {
            _root_add_playlist(state, &home->recommended[index]);
        }
    }
    if (home->my_created_count == 0U && home->my_favorite_count == 0U &&
            home->recommended_count == 0U)
    {
        (void)app_ui_add_body_label(state->list, "暂无歌单，稍后重试");
    }
}

static void _root_timer_cb(lv_timer_t *timer)
{
    music_root_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    _root_update_status(state);
    if (!state->dirty)
    {
        return;
    }
    const music_home_snapshot_t *home = NULL;
    const esp_err_t result = music_service_home_acquire(&home);
    if (result == ESP_OK && home != NULL)
    {
        if (home->generation != state->shown_generation)
        {
            state->shown_generation = home->generation;
            _root_build_list(state, home);
        }
        music_service_home_release(home);
        state->dirty = false;
    }
    else if (result == ESP_ERR_NOT_FOUND && state->shown_generation != 0U)
    {
        lv_obj_clean(state->list);
        state->ref_count = 0U;
        state->shown_generation = 0U;
        (void)app_ui_add_body_label(state->list, "暂无歌单，稍后重试");
        state->dirty = false;
    }
}

static void _root_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                           const void *payload, size_t payload_size,
                           void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_root_state_t *state = user_data;
    if (state != NULL)
    {
        state->dirty = true;
    }
}

static void _root_playlist_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    music_playlist_ref_t *ref = lv_event_get_user_data(event);
    if (ref != NULL)
    {
        const esp_err_t result = music_ui_open_playlist(ref);
        if (result != ESP_OK)
        {
            LOG_W("music playlist navigation failed: %s",
                  esp_err_to_name(result));
        }
    }
}

static void _root_login_clicked(lv_event_t *event)
{
    (void)event;
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_MUSIC,
        .page_id = MUSIC_PAGE_LOGIN,
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    (void)app_manager_navigate_async(&request, NULL, NULL);
}

static void _root_logout_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && music_service_logout() != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "退出登录失败，请稍后重试",
                               APP_UI_STATUS_WARNING);
    }
}

static void _root_refresh_clicked(lv_event_t *event)
{
    music_root_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && music_service_refresh_home(true) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "刷新请求失败，请稍后重试",
                               APP_UI_STATUS_WARNING);
    }
}

static void _root_event_bus_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                               const void *payload, size_t payload_size,
                               void *user_data)
{
    _root_event_cb(msg_id, sub_type, payload, payload_size, user_data);
}

static void _root_mount(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
    state->ref_count = 0U;
    state->shown_generation = 0U;
    state->dirty = true;
    app_ui_page_create(&state->page, "音乐", true);
    state->status_label = app_ui_add_body_label(state->page.content,
        "正在连接音乐服务…");
    lv_obj_t *buttons = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->login_button = app_ui_button_create(buttons, "扫码登录",
        _root_login_clicked, state);
    state->logout_button = app_ui_button_create(buttons, "退出登录",
        _root_logout_clicked, state);
    lv_obj_set_hidden(state->logout_button, true);
    state->refresh_button = app_ui_button_create(buttons, "刷新",
        _root_refresh_clicked, state);
    state->list = lv_obj_create(state->page.content);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(state->list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->list, 0, 0);
    lv_obj_set_style_pad_all(state->list, 0, 0);
    lv_obj_set_style_pad_row(state->list, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_scroll_dir(state->list, LV_DIR_NONE);
}

static void _root_resume(const app_manager_page_context_t *context)
{
    music_root_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _root_event_bus_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_root_timer_cb, 500U, state);
    state->dirty = true;
    _root_update_status(state);
    (void)music_service_refresh_home(true);
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
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->list = NULL;
    state->login_button = NULL;
    state->logout_button = NULL;
    state->refresh_button = NULL;
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
