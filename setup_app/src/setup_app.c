#define DBG_TAG "setup_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "apps_device_link_window.h"
#include "device_link_service.h"
#include "setup_wifi_adapter.h"
#include "onboarding_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETUP_PAGE_PROVISIONING "provisioning"
#define SETUP_WINDOW_TOTAL_MS APPS_DEVICE_LINK_WINDOW_TOTAL_MS

typedef struct setup_root_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *detail_label;
    lv_obj_t *controls;
    lv_obj_t *wifi_arcs[3];
    lv_obj_t *wifi_dot;
    setup_wifi_adapter_t wifi;
    event_bus_sub_handle_t device_link_subscription;
    connectivity_manager_status_snapshot_t connectivity;
    device_link_service_status_t device_link;
    setup_wifi_operation_kind_t completed_operation;
    esp_err_t completed_result;
    uint64_t device_link_generation;
    bool connectivity_valid;
    bool device_link_valid;
    bool revoke_armed;
    onboarding_service_state_t onboarding_state;
} setup_root_state_t;

typedef struct setup_provisioning_state
{
    app_ui_page_t page;
    lv_obj_t *passkey_label;
    lv_obj_t *device_label;
    lv_obj_t *status_label;
    lv_obj_t *remaining_label;
    lv_obj_t *confirm_row;
    lv_obj_t *confirm_button;
    lv_obj_t *deny_button;
    lv_obj_t *window_ring;
    event_bus_sub_handle_t subscription;
    uint64_t device_link_generation;
    device_link_confirmation_token_t confirmation_token;
    bool window_held;
} setup_provisioning_state_t;

_Static_assert(sizeof(setup_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Setup root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(setup_provisioning_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Setup provisioning state exceeds the lifecycle arena slot");

static void _setup_root_render(setup_root_state_t *state);
static esp_err_t _setup_root_pause(setup_root_state_t *state);

static const char *_setup_failure_detail(
    connectivity_manager_failure_t failure)
{
    switch (failure)
    {
    case CONNECTIVITY_MANAGER_FAILURE_AUTHENTICATION:
        return "密码错误或认证失败";
    case CONNECTIVITY_MANAGER_FAILURE_AP_NOT_FOUND:
        return "未找到接入点";
    case CONNECTIVITY_MANAGER_FAILURE_ASSOCIATION_TIMEOUT:
        return "连接接入点超时";
    case CONNECTIVITY_MANAGER_FAILURE_DHCP_TIMEOUT:
        return "获取网络地址超时";
    case CONNECTIVITY_MANAGER_FAILURE_LINK_LOST:
        return "网络连接已断开";
    case CONNECTIVITY_MANAGER_FAILURE_RADIO_UNAVAILABLE:
        return "Wi-Fi 射频不可用";
    case CONNECTIVITY_MANAGER_FAILURE_STORAGE:
        return "保存网络配置失败";
    case CONNECTIVITY_MANAGER_FAILURE_INTERNAL:
        return "Wi-Fi 内部错误";
    case CONNECTIVITY_MANAGER_FAILURE_NONE:
    default:
        return "操作未能完成";
    }
}

static const char *_setup_command_error(esp_err_t result)
{
    switch (result)
    {
    case ESP_ERR_INVALID_STATE:
        return "当前操作尚未完成";
    case ESP_ERR_NO_MEM:
        return "请求队列已满";
    case ESP_ERR_NOT_FOUND:
        return "没有可用的保存网络";
    default:
        return "请求未能提交";
    }
}

static void _setup_set_status(setup_root_state_t *state, const char *title,
                              const char *detail)
{
    lv_label_set_text(state->status_label, title);
    lv_label_set_text(state->detail_label, detail);
}

static lv_obj_t *_setup_add_command(setup_root_state_t *state,
                                    const char *symbol, const char *title,
                                    const char *subtitle,
                                    lv_event_cb_t callback)
{
    lv_obj_t *command = app_ui_add_command(
                            state->controls, symbol, title, subtitle,
                            callback, state);
    if (state->device_link_valid && state->device_link.active)
    {
        lv_obj_add_state(command, LV_STATE_DISABLED);
    }
    return command;
}

static void _setup_cancel_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = setup_wifi_adapter_cancel(&state->wifi);
    _setup_set_status(state, result == ESP_OK ? "正在取消" : "取消失败",
                      result == ESP_OK ? "等待网络操作结束" :
                      _setup_command_error(result));
}

static void _setup_disconnect_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = setup_wifi_adapter_disconnect(&state->wifi);
    _setup_set_status(state, result == ESP_OK ? "正在断开" : "断开未开始",
                      result == ESP_OK ? "正在清理当前连接" :
                      _setup_command_error(result));
    _setup_root_render(state);
}

static void _setup_reconnect_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = setup_wifi_adapter_reconnect_saved(&state->wifi);
    _setup_set_status(state, result == ESP_OK ? "正在连接" :
                      "重新连接未开始",
                      result == ESP_OK ? "使用已保存的网络配置" :
                      _setup_command_error(result));
    _setup_root_render(state);
}

