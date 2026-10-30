#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

typedef struct music_login_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *qr_object;
    lv_obj_t *wechat_button;
    music_image_t *qr_image;
    lv_image_dsc_t qr_descriptor;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
} music_login_state_t;

_Static_assert(sizeof(music_login_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music login state exceeds the page arena");

static void _login_release_qr(music_login_state_t *state)
{
    if (state->qr_object != NULL)
    {
        lv_image_set_src(state->qr_object, NULL);
        lv_obj_set_hidden(state->qr_object, true);
    }
    if (state->qr_image != NULL)
    {
        music_service_login_qr_release(state->qr_image);
        state->qr_image = NULL;
    }
}

static void _login_show_qr(music_login_state_t *state)
{
    if (state->qr_image != NULL)
    {
        return;
    }
    const music_image_t *image = NULL;
    if (music_service_login_qr_acquire(&image) != ESP_OK || image == NULL)
    {
        return;
    }
    state->qr_image = (music_image_t *)image;
    if (state->qr_object == NULL)
    {
        state->qr_object = lv_image_create(state->page.content);
        lv_obj_set_size(state->qr_object, 240, 240);
    }
    music_ui_show_image(state->qr_object, image, &state->qr_descriptor);
    lv_obj_set_hidden(state->qr_object, false);
}

static void _login_update(music_login_state_t *state)
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
                   status.qr_status[0] == '\0' ? "" : " · ",
                   status.qr_status);
    app_ui_set_status_text(state->status_label, text,
                           status.state == MUSIC_SERVICE_STATE_READY ?
                           APP_UI_STATUS_SUCCESS :
                           status.failure == MUSIC_SERVICE_FAILURE_NONE ?
                           APP_UI_STATUS_NEUTRAL : APP_UI_STATUS_WARNING);
    _login_show_qr(state);
    if (status.state == MUSIC_SERVICE_STATE_READY)
    {
        _login_release_qr(state);
        if (state->wechat_button != NULL)
        {
            lv_obj_add_state(state->wechat_button, LV_STATE_DISABLED);
        }
    }
    else
    {
        if (state->wechat_button != NULL)
        {
            lv_obj_remove_state(state->wechat_button, LV_STATE_DISABLED);
        }
    }
}

static void _login_timer_cb(lv_timer_t *timer)
{
    music_login_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL)
    {
        _login_update(state);
    }
}

static void _login_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                            const void *payload, size_t payload_size,
                            void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_login_state_t *state = user_data;
    if (state != NULL)
    {
        _login_update(state);
    }
}

static void _login_wechat_clicked(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    _login_release_qr(state);
    const esp_err_t result = music_service_login_begin(MUSIC_LOGIN_TYPE_WECHAT);
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "无法开始登录，请稍后重试",
                               APP_UI_STATUS_WARNING);
    }
}

static void _login_mount(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    state->qr_image = NULL;
    state->qr_object = NULL;
    memset(&state->qr_descriptor, 0, sizeof(state->qr_descriptor));
    app_ui_page_create(&state->page, "登录", true);
    (void)app_ui_add_body_label(state->page.content,
                                "使用微信扫描二维码登录 QQ 音乐。"
                                "登录态只保存在本机。");
    state->status_label = app_ui_add_body_label(state->page.content,
        "请选择登录方式");
    lv_obj_t *buttons = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->wechat_button = app_ui_button_create(buttons, "微信扫码",
        _login_wechat_clicked, state);
    state->qr_object = lv_image_create(state->page.content);
    lv_obj_set_size(state->qr_object, 240, 240);
    lv_obj_set_hidden(state->qr_object, true);
}

static void _login_resume(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _login_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_login_timer_cb, 500U, state);
    _login_update(state);
}

static esp_err_t _login_pause(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
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
    (void)music_service_login_cancel();
    _login_release_qr(state);
    return ESP_OK;
}

static void _login_unmount(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    _login_release_qr(state);
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->qr_object = NULL;
    state->wechat_button = NULL;
}

static const app_manager_page_ops_t s_login_ops =
{
    .mount = _login_mount,
    .resume = _login_resume,
    .pause = _login_pause,
    .unmount = _login_unmount,
};

const app_manager_page_definition_t music_login_page_definition =
{
    .ops = &s_login_ops,
    .memory_size = sizeof(music_login_state_t),
};
