#define DBG_TAG "settings_bluetooth"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"
#include "apps_device_link_window.h"
#include "event_bus.h"

#define SETTINGS_BT_WINDOW_TOTAL_MS APPS_DEVICE_LINK_WINDOW_TOTAL_MS
#define SETTINGS_BT_SET_TIMEOUT_MS  1500U

typedef struct settings_bluetooth_state
{
    app_ui_page_t page;
    lv_obj_t *enable_switch;
    lv_obj_t *status_value;
    lv_obj_t *detail_value;
    lv_obj_t *pair_command;
    lv_obj_t *ring_row;
    lv_obj_t *ring;
    lv_obj_t *passkey_label;
    lv_obj_t *confirm_row;
    lv_obj_t *unbind_action;
    lv_obj_t *unbind_status;
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t subscription;
    device_link_confirmation_token_t token;
    uint64_t rendered_generation;
    bool unbind_armed;
} settings_bluetooth_state_t;

_Static_assert(sizeof(settings_bluetooth_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Bluetooth page state exceeds the lifecycle arena slot");

static void _bluetooth_scrub_confirmation(settings_bluetooth_state_t *state)
{
    state->token = 0U;
    if (state->passkey_label != NULL)
    {
        lv_label_set_text(state->passkey_label, "");
        lv_obj_add_flag(state->passkey_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->passkey_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (state->confirm_row != NULL)
    {
        lv_obj_add_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
    }
}

static const char *_bluetooth_status_text(
    const device_link_service_status_t *status)
{
    if (!status->available || !status->enabled)
    {
        return "已关闭";
    }
    if (status->transitioning)
    {
        return "切换中";
    }
    if (status->bound)
    {
        return "已绑定";
    }
    if (status->active)
    {
        return "配对窗口开启";
    }
    return "未绑定";
}

static void _bluetooth_render(settings_bluetooth_state_t *state,
                              const device_link_service_status_t *status)
{
    if (lv_obj_has_state(state->enable_switch, LV_STATE_CHECKED) !=
            status->enabled)
    {
        if (status->enabled)
        {
            lv_obj_add_state(state->enable_switch, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(state->enable_switch, LV_STATE_CHECKED);
        }
    }
    app_ui_set_status_text(state->status_value,
                           _bluetooth_status_text(status),
                           status->bound ? APP_UI_STATUS_SUCCESS :
                           (status->active ? APP_UI_STATUS_ACCENT :
                            APP_UI_STATUS_NEUTRAL));

    char detail[48];
    if (status->active)
    {
        const uint32_t seconds = status->window_remaining_ms / 1000U;
        (void)snprintf(detail, sizeof(detail),
                       status->client_connected ? "手机已连接 · 剩余 %u:%02u" :
                       "等待手机连接 · 剩余 %u:%02u",
                       (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
    }
    else
    {
        (void)snprintf(detail, sizeof(detail), "%s",
                       status->client_connected ? "手机已连接" :
                       (status->bound ? "手机可随时连接" : "尚无配对"));
    }
    lv_label_set_text(state->detail_value, detail);

    if (status->active && status->enabled && !status->pending_confirmation)
    {
        lv_obj_remove_flag(state->ring_row, LV_OBJ_FLAG_HIDDEN);
        uint32_t clamped = status->window_remaining_ms;
        if (clamped > SETTINGS_BT_WINDOW_TOTAL_MS)
        {
            clamped = SETTINGS_BT_WINDOW_TOTAL_MS;
        }
        const uint32_t span = 360U * clamped / SETTINGS_BT_WINDOW_TOTAL_MS;
        lv_obj_set_style_arc_opa(state->ring,
                                 span > 0U ? LV_OPA_COVER : LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        if (span > 0U)
        {
            lv_arc_set_angles(state->ring, 0, (lv_value_precise_t)span);
        }
    }
    else
    {
        lv_obj_add_flag(state->ring_row, LV_OBJ_FLAG_HIDDEN);
    }

    if (status->pending_confirmation && status->confirmation_token != 0U)
    {
        char passkey[8];
        state->token = status->confirmation_token;
        (void)snprintf(passkey, sizeof(passkey), "%06u",
                       (unsigned)(status->numeric_comparison % 1000000U));
        lv_label_set_text(state->passkey_label, passkey);
        lv_obj_remove_flag(state->passkey_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        _bluetooth_scrub_confirmation(state);
    }

    if (status->enabled && !status->transitioning)
    {
        lv_obj_remove_state(state->pair_command, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->pair_command, LV_STATE_DISABLED);
    }
}

static void _bluetooth_refresh(settings_bluetooth_state_t *state)
{
    device_link_service_status_t status;
    if (device_link_service_get_status(&status) == ESP_OK)
    {
        state->rendered_generation = status.generation;
        _bluetooth_render(state, &status);
    }
}

static void _bluetooth_timer(lv_timer_t *timer)
{
    _bluetooth_refresh(lv_timer_get_user_data(timer));
}

static void _bluetooth_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    settings_bluetooth_state_t *state = user_data;
    if (msg_id != DEVICE_LINK_SERVICE_MSG ||
            sub_type != DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS ||
            payload == NULL ||
            payload_size != sizeof(device_link_service_status_t) ||
            state->page.root == NULL)
    {
        return;
    }
    const device_link_service_status_t *status = payload;
    if (status->generation <= state->rendered_generation)
    {
        return;
    }
    state->rendered_generation = status->generation;
    _bluetooth_render(state, status);
}

static void _bluetooth_enable_event(lv_event_t *event)
{
    settings_bluetooth_state_t *state = lv_event_get_user_data(event);
    const bool enable = lv_obj_has_state(state->enable_switch, LV_STATE_CHECKED);
    const esp_err_t result = device_link_service_set_enabled(
                                 enable, SETTINGS_BT_SET_TIMEOUT_MS);
    _bluetooth_refresh(state);
    if (result != ESP_OK)
    {
        /* Written after the refresh so the render cannot overwrite it. */
        app_ui_set_status_text(state->status_value,
                               result == ESP_ERR_TIMEOUT ? "切换中" : "切换失败",
                               result == ESP_ERR_TIMEOUT ? APP_UI_STATUS_ACCENT :
                               APP_UI_STATUS_ERROR);
        LOG_W("bluetooth enable failed: %s", esp_err_to_name(result));
    }
}

static void _bluetooth_pair_event(lv_event_t *event)
{
    settings_bluetooth_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = device_link_service_open_window();
    _bluetooth_refresh(state);
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->detail_value, "无法开启配对窗口",
                               APP_UI_STATUS_ERROR);
    }
}

static void _bluetooth_apply_confirmation(settings_bluetooth_state_t *state,
        bool accept)
{
    const device_link_confirmation_token_t token = state->token;
    if (token == 0U)
    {
        return;
    }
    if (device_link_service_confirm_binding(token, accept) != ESP_OK)
    {
        app_ui_set_status_text(state->detail_value, "确认提交失败，请重试",
                               APP_UI_STATUS_ERROR);
        return;
    }
    _bluetooth_scrub_confirmation(state);
    _bluetooth_refresh(state);
}

static void _bluetooth_confirm_event(lv_event_t *event)
{
    _bluetooth_apply_confirmation(lv_event_get_user_data(event), true);
}

static void _bluetooth_deny_event(lv_event_t *event)
{
    _bluetooth_apply_confirmation(lv_event_get_user_data(event), false);
}

static void _bluetooth_unbind_event(lv_event_t *event)
{
    settings_bluetooth_state_t *state = lv_event_get_user_data(event);
    device_link_service_status_t status;
    if (device_link_service_get_status(&status) != ESP_OK || !status.bound)
    {
        state->unbind_armed = false;
        lv_obj_remove_flag(state->unbind_status, LV_OBJ_FLAG_HIDDEN);
        app_ui_set_status_text(state->unbind_status, "当前没有已绑定的手机",
                               APP_UI_STATUS_NEUTRAL);
        return;
    }
    lv_obj_remove_flag(state->unbind_status, LV_OBJ_FLAG_HIDDEN);
    if (!state->unbind_armed)
    {
        state->unbind_armed = true;
        app_ui_set_status_text(state->unbind_status, "再次点击“解除绑定”以确认",
                               APP_UI_STATUS_WARNING);
        return;
    }
    state->unbind_armed = false;
    const esp_err_t result = device_link_service_revoke_binding();
    app_ui_set_status_text(state->unbind_status,
                           result == ESP_OK ? "已解除绑定" : "解绑失败",
                           result == ESP_OK ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_ERROR);
    _bluetooth_refresh(state);
}

static void _bluetooth_mount(const app_manager_page_context_t *context)
{
    settings_bluetooth_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "蓝牙", true);
    app_ui_page_set_subtitle(&state->page, "手机绑定");

    (void)app_ui_add_switch_row(state->page.content, "蓝牙开关",
                                "关闭后暂停配对与手机连接",
                                _bluetooth_enable_event, state,
                                &state->enable_switch);
    app_ui_add_value_row(state->page.content, "状态", "读取中",
                         &state->status_value);
    app_ui_add_value_row(state->page.content, "详情", "--",
                         &state->detail_value);
    state->pair_command = app_ui_add_command(
                              state->page.content, LV_SYMBOL_BLUETOOTH, "配对手机",
                              "开启绑定窗口，用手机发起",
                              _bluetooth_pair_event, state);

    lv_obj_t *ring_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(ring_row);
    lv_obj_set_width(ring_row, LV_PCT(100));
    lv_obj_set_height(ring_row, 96);
    lv_obj_set_flex_flow(ring_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ring_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(ring_row, false);
    state->ring = app_ui_ring_create(ring_row, 96, 8, APP_UI_COLOR_SURFACE_HI);
    lv_obj_add_flag(ring_row, LV_OBJ_FLAG_HIDDEN);
    state->ring_row = ring_row;

    state->passkey_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->passkey_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->passkey_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->passkey_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_label_set_text(state->passkey_label, "");

    state->confirm_row = app_ui_button_row_create(state->page.content, 44);
    (void)app_ui_button_create(state->confirm_row, "确认",
                               _bluetooth_confirm_event, state);
    (void)app_ui_button_create(state->confirm_row, "拒绝",
                               _bluetooth_deny_event, state);
    lv_obj_add_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);

    state->unbind_action = app_ui_add_danger_action(
                               state->page.content, LV_SYMBOL_TRASH, "解除绑定",
                               "清除与当前手机的配对", _bluetooth_unbind_event,
                               state);
    state->unbind_status = app_ui_add_body_label(state->page.content, " ");
    lv_obj_add_flag(state->unbind_status, LV_OBJ_FLAG_HIDDEN);

    state->refresh_timer = lv_timer_create(_bluetooth_timer, 1000U, state);
    _bluetooth_refresh(state);
}

static void _bluetooth_resume(const app_manager_page_context_t *context)
{
    settings_bluetooth_state_t *state = context->state;
    if (state->subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        if (event_bus_subscribe(DEVICE_LINK_SERVICE_MSG,
                                DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS,
                                _bluetooth_event, state, EVENT_BUS_DISPATCH_UI,
                                &state->subscription) != ESP_OK)
        {
            state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    state->rendered_generation = 0U;
    _bluetooth_refresh(state);
}

static esp_err_t _bluetooth_pause(const app_manager_page_context_t *context)
{
    settings_bluetooth_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        const esp_err_t result = event_bus_unsubscribe(state->subscription);
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
    }
    return ESP_OK;
}

static void _bluetooth_unmount(const app_manager_page_context_t *context)
{
    settings_bluetooth_state_t *state = context->state;
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_unsubscribe(state->subscription);
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->enable_switch = NULL;
    state->status_value = NULL;
    state->detail_value = NULL;
    state->pair_command = NULL;
    state->ring_row = NULL;
    state->ring = NULL;
    state->passkey_label = NULL;
    state->confirm_row = NULL;
    state->unbind_action = NULL;
    state->unbind_status = NULL;
}

static const app_manager_page_ops_t s_settings_bluetooth_ops =
{
    .mount = _bluetooth_mount,
    .resume = _bluetooth_resume,
    .pause = _bluetooth_pause,
    .unmount = _bluetooth_unmount,
};

const app_manager_page_definition_t settings_bluetooth_page_definition =
{
    .ops = &s_settings_bluetooth_ops,
    .memory_size = sizeof(settings_bluetooth_state_t),
};
