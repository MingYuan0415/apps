#define DBG_TAG "setup_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "device_link_service.h"
#include "setup_wifi_adapter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETUP_PAGE_PROVISIONING "provisioning"
#define SETUP_QR_SIZE 280
#define SETUP_COLOR_TEXT  0xF2F5F6
#define SETUP_COLOR_MUTED 0x91A0A8

typedef struct setup_root_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *detail_label;
    lv_obj_t *controls;
    setup_wifi_adapter_t wifi;
    event_bus_sub_handle_t device_link_subscription;
    connectivity_manager_status_snapshot_t connectivity;
    device_link_service_status_t device_link;
    setup_wifi_operation_kind_t completed_operation;
    esp_err_t completed_result;
    uint64_t device_link_generation;
    bool connectivity_valid;
    bool device_link_valid;
} setup_root_state_t;

typedef struct setup_provisioning_state
{
    app_ui_page_t page;
    lv_obj_t *qr;
    lv_obj_t *device_label;
    lv_obj_t *status_label;
    lv_obj_t *remaining_label;
    event_bus_sub_handle_t subscription;
    char qr_text[DEVICE_LINK_SERVICE_QR_MAX_BYTES];
    size_t qr_length;
    uint64_t device_link_generation;
} setup_provisioning_state_t;

