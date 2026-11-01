#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_LOGIN_QR_SIZE 240
#define MUSIC_LOGIN_SUCCESS_TICKS 3U

typedef struct music_login_state
{
    app_ui_page_t page;
    lv_obj_t *qr_frame;
    lv_obj_t *qr_object;
    lv_obj_t *qr_placeholder;
    lv_obj_t *login_title;
    lv_obj_t *success_view;
    lv_obj_t *success_icon;
    lv_obj_t *success_title;
    lv_obj_t *success_subtitle;
    lv_obj_t *qr_overlay;
    lv_obj_t *bottom_area;
    lv_obj_t *bottom_scrim;
    lv_obj_t *status_label;
    lv_obj_t *instruction_label;
    music_image_t *qr_image;
    lv_image_dsc_t qr_descriptor;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    uint64_t shown_generation;
    music_login_type_t login_type;
    uint8_t last_state;
    uint8_t last_failure;
    uint8_t success_ticks;
    bool qr_loading;
    bool request_pending;
    bool expired;
    bool auto_started;
    bool success;
} music_login_state_t;

_Static_assert(sizeof(music_login_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music login state exceeds the page arena");

static void _login_start(music_login_state_t *state,
                         music_login_type_t type);

static const char *_login_channel_title(music_login_type_t type)
{
    return type == MUSIC_LOGIN_TYPE_WECHAT ? "微信扫码登录" :
           "QQ 扫码登录";
}

static const char *_login_channel_instruction(music_login_type_t type)
{
    return type == MUSIC_LOGIN_TYPE_WECHAT ? "打开微信扫一扫" :
           "打开手机 QQ 扫一扫";
}

static void _login_release_qr(music_login_state_t *state)
{
    if (state->qr_object != NULL)
    {
        lv_image_set_src(state->qr_object, NULL);
        lv_obj_set_hidden(state->qr_object, true);
    }
    if (state->qr_placeholder != NULL)
    {
        lv_obj_set_hidden(state->qr_placeholder, false);
    }
    if (state->qr_image != NULL)
    {
        music_service_login_qr_release(state->qr_image);
        state->qr_image = NULL;
    }
    memset(&state->qr_descriptor, 0, sizeof(state->qr_descriptor));
    state->qr_loading = false;
    state->expired = false;
}

static bool _login_shows_qr(music_service_state_t state)
{
    return state == MUSIC_SERVICE_STATE_QR_READY ||
           state == MUSIC_SERVICE_STATE_QR_SCANNED ||
           state == MUSIC_SERVICE_STATE_AUTHENTICATING;
}

static void _login_set_overlay(music_login_state_t *state, bool visible)
{
    if (state->qr_overlay != NULL)
    {
        lv_obj_set_hidden(state->qr_overlay, !visible);
    }
    if (state->bottom_scrim != NULL)
    {
        lv_obj_set_hidden(state->bottom_scrim, !visible);
    }
    state->expired = visible;
}

static void _login_set_status(music_login_state_t *state, const char *text,
                              app_ui_status_t status_color)
{
    app_ui_set_status_text(state->status_label, text, status_color);
    lv_obj_set_hidden(state->status_label, text == NULL || text[0] == '\0');
}

static void _login_set_instruction(music_login_state_t *state,
                                   const char *text)
{
    (void)state;
    (void)text;
}

static void _login_set_bottom_visible(music_login_state_t *state, bool visible)
{
    if (state->bottom_area != NULL)
    {
        lv_obj_set_hidden(state->bottom_area, !visible);
    }
}

static void _login_show_qr(music_login_state_t *state,
                           const music_service_status_t *status)
{
    if (!_login_shows_qr(status->state) &&
            !(status->state == MUSIC_SERVICE_STATE_AUTH_REQUIRED &&
              status->qr_available))
    {
        return;
    }
    if (state->qr_image != NULL)
    {
        lv_obj_set_hidden(state->qr_object, false);
        if (state->qr_placeholder != NULL)
        {
            lv_obj_set_hidden(state->qr_placeholder, true);
        }
        lv_obj_set_hidden(state->login_title, false);
        state->qr_loading = false;
        return;
    }
    const music_image_t *image = NULL;
    if (music_service_login_qr_acquire(&image) == ESP_OK && image != NULL)
    {
        state->qr_image = (music_image_t *)image;
        music_ui_show_image(state->qr_object, image, &state->qr_descriptor,
                            MUSIC_LOGIN_QR_SIZE, MUSIC_LOGIN_QR_SIZE);
        lv_obj_set_hidden(state->qr_object, false);
        if (state->qr_placeholder != NULL)
        {
            lv_obj_set_hidden(state->qr_placeholder, true);
        }
        lv_obj_set_hidden(state->login_title, false);
        state->qr_loading = false;
    }
    else
    {
        state->qr_loading = true;
    }
}

static void _login_show_success(music_login_state_t *state,
                                const music_service_status_t *status,
                                bool just_logged_in)
{
    state->success = true;
    state->success_ticks = 0U;
    state->request_pending = false;
    _login_set_overlay(state, false);
    lv_obj_set_hidden(state->qr_frame, true);
    lv_obj_set_hidden(state->login_title, true);
    lv_obj_set_hidden(state->status_label, true);
    _login_set_bottom_visible(state, false);
    if (state->success_view != NULL)
    {
        lv_obj_set_hidden(state->success_view, false);
    }
    app_ui_label_set_text_if(state->success_title,
                             just_logged_in ? "登录成功" : "已登录");
    app_ui_label_set_text_if(state->success_subtitle,
                             just_logged_in ? "开启你的音乐之旅吧" :
                             (status->user_name[0] == '\0' ?
                              "账号已登录，可直接使用" : status->user_name));
}

static void _login_update(music_login_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        _login_set_status(state, "音乐服务暂不可用", APP_UI_STATUS_WARNING);
        _login_set_instruction(state, "");
        _login_set_bottom_visible(state, false);
        _login_release_qr(state);
        _login_set_overlay(state, false);
        return;
    }
    if (!state->auto_started && _login_shows_qr(status.state))
    {
        state->login_type = status.login_type;
        state->auto_started = true;
    }
    const bool ready = status.state == MUSIC_SERVICE_STATE_READY &&
                       status.logged_in;
    if (state->success && !ready)
    {
        state->success = false;
        state->success_ticks = 0U;
        lv_obj_set_hidden(state->success_view, true);
    }
    if (ready)
    {
        if (!state->success)
        {
            _login_show_success(state, &status, state->auto_started);
        }
        return;
    }
    if (state->success)
    {
        return;
    }

    lv_obj_set_hidden(state->success_view, true);
    _login_set_bottom_visible(state, false);
    const bool shows_qr = _login_shows_qr(status.state);
    if (shows_qr)
    {
        lv_obj_set_hidden(state->qr_frame, false);
        _login_set_overlay(state, false);
        if (status.state == MUSIC_SERVICE_STATE_QR_READY)
        {
            state->request_pending = false;
        }
        _login_show_qr(state, &status);
        _login_set_instruction(state,
                               status.state == MUSIC_SERVICE_STATE_QR_SCANNED ||
                               status.state == MUSIC_SERVICE_STATE_AUTHENTICATING ?
                               "" : _login_channel_instruction(
                                   state->login_type));
        if (status.state == MUSIC_SERVICE_STATE_QR_SCANNED)
        {
            _login_set_status(state, "已扫码，请确认", APP_UI_STATUS_SUCCESS);
        }
        else if (status.state == MUSIC_SERVICE_STATE_AUTHENTICATING)
        {
            _login_set_status(state, "正在完成登录…", APP_UI_STATUS_ACCENT);
        }
        else if (state->qr_loading)
        {
            _login_set_status(state, "正在获取二维码…", APP_UI_STATUS_NEUTRAL);
        }
        else
        {
            _login_set_status(state, "", APP_UI_STATUS_NEUTRAL);
        }
    }
    else if (status.state == MUSIC_SERVICE_STATE_AUTH_REQUIRED &&
             status.qr_available)
    {
        lv_obj_set_hidden(state->qr_frame, false);
        _login_show_qr(state, &status);
        _login_set_overlay(state, true);
        _login_set_status(state, "二维码已过期", APP_UI_STATUS_WARNING);
        _login_set_instruction(state, _login_channel_instruction(
                                   state->login_type));
        state->request_pending = false;
    }
    else
    {
        lv_obj_set_hidden(state->qr_frame, true);
        lv_obj_set_hidden(state->login_title, true);
        _login_set_bottom_visible(state, false);
        _login_set_overlay(state, false);
        _login_release_qr(state);
        if (status.state == MUSIC_SERVICE_STATE_WAITING_NETWORK)
        {
            _login_set_status(state, "等待网络连接", APP_UI_STATUS_NEUTRAL);
        }
        else if (status.state == MUSIC_SERVICE_STATE_WAITING_TIME)
        {
            _login_set_status(state, "等待时间同步", APP_UI_STATUS_NEUTRAL);
        }
        else if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
        {
            _login_set_status(state, music_ui_failure_text(status.failure),
                              APP_UI_STATUS_WARNING);
        }
        else
        {
            _login_set_status(state, "", APP_UI_STATUS_NEUTRAL);
        }
        _login_set_instruction(state, "");
    }
    if (state->request_pending &&
            (status.generation != state->shown_generation ||
             status.state != state->last_state ||
             status.failure != state->last_failure))
    {
        state->request_pending = false;
    }
    state->shown_generation = status.generation;
    state->last_state = status.state;
    state->last_failure = status.failure;
}

static void _login_timer_cb(lv_timer_t *timer)
{
    music_login_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    if (state->success)
    {
        if (state->success_ticks < MUSIC_LOGIN_SUCCESS_TICKS)
        {
            ++state->success_ticks;
        }
        else
        {
            (void)music_ui_enter_player();
        }
        return;
    }
    music_service_status_t status;
    if (!state->auto_started && !state->request_pending &&
            music_service_get_status(&status) == ESP_OK &&
            status.state == MUSIC_SERVICE_STATE_LOGGED_OUT &&
            !status.logged_in)
    {
        _login_start(state, state->login_type);
    }
    _login_update(state);
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

static void _login_start(music_login_state_t *state,
                         music_login_type_t type)
{
    if (state == NULL || state->request_pending || state->success)
    {
        return;
    }
    state->request_pending = true;
    state->auto_started = true;
    state->login_type = type;
    _login_set_overlay(state, false);
    (void)music_service_login_cancel();
    _login_release_qr(state);
    lv_obj_set_hidden(state->qr_frame, false);
    lv_obj_set_hidden(state->login_title, false);
    lv_obj_set_hidden(state->success_view, true);
    _login_set_bottom_visible(state, false);
    _login_set_status(state, "正在获取二维码…", APP_UI_STATUS_NEUTRAL);
    _login_set_instruction(state, _login_channel_instruction(type));
    const esp_err_t result = music_service_login_begin(type);
    if (result != ESP_OK)
    {
        state->request_pending = false;
        state->auto_started = false;
        _login_set_status(state,
                          result == ESP_ERR_TIMEOUT ?
                          "登录请求超时，请重试" :
                          result == ESP_ERR_INVALID_STATE ?
                          "请等待网络或当前登录结束" :
                          "当前无法开始登录",
                          APP_UI_STATUS_WARNING);
        return;
    }
    _login_update(state);
}

static void _login_refresh_clicked(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        state->request_pending = false;
        _login_start(state, state->login_type);
    }
}

static void _login_close_clicked(lv_event_t *event)
{
    (void)event;
    app_ui_request_back();
}

static void _login_copy_arguments(music_login_state_t *state,
                                  const app_manager_typed_blob_t *arguments)
{
    if (arguments == NULL ||
            arguments->version != APP_MANAGER_TYPED_BLOB_VERSION ||
            arguments->type != MUSIC_ARGUMENT_LOGIN ||
            arguments->size != sizeof(music_login_arguments_t))
    {
        return;
    }
    music_login_arguments_t payload;
    memcpy(&payload, arguments->payload, sizeof(payload));
    if (payload.type == MUSIC_LOGIN_TYPE_QQ ||
            payload.type == MUSIC_LOGIN_TYPE_WECHAT)
    {
        state->login_type = payload.type;
    }
}

static lv_obj_t *_login_label(lv_obj_t *parent, const char *text,
                              app_theme_font_id_t font, uint32_t color,
                              lv_text_align_t align, bool wrap)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP :
                           LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static void _login_mount(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->login_type = MUSIC_LOGIN_TYPE_QQ;
    _login_copy_arguments(state, context->arguments);
    app_ui_page_create_home(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_bottom(state->page.content, 8, 0);
    lv_obj_set_style_pad_left(state->page.content, 24, 0);
    lv_obj_set_style_pad_right(state->page.content, 24, 0);
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);

    lv_obj_t *top = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, MUSIC_UI_CONTROL_SIZE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(top, false);
    (void)music_ui_plain_icon_button(top, APP_IMAGE_UI_CLOSE,
                                     LV_SYMBOL_CLOSE, MUSIC_UI_CONTROL_SIZE,
                                     26, APP_UI_COLOR_MUTED,
                                     _login_close_clicked, state);

    lv_obj_t *center = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(center);
    lv_obj_set_width(center, LV_PCT(100));
    lv_obj_set_height(center, 0);
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, 8, 0);
    app_ui_make_passive(center, false);

    state->qr_frame = lv_obj_create(center);
    lv_obj_remove_style_all(state->qr_frame);
    lv_obj_set_size(state->qr_frame, MUSIC_LOGIN_QR_SIZE,
                    MUSIC_LOGIN_QR_SIZE);
    lv_obj_set_style_radius(state->qr_frame, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(state->qr_frame,
                              lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_opa(state->qr_frame, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(state->qr_frame, true, 0);
    lv_obj_set_flex_flow(state->qr_frame, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->qr_frame, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(state->qr_frame, false);
    state->qr_placeholder = music_ui_image(state->qr_frame,
                                           APP_IMAGE_MUSIC_ICON,
                                           LV_SYMBOL_AUDIO, 64);
    if (state->qr_placeholder != NULL)
    {
        app_ui_icon_set_color(state->qr_placeholder, APP_UI_COLOR_BACKGROUND);
        lv_obj_center(state->qr_placeholder);
    }
    state->qr_object = lv_image_create(state->qr_frame);
    lv_obj_set_size(state->qr_object, MUSIC_LOGIN_QR_SIZE,
                    MUSIC_LOGIN_QR_SIZE);
    lv_obj_set_hidden(state->qr_object, true);
    lv_image_set_antialias(state->qr_object, false);
    app_ui_make_passive(state->qr_object, false);
    state->qr_overlay = lv_button_create(state->qr_frame);
    lv_obj_set_size(state->qr_overlay, MUSIC_LOGIN_QR_SIZE,
                    MUSIC_LOGIN_QR_SIZE);
    lv_obj_set_ignore_layout(state->qr_overlay, true);
    lv_obj_align(state->qr_overlay, LV_ALIGN_CENTER, 0, 0);
    music_ui_flat_button_reset(state->qr_overlay, APP_UI_RADIUS_CARD);
    lv_obj_set_style_bg_color(state->qr_overlay,
                              lv_color_hex(APP_UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(state->qr_overlay, LV_OPA_60, 0);
    lv_obj_set_flex_flow(state->qr_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->qr_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->qr_overlay, 6, 0);
    app_ui_click_only(state->qr_overlay);
    lv_obj_add_event_cb(state->qr_overlay, _login_refresh_clicked,
                        LV_EVENT_CLICKED, state);
    lv_obj_t *refresh = music_ui_image(state->qr_overlay,
                                       APP_IMAGE_UI_REFRESH,
                                       LV_SYMBOL_REFRESH, 40);
    if (refresh != NULL)
    {
        app_ui_icon_set_color(refresh, APP_UI_COLOR_RAIN);
    }
    (void)_login_label(state->qr_overlay, "点击刷新",
                       APP_THEME_FONT_BODY, APP_UI_COLOR_TEXT,
                       LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_hidden(state->qr_overlay, true);

    state->login_title = _login_label(center, _login_channel_title(
                                          state->login_type),
                                      APP_THEME_FONT_HEAD,
                                      APP_UI_COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_height(state->login_title,
                      lv_font_get_line_height(app_ui_font(
                                  APP_THEME_FONT_HEAD)));
    state->status_label = _login_label(center, "正在获取二维码…",
                                       APP_THEME_FONT_SMALL,
                                       APP_UI_COLOR_MUTED,
                                       LV_TEXT_ALIGN_CENTER, false);

    state->success_view = lv_obj_create(center);
    lv_obj_remove_style_all(state->success_view);
    lv_obj_set_width(state->success_view, LV_PCT(100));
    lv_obj_set_height(state->success_view, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->success_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->success_view, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->success_view, 14, 0);
    app_ui_make_passive(state->success_view, false);
    lv_obj_t *logo = lv_obj_create(state->success_view);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, 112, 112);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    app_ui_make_passive(logo, false);
    state->success_icon = music_ui_image(logo, APP_IMAGE_MUSIC_ICON,
                                         LV_SYMBOL_AUDIO, 64);
    if (state->success_icon != NULL)
    {
        app_ui_icon_set_color(state->success_icon, APP_UI_COLOR_RAIN);
        lv_obj_center(state->success_icon);
    }
    state->success_title = _login_label(state->success_view, "登录成功",
                                        APP_THEME_FONT_BIGL,
                                        APP_UI_COLOR_TEXT,
                                        LV_TEXT_ALIGN_CENTER, false);
    state->success_subtitle = _login_label(state->success_view,
                                           "开启你的音乐之旅吧",
                                           APP_THEME_FONT_BODY,
                                           APP_UI_COLOR_MUTED,
                                           LV_TEXT_ALIGN_CENTER, false);
    lv_obj_set_hidden(state->success_view, true);

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
    music_service_status_t status;
    if (!state->success && !state->auto_started && !state->request_pending &&
            music_service_get_status(&status) == ESP_OK &&
            status.state == MUSIC_SERVICE_STATE_LOGGED_OUT &&
            !status.logged_in)
    {
        _login_start(state, state->login_type);
    }
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
    const esp_err_t cancel_result = music_service_login_cancel();
    _login_release_qr(state);
    _login_set_overlay(state, false);
    state->request_pending = false;
    state->auto_started = false;
    state->success = false;
    return cancel_result;
}

static void _login_new_intent(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    const music_login_type_t previous = state->login_type;
    _login_copy_arguments(state, context->arguments);
    if (state->login_type != previous && state->page.root != NULL)
    {
        _login_release_qr(state);
        app_ui_label_set_text_if(state->login_title,
                                 _login_channel_title(state->login_type));
        _login_set_instruction(state,
                               _login_channel_instruction(state->login_type));
        state->auto_started = false;
        state->request_pending = false;
        _login_start(state, state->login_type);
    }
}

static void _login_unmount(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    _login_release_qr(state);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_login_ops =
{
    .mount = _login_mount,
    .resume = _login_resume,
    .pause = _login_pause,
    .unmount = _login_unmount,
    .new_intent = _login_new_intent,
};

const app_manager_page_definition_t music_login_page_definition =
{
    .ops = &s_login_ops,
    .memory_size = sizeof(music_login_state_t),
};
