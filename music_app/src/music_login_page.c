#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_LOGIN_QR_SIZE 224

typedef struct music_login_state
{
    app_ui_page_t page;
    lv_obj_t *identity_card;
    lv_obj_t *identity_label;
    lv_obj_t *account_label;
    lv_obj_t *status_label;
    lv_obj_t *instruction_label;
    lv_obj_t *qr_frame;
    lv_obj_t *qr_object;
    lv_obj_t *qr_placeholder;
    lv_obj_t *login_actions;
    lv_obj_t *account_actions;
    lv_obj_t *qq_button;
    lv_obj_t *wechat_button;
    lv_obj_t *cancel_button;
    lv_obj_t *logout_button;
    lv_obj_t *confirm_sheet;
    music_image_t *qr_image;
    lv_image_dsc_t qr_descriptor;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    uint64_t shown_generation;
    uint8_t last_state;
    uint8_t last_failure;
    bool qr_loading;
    bool request_pending;
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
}

static bool _login_shows_qr(music_service_state_t state)
{
    return state == MUSIC_SERVICE_STATE_QR_READY ||
           state == MUSIC_SERVICE_STATE_QR_SCANNED ||
           state == MUSIC_SERVICE_STATE_AUTHENTICATING;
}

static void _login_show_qr(music_login_state_t *state,
                           const music_service_status_t *status)
{
    if (!_login_shows_qr(status->state))
    {
        _login_release_qr(state);
        return;
    }
    if (state->qr_image != NULL)
    {
        return;
    }
    lv_obj_set_hidden(state->qr_frame, false);
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
        state->qr_loading = false;
    }
    else
    {
        state->qr_loading = true;
    }
}

static void _login_set_actions(music_login_state_t *state, bool ready)
{
    lv_obj_set_hidden(state->login_actions, ready);
    lv_obj_set_hidden(state->account_actions, !ready);
    lv_obj_t *buttons[] =
    {
        state->qq_button, state->wechat_button, state->cancel_button,
        state->logout_button
    };
    for (size_t index = 0U; index < sizeof(buttons) / sizeof(buttons[0]);
            ++index)
    {
        if (buttons[index] != NULL)
        {
            lv_obj_add_state(buttons[index], LV_STATE_DISABLED);
        }
    }
}

static void _login_update_buttons(music_login_state_t *state,
                                  const music_service_status_t *status)
{
    const bool ready = status->state == MUSIC_SERVICE_STATE_READY &&
                       status->logged_in;
    const bool qr_flow = _login_shows_qr(status->state);
    _login_set_actions(state, ready);
    lv_obj_t *buttons[] =
    {
        state->qq_button, state->wechat_button, state->cancel_button,
        state->logout_button
    };
    for (size_t index = 0U; index < sizeof(buttons) / sizeof(buttons[0]);
            ++index)
    {
        if (buttons[index] == NULL)
        {
            continue;
        }
        const bool disabled = state->request_pending ||
                              (index < 2U && qr_flow) ||
                              (ready && index < 3U);
        if (!disabled)
        {
            lv_obj_remove_state(buttons[index], LV_STATE_DISABLED);
        }
    }
    if (state->cancel_button != NULL)
    {
        lv_obj_set_hidden(state->cancel_button, !qr_flow);
    }
}