_Static_assert(sizeof(setup_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Setup root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(setup_provisioning_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Setup provisioning state exceeds the lifecycle arena slot");

static void _setup_root_render(setup_root_state_t *state);

static void _setup_secure_zero(void *data, size_t size)
{
    volatile uint8_t *bytes = data;
    while (size > 0U)
    {
        *bytes++ = 0U;
        --size;
    }
}

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
    const esp_err_t result = device_link_service_open_window();
    if (result == ESP_OK)
    {
        app_ui_request_open_page(APP_MANAGER_ID_SETUP,
                                 SETUP_PAGE_PROVISIONING);
    }
    else
    {
        setup_root_state_t *state = lv_event_get_user_data(event);
        _setup_set_status(state, "手机配网未启动",
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
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, "自动连接");
    lv_obj_set_style_text_color(label, lv_color_hex(SETUP_COLOR_TEXT), 0);
    lv_obj_t *toggle = lv_switch_create(row);
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
    (void)app_ui_add_action(state->controls, LV_SYMBOL_BLUETOOTH,
                            "手机配网",
                            transport_fault ?
                            "蓝牙关闭失败，需要重启" :
                            state->device_link_valid &&
                            state->device_link.active ?
                            "配网窗口正在运行" :
                            "显示二维码并开启 10 分钟 BLE 窗口",
                            _setup_open_provisioning_event, state);
    if (transport_fault)
    {
        lv_label_set_text(state->detail_label,
                          "蓝牙关闭失败，需要重启");
    }
    else if (state->device_link_valid && state->device_link.active)
    {
        lv_label_set_text(state->detail_label,
                          "手机配网进行中，本机网络管理已锁定");
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
    app_ui_page_create(&state->page, "网络设置", true);
    (void)app_ui_add_section(state->page.content, "WI-FI");
    state->status_label = lv_label_create(state->page.content);
    lv_label_set_text(state->status_label, "正在加载");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_color(state->status_label,
                                lv_color_hex(SETUP_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    state->detail_label = lv_label_create(state->page.content);
    lv_label_set_text(state->detail_label, "正在读取网络状态");
    lv_obj_set_width(state->detail_label, LV_PCT(100));
    lv_label_set_long_mode(state->detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->detail_label,
                                lv_color_hex(SETUP_COLOR_MUTED), 0);
    state->controls = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->controls);
    lv_obj_set_width(state->controls, LV_PCT(100));
    lv_obj_set_height(state->controls, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(state->controls, 8, 0);
    lv_obj_set_flex_flow(state->controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(state->controls, LV_OBJ_FLAG_SCROLLABLE);
}

static esp_err_t _setup_root_resume(setup_root_state_t *state)
{
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
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->device_link_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            result = ESP_OK;
        }
    }
    if (result == ESP_OK)
    {
        result = setup_wifi_adapter_close(&state->wifi);
    }
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
    }
    return result;
}

static void _setup_root_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    setup_root_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->device_link_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        _setup_root_mount(state);
        break;
    case APP_MANAGER_MSG_ONRESUME:
        (void)_setup_root_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_setup_root_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        state->status_label = NULL;
        state->detail_label = NULL;
        state->controls = NULL;
        break;
    case APP_MANAGER_MSG_ONSTOP:
    {
        const esp_err_t pause_result = _setup_root_pause(state);
        const esp_err_t close_result = device_link_service_close_window();
        if (pause_result == ESP_OK && close_result == ESP_OK)
        {
            LOG_I("stopped");
        }
        else
        {
            app_manager_this_page_report_cleanup_error(
                pause_result != ESP_OK ? pause_result : close_result);
        }
        break;
    }
    default:
        break;
    }
}

static void _setup_provisioning_scrub(setup_provisioning_state_t *state)
{
    if (state->qr != NULL)
    {
        lv_canvas_fill_bg(state->qr, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        lv_obj_delete(state->qr);
        state->qr = NULL;
    }
    _setup_secure_zero(state->qr_text, sizeof(state->qr_text));
    state->qr_length = 0U;
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
                          "配网服务发生错误");
        lv_label_set_text(state->remaining_label, "");
        return;
    }
    if (!status->active)
    {
        _setup_provisioning_scrub(state);
        lv_label_set_text(state->status_label, "配网窗口已关闭");
        lv_label_set_text(state->remaining_label, "");
        return;
    }
    lv_label_set_text(state->status_label,
                      status->client_connected ? "手机已连接" :
                      "等待手机连接");
    char remaining[48];
    const uint32_t seconds = status->window_remaining_ms / 1000U;
    (void)snprintf(remaining, sizeof(remaining), "剩余 %u:%02u",
                   (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
    lv_label_set_text(state->remaining_label, remaining);
    if (status->qr_ready && state->qr == NULL)
    {
        size_t length = 0U;
        if (device_link_service_copy_qr(
                    state->qr_text, sizeof(state->qr_text), &length) == ESP_OK)
        {
            state->qr_length = length;
            state->qr = lv_qrcode_create(state->page.content);
            lv_qrcode_set_size(state->qr, SETUP_QR_SIZE);
            lv_qrcode_set_dark_color(state->qr, lv_color_hex(0x111111));
            lv_qrcode_set_light_color(state->qr, lv_color_hex(0xFFFFFF));
            lv_qrcode_set_quiet_zone(state->qr, true);
            if (lv_qrcode_update(state->qr, state->qr_text,
                                 (uint32_t)state->qr_length) != LV_RESULT_OK)
            {
                _setup_provisioning_scrub(state);
                lv_label_set_text(state->status_label, "二维码生成失败");
            }
        }
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

static void _setup_provisioning_mount(setup_provisioning_state_t *state)
{
    app_ui_page_create(&state->page, "手机配网", true);
    state->device_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->device_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->device_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->device_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    state->status_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);
    state->remaining_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->remaining_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->remaining_label,
                                LV_TEXT_ALIGN_CENTER, 0);
}

static esp_err_t _setup_provisioning_resume(
    setup_provisioning_state_t *state)
{
    esp_err_t result = device_link_service_open_window();
    if (result == ESP_OK)
    {
        result = event_bus_subscribe(
                     DEVICE_LINK_SERVICE_MSG,
                     DEVICE_LINK_SERVICE_MSG_SUB_TYPE_STATUS,
                     _setup_provisioning_event, state,
                     EVENT_BUS_DISPATCH_UI, &state->subscription);
    }
    device_link_service_status_t status;
    if (result == ESP_OK)
    {
        result = device_link_service_get_status(&status);
    }
    if (result == ESP_OK)
    {
        state->device_link_generation = status.generation;
        _setup_provisioning_render(state, &status);
    }
    else
    {
        lv_label_set_text(state->status_label, "配网服务不可用");
    }
    return result;
}

static esp_err_t _setup_provisioning_pause(
    setup_provisioning_state_t *state)
{
    _setup_provisioning_scrub(state);
    /* The binding window is a foreground resource owned between RESUME and
     * PAUSE: leaving the page closes it so no window outlives its page. */
    esp_err_t result = device_link_service_close_window();
    esp_err_t unsubscribe_result = ESP_OK;

    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        /* The subscription is unsubscribed even when the close failed, so a
         * later event cannot target an unmounted page. */
        unsubscribe_result = event_bus_unsubscribe(state->subscription);
        if (unsubscribe_result == ESP_OK ||
                unsubscribe_result == ESP_ERR_NOT_FOUND)
        {
            state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            unsubscribe_result = ESP_OK;
        }
    }
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    {
        /* Preserve the close failure: report it first (the App Manager
         * keeps the first reported error), then the unsubscribe failure if
         * any. */
        app_manager_this_page_report_cleanup_error(result);
        if (unsubscribe_result != ESP_OK)
        {
            app_manager_this_page_report_cleanup_error(unsubscribe_result);
        }
        return result;
    }
    if (unsubscribe_result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(unsubscribe_result);
        return unsubscribe_result;
    }
    return ESP_OK;
}

static void _setup_provisioning_handler(app_manager_msg_type_t message,
                                        void *param)
{
    (void)param;
    setup_provisioning_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        _setup_provisioning_mount(state);
        break;
    case APP_MANAGER_MSG_ONRESUME:
        (void)_setup_provisioning_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_setup_provisioning_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _setup_provisioning_scrub(state);
        app_ui_page_destroy(&state->page);
        state->device_label = NULL;
        state->status_label = NULL;
        state->remaining_label = NULL;
        break;
    case APP_MANAGER_MSG_ONSTOP:
    {
        const esp_err_t pause_result = _setup_provisioning_pause(state);
        const esp_err_t close_result = device_link_service_close_window();
        if (pause_result != ESP_OK || close_result != ESP_OK)
        {
            app_manager_this_page_report_cleanup_error(
                pause_result != ESP_OK ? pause_result : close_result);
        }
        break;
    }
    default:
        break;
    }
}

static const app_manager_page_definition_t s_setup_root_definition =
{
    .handler = _setup_root_handler,
    .memory_size = sizeof(setup_root_state_t),
};

static const app_manager_page_definition_t s_setup_provisioning_definition =
{
    .handler = _setup_provisioning_handler,
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

APP_MANAGER_APP_EXPORT(setup, NULL, APP_MANAGER_ID_SETUP, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_setup_routes);