static void _setup_forget_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    const esp_err_t result = setup_wifi_adapter_forget(&state->wifi);
    _setup_set_status(state, result == ESP_OK ? "正在忘记网络" :
                      "忘记网络未开始",
                      result == ESP_OK ? "删除保存配置并断开连接" :
                      _setup_command_error(result));
    _setup_root_render(state);
}

static void _setup_auto_connect_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    lv_obj_t *toggle = lv_event_get_target_obj(event);
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    const esp_err_t result = setup_wifi_adapter_set_auto_connect(
                                 &state->wifi, enabled);
    if (result != ESP_OK)
    {
        if (state->connectivity.auto_connect)
        {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        }
        _setup_set_status(state, "自动连接未更新",
                          _setup_command_error(result));
    }
}

static void _setup_open_provisioning_event(lv_event_t *event)
{
    (void)event;
    app_ui_request_open_page(APP_MANAGER_ID_SETUP,
                             SETUP_PAGE_PROVISIONING);
}

static void _setup_revoke_binding_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);

    if (!state->revoke_armed)
    {
        state->revoke_armed = true;
        _setup_set_status(state, "再次点击“解除绑定”以确认", "");
        return;
    }
    state->revoke_armed = false;
    const esp_err_t result = device_link_service_revoke_binding();

    if (result != ESP_OK)
    {
        _setup_set_status(state, "解除绑定失败",
                          _setup_command_error(result));
    }
}

static void _setup_finish_event(lv_event_t *event)
{
    setup_root_state_t *state = lv_event_get_user_data(event);
    if (state->onboarding_state == ONBOARDING_SERVICE_COMPLETED)
    {
        app_ui_request_run(APP_MANAGER_ID_HOME);
        return;
    }
    esp_err_t result = state->connectivity.state ==
                       CONNECTIVITY_MANAGER_STATE_IP_READY ?
                       onboarding_service_complete() :
                       onboarding_service_defer();
    if (result == ESP_OK)
    {
        app_ui_request_run(APP_MANAGER_ID_HOME);
    }
    else
    {
        _setup_set_status(state, "设置状态未保存",
                          _setup_command_error(result));
    }
}