static void _login_update(music_login_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "音乐服务不可用",
                               APP_UI_STATUS_ERROR);
        app_ui_label_set_text_if(state->identity_label, "音乐服务未连接");
        app_ui_label_set_text_if(state->account_label,
                                 "请返回首页稍后重试");
        _login_release_qr(state);
        lv_obj_set_hidden(state->qr_frame, true);
        lv_obj_add_state(state->qq_button, LV_STATE_DISABLED);
        lv_obj_add_state(state->wechat_button, LV_STATE_DISABLED);
        return;
    }
    const bool ready = status.state == MUSIC_SERVICE_STATE_READY &&
                       status.logged_in;
    const bool error = status.state == MUSIC_SERVICE_STATE_ERROR ||
                       status.failure != MUSIC_SERVICE_FAILURE_NONE;
    app_ui_label_set_text_if(state->identity_label,
                             ready ? "QQ 音乐账号" : "扫码登录");
    if (ready)
    {
        app_ui_label_set_text_if(state->account_label,
                                 status.user_name[0] == '\0' ?
                                 "已登录" : status.user_name);
    }
    else if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        app_ui_label_set_text_if(state->account_label,
                                 music_ui_failure_text(status.failure));
    }
    else
    {
        app_ui_label_set_text_if(state->account_label,
                                 "登录后可同步收藏与歌单");
    }
    char status_text[128];
    (void)snprintf(status_text, sizeof(status_text), "%s%s%s",
                   music_ui_state_text(&status),
                   status.qr_status[0] == '\0' ? "" : " · ",
                   status.qr_status);
    app_ui_set_status_text(state->status_label, status_text,
                           error ? APP_UI_STATUS_WARNING :
                           ready ? APP_UI_STATUS_SUCCESS :
                           _login_shows_qr(status.state) ?
                           APP_UI_STATUS_ACCENT : APP_UI_STATUS_NEUTRAL);
    if (status.state == MUSIC_SERVICE_STATE_QR_SCANNED)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "已扫码，请在手机上确认");
    }
    else if (status.state == MUSIC_SERVICE_STATE_AUTHENTICATING)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "正在完成登录…");
    }
    else if (status.state == MUSIC_SERVICE_STATE_QR_READY)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "使用对应 App 扫描二维码");
    }
    else if (ready)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "账号数据仅保存在本机");
    }
    else if (!status.network_ready)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "等待网络连接后重试");
    }
    else if (!status.time_ready)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "等待时间同步后重试");
    }
    else if (status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "请检查网络后选择一种登录方式");
    }
    else
    {
        app_ui_label_set_text_if(state->instruction_label,
                                 "选择一种扫码方式");
    }
    const bool shows_qr = _login_shows_qr(status.state);
    lv_obj_set_hidden(state->identity_card, shows_qr);
    lv_obj_set_hidden(state->qr_frame, !shows_qr);
    _login_show_qr(state, &status);
    if (state->qr_loading && _login_shows_qr(status.state))
    {
        app_ui_set_status_text(state->status_label, "正在获取二维码…",
                               APP_UI_STATUS_NEUTRAL);
    }
    if (state->request_pending &&
            (status.generation != state->shown_generation ||
             status.state != state->last_state ||
             status.failure != state->last_failure))
    {
        state->request_pending = false;
    }
    _login_update_buttons(state, &status);
    state->shown_generation = status.generation;
    state->last_state = status.state;
    state->last_failure = status.failure;
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

static void _login_start(music_login_state_t *state,
                         music_login_type_t type)
{
    if (state == NULL || state->request_pending)
    {
        return;
    }
    state->request_pending = true;
    _login_set_actions(state, false);
    const esp_err_t result = music_service_login_begin(type);
    if (result != ESP_OK)
    {
        state->request_pending = false;
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "登录请求超时，请重试" :
                               result == ESP_ERR_INVALID_STATE ?
                               "请等待当前登录或网络状态就绪" :
                               "当前无法开始登录",
                               APP_UI_STATUS_WARNING);
        _login_update(state);
        return;
    }
    _login_release_qr(state);
    lv_obj_set_hidden(state->qr_frame, false);
    app_ui_set_status_text(state->status_label, "正在获取二维码…",
                           APP_UI_STATUS_ACCENT);
    app_ui_label_set_text_if(state->instruction_label,
                             "请保持页面打开直到确认完成");
}

static void _login_qq_clicked(lv_event_t *event)
{
    _login_start(lv_event_get_user_data(event), MUSIC_LOGIN_TYPE_QQ);
}

static void _login_wechat_clicked(lv_event_t *event)
{
    _login_start(lv_event_get_user_data(event), MUSIC_LOGIN_TYPE_WECHAT);
}

static void _login_cancel_clicked(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->request_pending)
    {
        return;
    }
    state->request_pending = true;
    _login_set_actions(state, false);
    const esp_err_t result = music_service_login_cancel();
    _login_release_qr(state);
    state->request_pending = false;
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               "取消登录失败，请重试",
                               APP_UI_STATUS_WARNING);
        _login_update(state);
        return;
    }
    app_ui_set_status_text(state->status_label, "已取消登录",
                           APP_UI_STATUS_NEUTRAL);
    _login_update(state);
}

static void _login_cancel_sheet(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
}

