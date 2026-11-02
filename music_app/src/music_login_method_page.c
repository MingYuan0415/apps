#include "music_app_internal.h"

#include "event_bus.h"

#include <string.h>

typedef struct music_login_method_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *retry_button;
    lv_obj_t *qq_row;
    lv_obj_t *wechat_row;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    uint32_t routed_ms;
    bool routed;
} music_login_method_state_t;

_Static_assert(sizeof(music_login_method_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Music login method state exceeds the page arena");

static void _method_update(music_login_method_state_t *state);

static void _method_close_clicked(lv_event_t *event)
{
    (void)event;
    app_ui_request_back();
}

static void _method_channel_clicked(lv_event_t *event)
{
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    const music_login_type_t type = encoded == MUSIC_LOGIN_TYPE_WECHAT ?
                                    MUSIC_LOGIN_TYPE_WECHAT :
                                    MUSIC_LOGIN_TYPE_QQ;
    (void)music_ui_open_login_qr(type);
}

static void _method_retry_clicked(lv_event_t *event)
{
    music_login_method_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        (void)music_service_retry();
        _method_update(state);
    }
}

static lv_obj_t *_method_well(lv_obj_t *parent, const char *text)
{
    lv_obj_t *well = lv_obj_create(parent);
    if (well == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(well);
    lv_obj_set_size(well, MUSIC_UI_WELL_SIZE, MUSIC_UI_WELL_SIZE);
    lv_obj_set_style_radius(well, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(well, lv_color_hex(APP_UI_COLOR_RAIN_TINT), 0);
    lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
    app_ui_make_passive(well, false);
    lv_obj_t *label = lv_label_create(well);
    if (label != NULL)
    {
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_RAIN), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, text != NULL ? text : "");
        app_ui_make_passive(label, false);
        lv_obj_center(label);
    }
    return well;
}

static lv_obj_t *_method_channel_row(lv_obj_t *parent, const char *well_text,
                                     const char *title, const char *subtitle,
                                     music_login_type_t type)
{
    lv_obj_t *row = lv_button_create(parent);
    if (row == NULL)
    {
        return NULL;
    }
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 72);
    music_ui_flat_button_reset(row, APP_UI_RADIUS_CARD);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(row, (void *)(uintptr_t)type);
    (void)_method_well(row, well_text);
    lv_obj_t *text = lv_obj_create(row);
    if (text == NULL)
    {
        return row;
    }
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    app_ui_make_passive(text, false);
    lv_obj_t *title_label = lv_label_create(text);
    if (title_label != NULL)
    {
        lv_obj_set_width(title_label, LV_PCT(100));
        lv_obj_set_height(title_label,
                          lv_font_get_line_height(app_ui_font(
                                      APP_THEME_FONT_BODY)));
        lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(title_label,
                                   app_ui_font(APP_THEME_FONT_BODY), 0);
        lv_obj_set_style_text_color(title_label,
                                    lv_color_hex(APP_UI_COLOR_TEXT), 0);
        lv_label_set_text(title_label, title != NULL ? title : "");
        app_ui_make_passive(title_label, false);
    }
    lv_obj_t *subtitle_label = lv_label_create(text);
    if (subtitle_label != NULL)
    {
        lv_obj_set_width(subtitle_label, LV_PCT(100));
        lv_obj_set_height(subtitle_label,
                          lv_font_get_line_height(app_ui_font(
                                      APP_THEME_FONT_SMALL)));
        lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(subtitle_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(subtitle_label,
                                    lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_label_set_text(subtitle_label,
                          subtitle != NULL ? subtitle : "");
        app_ui_make_passive(subtitle_label, false);
    }
    lv_obj_t *chevron = music_ui_image(row, APP_IMAGE_UI_CHEVRON_RIGHT,
                                       LV_SYMBOL_RIGHT, 20);
    if (chevron != NULL)
    {
        app_ui_icon_set_color(chevron, APP_UI_COLOR_FAINT);
    }
    lv_obj_add_event_cb(row, _method_channel_clicked, LV_EVENT_CLICKED, NULL);
    return row;
}

static void _method_set_enabled(music_login_method_state_t *state, bool enabled)
{
    lv_obj_t *rows[] = {state->qq_row, state->wechat_row};
    for (size_t index = 0U; index < sizeof(rows) / sizeof(rows[0]); ++index)
    {
        if (rows[index] == NULL)
        {
            continue;
        }
        if (enabled)
        {
            lv_obj_remove_state(rows[index], LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(rows[index], LV_STATE_DISABLED);
        }
    }
}

static void _method_set_retry(music_login_method_state_t *state, bool visible)
{
    if (state->retry_button != NULL)
    {
        lv_obj_set_hidden(state->retry_button, !visible);
    }
    if (state->subtitle_label != NULL)
    {
        lv_obj_set_hidden(state->subtitle_label, visible);
    }
}

static void _method_update(music_login_method_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        app_ui_set_status_text(state->status_label, "音乐服务暂不可用",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        _method_set_retry(state, true);
        _method_set_enabled(state, false);
        return;
    }
    if (state->routed &&
            (uint32_t)(lv_tick_get() - state->routed_ms) >= 2000U &&
            app_page_is_actived(APP_MANAGER_ID_MUSIC, MUSIC_PAGE_LOGIN_METHOD))
    {
        state->routed = false;
    }

    if (status.state == MUSIC_SERVICE_STATE_READY && status.logged_in)
    {
        if (!state->routed)
        {
            state->routed = true;
            state->routed_ms = lv_tick_get();
            (void)music_ui_enter_player();
        }
        return;
    }

    const bool service_error =
        status.state == MUSIC_SERVICE_STATE_ERROR;
    const bool waiting = !status.network_ready || !status.time_ready;
    const bool retry = service_error ||
                       (status.failure != MUSIC_SERVICE_FAILURE_NONE &&
                        status.state != MUSIC_SERVICE_STATE_AUTH_REQUIRED);
    _method_set_retry(state, retry);
    if (retry)
    {
        app_ui_set_status_text(
            state->status_label,
            status.failure != MUSIC_SERVICE_FAILURE_NONE ?
            music_ui_failure_text(status.failure) : "音乐服务暂不可用",
            APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
    }
    else if (!status.network_ready)
    {
        app_ui_set_status_text(state->status_label, "等待网络连接",
                               APP_UI_STATUS_NEUTRAL);
        lv_obj_set_hidden(state->status_label, false);
    }
    else if (!status.time_ready)
    {
        app_ui_set_status_text(state->status_label, "等待时间同步",
                               APP_UI_STATUS_NEUTRAL);
        lv_obj_set_hidden(state->status_label, false);
    }
    else if (status.state == MUSIC_SERVICE_STATE_AUTH_REQUIRED)
    {
        app_ui_set_status_text(state->status_label, "登录已过期，请重新登录",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
    }
    else
    {
        lv_obj_set_hidden(state->status_label, true);
    }
    _method_set_enabled(state, !waiting && !retry);
}

static void _method_timer_cb(lv_timer_t *timer)
{
    music_login_method_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL)
    {
        _method_update(state);
    }
}

static void _method_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_login_method_state_t *state = user_data;
    if (state != NULL)
    {
        _method_update(state);
    }
}

static void _method_mount(const app_manager_page_context_t *context)
{
    music_login_method_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create_home(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_bottom(state->page.content, 8, 0);
    lv_obj_set_style_pad_left(state->page.content, 24, 0);
    lv_obj_set_style_pad_right(state->page.content, 24, 0);
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    app_ui_make_passive(state->page.content, true);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state->page.content, LV_SCROLLBAR_MODE_AUTO);

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
                                     _method_close_clicked, state);

    lv_obj_t *hero = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 8, 0);
    app_ui_make_passive(hero, false);
    lv_obj_t *icon = music_ui_image(hero, APP_IMAGE_MUSIC_ICON,
                                    LV_SYMBOL_AUDIO, 64);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, APP_UI_COLOR_RAIN);
    }
    lv_obj_t *title = app_ui_add_section(hero, "QQ 音乐");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_pad_top(title, 0, 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    state->subtitle_label = app_ui_add_body_label(hero,
        "登录后同步歌单与收藏");
    lv_obj_set_width(state->subtitle_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->subtitle_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->subtitle_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_align(state->subtitle_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    state->retry_button = music_ui_plain_text_button(
                              hero, "重新检查", MUSIC_UI_CONTROL_SIZE,
                              APP_THEME_FONT_BODY, APP_UI_COLOR_RAIN,
                              _method_retry_clicked, state);
    if (state->retry_button != NULL)
    {
        lv_obj_set_width(state->retry_button, 160);
        lv_obj_set_hidden(state->retry_button, true);
    }

    state->qq_row = _method_channel_row(state->page.content, "QQ",
                                        "QQ 扫码登录",
                                        "使用手机 QQ 扫一扫",
                                        MUSIC_LOGIN_TYPE_QQ);
    state->wechat_row = _method_channel_row(state->page.content, "微信",
                                            "微信扫码登录",
                                            "使用微信扫一扫",
                                            MUSIC_LOGIN_TYPE_WECHAT);
    state->status_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_hidden(state->status_label, true);
}

static void _method_resume(const app_manager_page_context_t *context)
{
    music_login_method_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _method_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_method_timer_cb, 500U, state);
    _method_update(state);
}

static esp_err_t _method_pause(const app_manager_page_context_t *context)
{
    music_login_method_state_t *state = context->state;
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

static void _method_unmount(const app_manager_page_context_t *context)
{
    music_login_method_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_login_method_ops =
{
    .mount = _method_mount,
    .resume = _method_resume,
    .pause = _method_pause,
    .unmount = _method_unmount,
};

const app_manager_page_definition_t music_login_method_page_definition =
{
    .ops = &s_login_method_ops,
    .memory_size = sizeof(music_login_method_state_t),
};