static void _setup_render_auto_connect(setup_root_state_t *state)
{
    lv_obj_t *row = lv_obj_create(state->controls);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(row, false);
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(label, "自动连接");
    lv_obj_t *toggle = lv_switch_create(row);
    app_ui_click_only(toggle);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              0);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(toggle, lv_color_hex(APP_UI_COLOR_TEXT),
                              LV_PART_KNOB);
    if (state->connectivity.auto_connect)
    {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    if (state->device_link_valid && state->device_link.active)
    {
        lv_obj_add_state(toggle, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(toggle, _setup_auto_connect_event,
                        LV_EVENT_VALUE_CHANGED, state);
}

static void _setup_render_management(setup_root_state_t *state)
{
    const connectivity_manager_status_snapshot_t *status =
        &state->connectivity;
    if (status->state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        (void)_setup_add_command(state, LV_SYMBOL_CLOSE, "断开连接",
                                 "本次启动保持离线",
                                 _setup_disconnect_event);
    }
    else if (status->saved_profile)
    {
        (void)_setup_add_command(state, LV_SYMBOL_LOOP, "重新连接",
                                 "连接已保存的网络",
                                 _setup_reconnect_event);
    }
    if (status->saved_profile)
    {
        _setup_render_auto_connect(state);
        (void)_setup_add_command(state, LV_SYMBOL_TRASH, "忘记网络",
                                 "删除保存配置并断开连接",
                                 _setup_forget_event);
    }
}

static void _setup_wifi_glyph(lv_obj_t *parent, lv_obj_t **arcs,
                              lv_obj_t **dot)
{
    static const int32_t sizes[3] = { 56, 40, 24 };
    lv_obj_t *glyph = lv_obj_create(parent);
    lv_obj_remove_style_all(glyph);
    lv_obj_set_size(glyph, 56, 44);
    app_ui_make_passive(glyph, false);
    lv_obj_add_flag(glyph, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (size_t index = 0U; index < 3U; ++index)
    {
        const int32_t size = sizes[index];
        lv_obj_t *arc = lv_arc_create(glyph);
        lv_obj_set_size(arc, size, size);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(APP_UI_COLOR_MUTED),
                                   LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_arc_set_angles(arc, 225, 315);
        lv_obj_set_pos(arc, 28 - size / 2, 36 - size / 2);
        app_ui_make_passive(arc, false);
        arcs[index] = arc;
    }
    *dot = lv_obj_create(glyph);
    lv_obj_remove_style_all(*dot);
    lv_obj_set_size(*dot, 6, 6);
    lv_obj_set_style_bg_color(*dot, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(*dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(*dot, LV_RADIUS_CIRCLE, 0);
    app_ui_make_passive(*dot, false);
    lv_obj_set_pos(*dot, 25, 33);
}

static void _setup_wifi_glyph_color(lv_obj_t **arcs, lv_obj_t *dot,
                                    uint32_t color)
{
    for (size_t index = 0U; index < 3U; ++index)
    {
        lv_obj_set_style_arc_color(arcs[index], lv_color_hex(color),
                                   LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
}

static void _setup_root_render(setup_root_state_t *state)
{
    if (state->controls == NULL || !state->connectivity_valid)
    {
        return;
    }
    lv_obj_clean(state->controls);
    const connectivity_manager_status_snapshot_t *status =
        &state->connectivity;
    if (setup_wifi_adapter_has_operation(&state->wifi))
    {
        _setup_set_status(state, "网络操作进行中", status->ssid);
        (void)_setup_add_command(state, LV_SYMBOL_CLOSE, "取消",
                                 "停止本次网络操作", _setup_cancel_event);
    }
    else if (state->completed_operation != SETUP_WIFI_OPERATION_NONE)
    {
        if (state->completed_result == ESP_ERR_NOT_FINISHED)
        {
            _setup_set_status(state, "操作已取消", status->ssid);
        }
        else if (state->completed_result != ESP_OK)
        {
            _setup_set_status(state, "网络操作失败",
                              _setup_failure_detail(status->failure));
        }
        else
        {
            switch (state->completed_operation)
            {
            case SETUP_WIFI_OPERATION_DISCONNECT:
                _setup_set_status(state, "已断开连接", status->ssid);
                break;
            case SETUP_WIFI_OPERATION_RECONNECT:
                _setup_set_status(state, "已连接", status->ssid);
                break;
            case SETUP_WIFI_OPERATION_FORGET:
                _setup_set_status(state, "已忘记网络", "");
                break;
            case SETUP_WIFI_OPERATION_POLICY:
                _setup_set_status(
                    state,
                    status->auto_connect ? "自动连接已启用" :
                    "自动连接已关闭",
                    status->ssid);
                break;
            case SETUP_WIFI_OPERATION_NONE:
            default:
                break;
            }
        }
        state->completed_operation = SETUP_WIFI_OPERATION_NONE;
        _setup_render_management(state);
    }
    else
    {
        switch (status->state)
        {
        case CONNECTIVITY_MANAGER_STATE_OFFLINE:
            _setup_set_status(state, "Wi-Fi 不可用",
                              _setup_failure_detail(status->failure));
            break;
        case CONNECTIVITY_MANAGER_STATE_CONNECTING:
            _setup_set_status(state, "正在连接", status->ssid);
            break;
        case CONNECTIVITY_MANAGER_STATE_WAITING_IP:
            _setup_set_status(state, "正在获取地址", status->ssid);
            break;
        case CONNECTIVITY_MANAGER_STATE_IP_READY:
            _setup_set_status(state,
                              status->profile_persisted ? "已连接" :
                              "已连接，但未保存",
                              status->profile_persisted ? status->ssid :
                              _setup_failure_detail(status->failure));
            break;
        case CONNECTIVITY_MANAGER_STATE_RETRY_WAIT:
            _setup_set_status(state, "等待重新连接", status->ssid);
            break;
        case CONNECTIVITY_MANAGER_STATE_SUSPENDED:
            _setup_set_status(state, "Wi-Fi 已暂停", "系统正在待机");
            break;
        case CONNECTIVITY_MANAGER_STATE_SCANNING:
            _setup_set_status(state, "手机正在扫描", "等待配网请求完成");
            break;
        case CONNECTIVITY_MANAGER_STATE_IDLE:
        default:
            _setup_set_status(state, status->saved_profile ? "已保存网络" :
                              "当前未连接",
                              status->saved_profile ? status->ssid :
                              "请使用手机配置新网络");
            break;
        }
        _setup_render_management(state);
    }

    const bool transport_fault = state->device_link_valid &&
                                 state->device_link.active &&
                                 state->device_link.state ==
                                 DEVICE_LINK_SERVICE_STATE_ERROR;
    const bool bound = state->device_link_valid && state->device_link.bound;

    if (bound)
    {
        (void)app_ui_add_action(state->controls, LV_SYMBOL_BLUETOOTH,
                                "解除绑定",
                                transport_fault ?
                                "蓝牙关闭失败，需要重启" :
                                "清除本机保存的手机绑定",
                                _setup_revoke_binding_event, state);
    }
    else
    {
        (void)app_ui_add_action(state->controls, LV_SYMBOL_BLUETOOTH,
                                "手机绑定",
                                transport_fault ?
                                "蓝牙关闭失败，需要重启" :
                                state->device_link_valid &&
                                state->device_link.active ?
                                "绑定窗口正在运行" :
                                "开启 2 分钟绑定窗口",
                                _setup_open_provisioning_event, state);
    }
    if (transport_fault)
    {
        lv_label_set_text(state->detail_label,
                          "蓝牙关闭失败，需要重启");
    }
    else if (state->device_link_valid && state->device_link.active)
    {
        lv_label_set_text(state->detail_label,
                          "手机绑定进行中，本机网络管理已锁定");
    }
    uint32_t glyph_color = APP_UI_COLOR_MUTED;
    if (status->state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        glyph_color = APP_UI_COLOR_RAIN;
    }
    else if (status->state == CONNECTIVITY_MANAGER_STATE_CONNECTING ||
             status->state == CONNECTIVITY_MANAGER_STATE_WAITING_IP ||
             status->state == CONNECTIVITY_MANAGER_STATE_SCANNING)
    {
        glyph_color = APP_UI_COLOR_SUN;
    }
    _setup_wifi_glyph_color(state->wifi_arcs, state->wifi_dot, glyph_color);
    if (status->state == CONNECTIVITY_MANAGER_STATE_IP_READY)
    {
        if (state->onboarding_state != ONBOARDING_SERVICE_COMPLETED)
        {
            (void)app_ui_add_command(state->controls, LV_SYMBOL_OK, "完成设置",
                                     "保存引导状态并进入主页",
                                     _setup_finish_event, state);
        }
    }
    else if (state->onboarding_state != ONBOARDING_SERVICE_COMPLETED)
    {
        (void)app_ui_add_command(state->controls, LV_SYMBOL_RIGHT, "稍后设置",
                                 "离线功能仍可使用",
                                 _setup_finish_event, state);
    }
}

static void _setup_wifi_status(
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind,
    void *user_data)
{
    setup_root_state_t *state = user_data;
    state->connectivity = *snapshot;
    state->connectivity_valid = true;
    if (scope == SETUP_WIFI_STATUS_OPERATION && snapshot->operation_complete)
    {
        state->completed_operation = operation_kind;
        state->completed_result = snapshot->last_error;
    }
    _setup_root_render(state);
}

static void _setup_root_provisioning_event(
    event_bus_msg_id_t message_id, uint32_t subtype,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_root_state_t *state = user_data;
    if (message_id != DEVICE_LINK_SERVICE_MSG ||
            subtype != DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS ||
            payload == NULL ||
            payload_size != sizeof(device_link_service_status_t))
    {
        return;
    }
    const device_link_service_status_t *status = payload;

    if (status->generation <= state->device_link_generation)
    {
        return;
    }
    state->device_link_generation = status->generation;
    memcpy(&state->device_link, payload, sizeof(state->device_link));
    state->device_link_valid = true;
    _setup_root_render(state);
}

static void _setup_root_mount(setup_root_state_t *state)
{
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "网络设置", true);
    app_ui_page_set_subtitle(&state->page, "Wi-Fi 与手机绑定");

    lv_obj_t *card = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_column(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(card, false);
    _setup_wifi_glyph(card, state->wifi_arcs, &state->wifi_dot);

    lv_obj_t *text = lv_obj_create(card);
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    app_ui_make_passive(text, false);
    state->status_label = lv_label_create(text);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_label_set_long_mode(state->status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state->status_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_label_set_text(state->status_label, "正在加载");
    state->detail_label = lv_label_create(text);
    lv_obj_set_width(state->detail_label, LV_PCT(100));
    lv_label_set_long_mode(state->detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state->detail_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->detail_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->detail_label, "正在读取网络状态");
    state->controls = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->controls);
    lv_obj_set_width(state->controls, LV_PCT(100));
    lv_obj_set_height(state->controls, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(state->controls, 8, 0);
    lv_obj_set_flex_flow(state->controls, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(state->controls, false);
}

static esp_err_t _setup_root_resume(setup_root_state_t *state)
{
    (void)onboarding_service_get_state(&state->onboarding_state);
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _setup_wifi_status,
    };
    esp_err_t result = setup_wifi_adapter_open(&state->wifi, &callbacks,
                       state);
    if (result == ESP_OK)
    {
        result = event_bus_subscribe(
                     DEVICE_LINK_SERVICE_MSG,
                     DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS,
                     _setup_root_provisioning_event, state,
                     EVENT_BUS_DISPATCH_UI,
                     &state->device_link_subscription);
    }
    if (result == ESP_OK)
    {
        result = device_link_service_get_status(&state->device_link);
        state->device_link_valid = result == ESP_OK;
        if (result == ESP_OK)
        {
            state->device_link_generation = state->device_link.generation;
        }
    }
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = _setup_root_pause(state);
        state->device_link_valid = false;
        if (cleanup_result != ESP_OK)
        {
            LOG_W("setup resume rollback failed: %s",
                  esp_err_to_name(cleanup_result));
        }
        _setup_set_status(state, "网络设置不可用",
                          _setup_command_error(result));
    }
    else
    {
        _setup_root_render(state);
    }
    return result;
}

static esp_err_t _setup_root_pause(setup_root_state_t *state)
{
    esp_err_t result = ESP_OK;
    if (state->device_link_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        result = event_bus_unsubscribe(state->device_link_subscription);
        state->device_link_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        if (result == ESP_ERR_NOT_FOUND)
        {
            result = ESP_OK;
        }
    }
    const esp_err_t close_result = setup_wifi_adapter_close(&state->wifi);

    return result != ESP_OK ? result : close_result;
}

static void _setup_root_mount_op(const app_manager_page_context_t *context)
{
    _setup_root_mount(context->state);
}

static void _setup_root_resume_op(const app_manager_page_context_t *context)
{
    (void)_setup_root_resume(context->state);
}

static esp_err_t _setup_root_pause_op(const app_manager_page_context_t *context)
{
    return _setup_root_pause(context->state);
}

static void _setup_root_unmount(const app_manager_page_context_t *context)
{
    setup_root_state_t *state = context->state;
    /* Back-stop release: ignore errors so a half-failed pause cannot orphan
     * a subscription whose user_data points at this arena slot. */
    if (state->device_link_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_unsubscribe(state->device_link_subscription);
        state->device_link_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
    (void)setup_wifi_adapter_close(&state->wifi);
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->detail_label = NULL;
    state->controls = NULL;
    for (size_t index = 0U; index < 3U; ++index)
    {
        state->wifi_arcs[index] = NULL;
    }
    state->wifi_dot = NULL;
}

static void _setup_provisioning_scrub(setup_provisioning_state_t *state)
{
    if (state->passkey_label != NULL)
    {
        lv_label_set_text(state->passkey_label, "");
    }
    state->confirmation_token = 0U;
    if (state->confirm_row != NULL)
    {
        lv_obj_add_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _setup_provisioning_render(
    setup_provisioning_state_t *state,
    const device_link_service_status_t *status)
{
    lv_label_set_text(state->device_label, "MT");
    if (status->state == DEVICE_LINK_SERVICE_STATE_ERROR)
    {
        _setup_provisioning_scrub(state);
        lv_label_set_text(state->status_label,
                          status->active ? "蓝牙关闭失败，需要重启" :
                          "绑定服务发生错误");
        lv_label_set_text(state->remaining_label, "");
        lv_arc_set_angles(state->window_ring, 0, 0);
        lv_obj_set_style_arc_opa(state->window_ring, LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        return;
    }
    if (!status->active)
    {
        _setup_provisioning_scrub(state);
        lv_label_set_text(state->status_label, "绑定窗口已关闭");
        lv_label_set_text(state->remaining_label, "");
        lv_arc_set_angles(state->window_ring, 0, 0);
        lv_obj_set_style_arc_opa(state->window_ring, LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        return;
    }
    if (status->pending_confirmation && status->confirmation_token != 0U)
    {
        char passkey[8];

        state->confirmation_token = status->confirmation_token;
        lv_obj_remove_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
        (void)snprintf(passkey, sizeof(passkey), "%06u",
                       (unsigned)(status->numeric_comparison % 1000000U));
        if (state->passkey_label != NULL)
        {
            lv_label_set_text(state->passkey_label, passkey);
        }
        lv_label_set_text(state->status_label, "核对手机上的数字后确认");
    }
    else
    {
        state->confirmation_token = 0U;
        lv_obj_add_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
        if (state->passkey_label != NULL)
        {
            lv_label_set_text(state->passkey_label, "");
        }
        lv_label_set_text(state->status_label,
                          status->client_connected ? "手机已连接，等待配对" :
                          "等待手机连接");
    }
    char remaining[48];
    const uint32_t seconds = status->window_remaining_ms / 1000U;
    (void)snprintf(remaining, sizeof(remaining), "剩余 %u:%02u",
                   (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
    lv_label_set_text(state->remaining_label, remaining);

    uint32_t clamped = status->window_remaining_ms;
    if (clamped > SETUP_WINDOW_TOTAL_MS)
    {
        clamped = SETUP_WINDOW_TOTAL_MS;
    }
    const uint32_t span = status->active ? 360U * clamped /
                          SETUP_WINDOW_TOTAL_MS : 0U;
    lv_obj_set_style_arc_opa(state->window_ring,
                             span > 0U ? LV_OPA_COVER : LV_OPA_TRANSP,
                             LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(state->window_ring,
                               lv_color_hex(APP_UI_COLOR_RAIN),
                               LV_PART_INDICATOR);
    if (span > 0U)
    {
        lv_arc_set_angles(state->window_ring, 0,
                          (lv_value_precise_t)span);
    }
}

static void _setup_provisioning_event(
    event_bus_msg_id_t message_id, uint32_t subtype,
    const void *payload, size_t payload_size, void *user_data)
{
    setup_provisioning_state_t *state = user_data;
    if (message_id == DEVICE_LINK_SERVICE_MSG &&
            subtype == DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS &&
            payload != NULL &&
            payload_size == sizeof(device_link_service_status_t))
    {
        const device_link_service_status_t *status = payload;

        if (status->generation <= state->device_link_generation)
        {
            return;
        }
        state->device_link_generation = status->generation;
        _setup_provisioning_render(state, status);
    }
}

static void _setup_provisioning_apply_confirmation(
    setup_provisioning_state_t *state, bool accept)
{
    const device_link_confirmation_token_t token =
        state->confirmation_token;

    if (token == 0U)
    {
        return;
    }
    const esp_err_t result =
        device_link_service_confirm_binding(token, accept);

    if (result != ESP_OK)
    {
        lv_label_set_text(state->status_label, "确认提交失败，请重试");
        LOG_W("binding confirmation failed: %s", esp_err_to_name(result));
        return;
    }
    _setup_provisioning_scrub(state);
}

static void _setup_provisioning_confirm_event(lv_event_t *event)
{
    _setup_provisioning_apply_confirmation(
        lv_event_get_user_data(event), true);
}

static void _setup_provisioning_deny_event(lv_event_t *event)
{
    _setup_provisioning_apply_confirmation(
        lv_event_get_user_data(event), false);
}

static void _setup_provisioning_mount(setup_provisioning_state_t *state)
{
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "手机绑定", true);
    app_ui_page_set_subtitle(&state->page, "BLE 数字比对");

    lv_obj_t *ring_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(ring_row);
    lv_obj_set_width(ring_row, LV_PCT(100));
    lv_obj_set_height(ring_row, 120);
    lv_obj_set_flex_flow(ring_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ring_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(ring_row, false);
    state->window_ring = app_ui_ring_create(ring_row, 120, 6,
                                            APP_UI_COLOR_SURFACE_HI);

    state->passkey_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->passkey_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->passkey_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->passkey_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(state->passkey_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_label_set_text(state->passkey_label, "");
    state->device_label = lv_label_create(state->window_ring);
    lv_obj_set_width(state->device_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->device_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->device_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(state->device_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_center(state->device_label);
    state->status_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_label_set_long_mode(state->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->status_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    state->remaining_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->remaining_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->remaining_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->remaining_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->remaining_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    /* Binding confirmation row: hidden until a commit is pending. */
    state->confirm_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->confirm_row);
    lv_obj_set_width(state->confirm_row, LV_PCT(100));
    lv_obj_set_height(state->confirm_row, 44);
    lv_obj_set_flex_flow(state->confirm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->confirm_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state->confirm_row, 8, 0);
    app_ui_make_passive(state->confirm_row, false);
    lv_obj_add_flag(state->confirm_row, LV_OBJ_FLAG_HIDDEN);
    state->confirm_button = app_ui_button_create(state->confirm_row,
                            "确认绑定",
                            _setup_provisioning_confirm_event,
                            state);
    lv_obj_set_style_text_color(lv_obj_get_child(state->confirm_button, 0),
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    state->deny_button = app_ui_button_create(state->confirm_row, "拒绝",
                         _setup_provisioning_deny_event,
                         state);
    lv_obj_set_style_text_color(lv_obj_get_child(state->deny_button, 0),
                                lv_color_hex(APP_UI_COLOR_WARNING), 0);
}

static esp_err_t _setup_provisioning_release_foreground(
    setup_provisioning_state_t *state)
{
    esp_err_t result = ESP_OK;

    if (state->window_held)
    {
        result = device_link_service_close_window();
        if (result == ESP_OK || result == ESP_ERR_INVALID_STATE)
        {
            state->window_held = false;
            result = ESP_OK;
        }
    }
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        const esp_err_t unsubscribe_result =
            event_bus_unsubscribe(state->subscription);

        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        if (unsubscribe_result != ESP_OK &&
                unsubscribe_result != ESP_ERR_NOT_FOUND && result == ESP_OK)
        {
            result = unsubscribe_result;
        }
    }
    return result;
}

static esp_err_t _setup_provisioning_resume(
    setup_provisioning_state_t *state)
{
    device_link_service_status_t status;
    esp_err_t result = device_link_service_open_window();

    if (result != ESP_OK)
    {
        goto fail;
    }
    state->window_held = true;
    result = event_bus_subscribe(
                 DEVICE_LINK_SERVICE_MSG,
                 DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS,
                 _setup_provisioning_event, state,
                 EVENT_BUS_DISPATCH_UI, &state->subscription);
    if (result != ESP_OK)
    {
        goto fail;
    }
    result = device_link_service_get_status(&status);
    if (result != ESP_OK)
    {
        goto fail;
    }
    state->device_link_generation = status.generation;
    _setup_provisioning_render(state, &status);
    return ESP_OK;

fail:
    lv_label_set_text(state->status_label, "绑定服务不可用");
    (void)_setup_provisioning_release_foreground(state);
    return result;
}

static void _setup_provisioning_mount_op(
    const app_manager_page_context_t *context)
{
    _setup_provisioning_mount(context->state);
}

static void _setup_provisioning_resume_op(
    const app_manager_page_context_t *context)
{
    (void)_setup_provisioning_resume(context->state);
}

static esp_err_t _setup_provisioning_pause_op(
    const app_manager_page_context_t *context)
{
    setup_provisioning_state_t *state = context->state;
    _setup_provisioning_scrub(state);
    return _setup_provisioning_release_foreground(state);
}

static void _setup_provisioning_unmount(
    const app_manager_page_context_t *context)
{
    setup_provisioning_state_t *state = context->state;
    _setup_provisioning_scrub(state);
    (void)_setup_provisioning_release_foreground(state);
    app_ui_page_destroy(&state->page);
    state->device_label = NULL;
    state->status_label = NULL;
    state->remaining_label = NULL;
    state->passkey_label = NULL;
    state->confirm_row = NULL;
    state->confirm_button = NULL;
    state->deny_button = NULL;
    state->window_ring = NULL;
}

static const app_manager_page_ops_t s_setup_root_ops =
{
    .mount = _setup_root_mount_op,
    .resume = _setup_root_resume_op,
    .pause = _setup_root_pause_op,
    .unmount = _setup_root_unmount,
};

static const app_manager_page_ops_t s_setup_provisioning_ops =
{
    .mount = _setup_provisioning_mount_op,
    .resume = _setup_provisioning_resume_op,
    .pause = _setup_provisioning_pause_op,
    .unmount = _setup_provisioning_unmount,
};

static const app_manager_page_definition_t s_setup_root_definition =
{
    .ops = &s_setup_root_ops,
    .memory_size = sizeof(setup_root_state_t),
};

static const app_manager_page_definition_t s_setup_provisioning_definition =
{
    .ops = &s_setup_provisioning_ops,
    .memory_size = sizeof(setup_provisioning_state_t),
};

static const app_manager_page_route_t s_setup_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_setup_root_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETUP_PAGE_PROVISIONING,
        .definition = &s_setup_provisioning_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(setup, APP_IMAGE_SETUP_ICON, "网络设置",
                            APP_MANAGER_ID_SETUP, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_setup_routes, 60U,
                            "手机配对与 Wi-Fi");