static void _login_confirm_logout(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->request_pending)
    {
        return;
    }
    state->request_pending = true;
    _login_set_actions(state, true);
    const esp_err_t result = music_service_logout();
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    if (result != ESP_OK)
    {
        state->request_pending = false;
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "退出请求超时，请重试" :
                               "当前无法退出账号",
                               APP_UI_STATUS_WARNING);
        _login_update(state);
        return;
    }
    app_ui_set_status_text(state->status_label, "正在退出账号…",
                           APP_UI_STATUS_ACCENT);
}

static void _login_logout_clicked(lv_event_t *event)
{
    music_login_state_t *state = lv_event_get_user_data(event);
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
                                  _login_confirm_logout, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _login_cancel_sheet, state);
}

static lv_obj_t *_login_identity_card(music_login_state_t *state,
                                      lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(card, false);
    state->identity_label = app_ui_add_body_label(card, "扫码登录");
    lv_obj_set_style_text_font(state->identity_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->identity_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    state->account_label = app_ui_add_section(card, "登录后可同步收藏与歌单");
    lv_obj_set_width(state->account_label, LV_PCT(100));
    lv_label_set_long_mode(state->account_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_top(state->account_label, 0, 0);
    lv_obj_set_style_text_color(state->account_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    return card;
}

static void _login_mount(const app_manager_page_context_t *context)
{
    music_login_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "账号", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP, 0);
    state->identity_card = _login_identity_card(state,
        state->page.content);

    state->status_label = app_ui_add_body_label(state->page.content,
        "连接中…");
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    state->instruction_label = app_ui_add_body_label(state->page.content,
        "正在检查账号状态");

    state->qr_frame = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->qr_frame);
    lv_obj_set_width(state->qr_frame, LV_PCT(100));
    lv_obj_set_height(state->qr_frame, MUSIC_LOGIN_QR_SIZE);
    lv_obj_set_flex_flow(state->qr_frame, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->qr_frame, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(state->qr_frame, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(state->qr_frame,
                              lv_color_hex(APP_UI_COLOR_SURFACE_LO), 0);
    lv_obj_set_style_bg_opa(state->qr_frame, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(state->qr_frame, LV_DIR_NONE);
    app_ui_make_passive(state->qr_frame, false);
    lv_obj_set_hidden(state->qr_frame, true);
    state->qr_placeholder = app_ui_image(state->qr_frame,
                                         APP_IMAGE_MUSIC_ICON,
                                         LV_SYMBOL_AUDIO, 48);
    if (state->qr_placeholder != NULL)
    {
        app_ui_icon_set_color(state->qr_placeholder, APP_UI_COLOR_RAIN);
        lv_obj_center(state->qr_placeholder);
    }
    state->qr_object = lv_image_create(state->qr_frame);
    lv_obj_set_size(state->qr_object, MUSIC_LOGIN_QR_SIZE,
                    MUSIC_LOGIN_QR_SIZE);
    lv_obj_set_hidden(state->qr_object, true);
    app_ui_make_passive(state->qr_object, false);

    state->login_actions = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->qq_button = app_ui_button_create(state->login_actions, "QQ 扫码",
                                            _login_qq_clicked, state);
    state->wechat_button = app_ui_button_create(state->login_actions,
        "微信扫码", _login_wechat_clicked, state);
    state->cancel_button = app_ui_button_create(state->page.content,
        "取消登录", _login_cancel_clicked, state);
    lv_obj_set_flex_grow(state->cancel_button, 0);
    lv_obj_set_width(state->cancel_button, LV_PCT(100));
    lv_obj_set_height(state->cancel_button, APP_UI_BUTTON_HEIGHT);
    lv_obj_set_hidden(state->cancel_button, true);

    state->account_actions = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    state->logout_button = app_ui_button_create(state->account_actions,
        "退出登录", _login_logout_clicked, state);
    app_ui_button_set_kind(state->logout_button, APP_UI_BUTTON_DANGER);
    lv_obj_set_hidden(state->account_actions, true);
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
    const esp_err_t cancel_result = music_service_login_cancel();
    _login_release_qr(state);
    state->request_pending = false;
    return cancel_result;
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
};

const app_manager_page_definition_t music_login_page_definition =
{
    .ops = &s_login_ops,
    .memory_size = sizeof(music_login_state_t),
};
