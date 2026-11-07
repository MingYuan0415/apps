#include "music_app_internal.h"

#include "event_bus.h"

#include <string.h>

#define MUSIC_START_MIN_DWELL_MS 1000U

typedef struct music_start_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *retry_button;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    uint32_t started_ms;
    uint32_t routed_ms;
    bool routed;
} music_start_state_t;

_Static_assert(sizeof(music_start_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music start state exceeds the page arena");

static void _start_set_status(music_start_state_t *state, const char *text,
                              uint32_t color)
{
    if (state->status_label == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(state->status_label, text != NULL ? text : "");
    lv_obj_set_style_text_color(state->status_label, lv_color_hex(color), 0);
}

static void _start_set_retry(music_start_state_t *state, bool visible)
{
    if (state->retry_button != NULL)
    {
        lv_obj_set_hidden(state->retry_button, !visible);
    }
}

static void _start_check(music_start_state_t *state)
{
    music_service_status_t status;
    if (music_service_get_status(&status) != ESP_OK)
    {
        _start_set_status(state, "音乐服务暂不可用", APP_UI_COLOR_SUN);
        _start_set_retry(state, true);
        return;
    }
    if (state->routed &&
            (uint32_t)(lv_tick_get() - state->routed_ms) >= 2000U &&
            app_page_is_actived(APP_MANAGER_ID_MUSIC, MUSIC_PAGE_START))
    {
        state->routed = false;
    }

    if (status.state == MUSIC_SERVICE_STATE_READY && status.logged_in)
    {
        if ((uint32_t)(lv_tick_get() - state->started_ms) <
                MUSIC_START_MIN_DWELL_MS)
        {
            return;
        }
        if (!state->routed)
        {
            state->routed = true;
            state->routed_ms = lv_tick_get();
            (void)music_ui_enter_player();
        }
        return;
    }

    if (status.state == MUSIC_SERVICE_STATE_QR_READY ||
            status.state == MUSIC_SERVICE_STATE_QR_SCANNED ||
            status.state == MUSIC_SERVICE_STATE_QR_EXPIRED ||
            status.state == MUSIC_SERVICE_STATE_AUTHENTICATING)
    {
        if ((uint32_t)(lv_tick_get() - state->started_ms) <
                MUSIC_START_MIN_DWELL_MS)
        {
            return;
        }
        if (!state->routed)
        {
            state->routed = true;
            state->routed_ms = lv_tick_get();
            (void)music_ui_enter_login_qr(status.login_type);
        }
        return;
    }

    if (status.state == MUSIC_SERVICE_STATE_LOGGED_OUT ||
            music_ui_login_required(&status) ||
            music_ui_storage_failure(&status))
    {
        if ((uint32_t)(lv_tick_get() - state->started_ms) <
                MUSIC_START_MIN_DWELL_MS)
        {
            return;
        }
        if (!state->routed)
        {
            state->routed = true;
            state->routed_ms = lv_tick_get();
            (void)music_ui_reset_to_login();
        }
        return;
    }

    if (status.state == MUSIC_SERVICE_STATE_WAITING_NETWORK)
    {
        _start_set_status(state, "正在连接网络…", APP_UI_COLOR_MUTED);
        _start_set_retry(state, false);
        return;
    }
    if (status.state == MUSIC_SERVICE_STATE_WAITING_TIME)
    {
        _start_set_status(state, "正在同步时间…", APP_UI_COLOR_MUTED);
        _start_set_retry(state, false);
        return;
    }
    if (music_ui_validation_pending(&status))
    {
        _start_set_status(state, "正在验证登录状态…", APP_UI_COLOR_MUTED);
        _start_set_retry(state, false);
        return;
    }
    if (status.state == MUSIC_SERVICE_STATE_ERROR ||
            status.failure != MUSIC_SERVICE_FAILURE_NONE)
    {
        _start_set_status(state,
                          status.failure != MUSIC_SERVICE_FAILURE_NONE ?
                          music_ui_failure_text(status.failure) :
                          "音乐服务暂不可用",
                          APP_UI_COLOR_SUN);
        _start_set_retry(state, true);
        return;
    }

    _start_set_status(state, "正在准备音乐…", APP_UI_COLOR_MUTED);
    _start_set_retry(state, false);
}

static void _start_retry_clicked(lv_event_t *event)
{
    music_start_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        (void)music_service_retry();
        state->routed = false;
        _start_check(state);
    }
}

static void _start_close_clicked(lv_event_t *event)
{
    (void)event;
    app_ui_request_back();
}

static void _start_timer_cb(lv_timer_t *timer)
{
    music_start_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL)
    {
        _start_check(state);
    }
}

static void _start_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                            const void *payload, size_t payload_size,
                            void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_start_state_t *state = user_data;
    if (state != NULL)
    {
        _start_check(state);
    }
}

static void _start_mount(const app_manager_page_context_t *context)
{
    music_start_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create_home(&state->page);
    lv_obj_set_style_pad_all(state->page.content, 0, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_flex_flow(state->page.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->page.content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(state->page.content, 12, 0);

    lv_obj_t *close_button = music_ui_plain_icon_button(
                                 state->page.content, APP_IMAGE_UI_CLOSE,
                                 LV_SYMBOL_CLOSE, MUSIC_UI_CONTROL_SIZE, 26,
                                 APP_UI_COLOR_MUTED, _start_close_clicked,
                                 state);
    if (close_button != NULL)
    {
        lv_obj_set_ignore_layout(close_button, true);
        lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -4, 4);
    }

    lv_obj_t *icon = music_ui_image(state->page.content,
                                    APP_IMAGE_MUSIC_ICON, LV_SYMBOL_AUDIO,
                                    96);
    if (icon != NULL)
    {
        app_ui_icon_set_color(icon, APP_UI_COLOR_RAIN);
    }

    lv_obj_t *title = app_ui_add_section(state->page.content, "QQ 音乐");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_pad_top(title, 0, 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    state->status_label = app_ui_add_body_label(state->page.content,
        "正在启动音乐服务…");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_align(state->status_label,
                                LV_TEXT_ALIGN_CENTER, 0);

    state->retry_button = music_ui_plain_text_button(
                              state->page.content, "重新检查",
                              MUSIC_UI_CONTROL_SIZE, APP_THEME_FONT_BODY,
                              APP_UI_COLOR_RAIN, _start_retry_clicked, state);
    if (state->retry_button != NULL)
    {
        lv_obj_set_width(state->retry_button, 160);
        lv_obj_set_hidden(state->retry_button, true);
    }
}

static void _start_resume(const app_manager_page_context_t *context)
{
    music_start_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->started_ms = lv_tick_get();
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _start_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_start_timer_cb, 500U, state);
    _start_check(state);
}

static esp_err_t _start_pause(const app_manager_page_context_t *context)
{
    music_start_state_t *state = context->state;
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

static void _start_unmount(const app_manager_page_context_t *context)
{
    music_start_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_start_ops =
{
    .mount = _start_mount,
    .resume = _start_resume,
    .pause = _start_pause,
    .unmount = _start_unmount,
};

const app_manager_page_definition_t music_start_page_definition =
{
    .ops = &s_start_ops,
    .memory_size = sizeof(music_start_state_t),
};
