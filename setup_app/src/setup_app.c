#define DBG_TAG "setup_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "setup_wifi_adapter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETUP_PAGE_SLOT_BYTES       2728U
#define SETUP_KEY_DESCRIPTOR_COUNT  44U
#define SETUP_COLOR_SURFACE         0x1A2024
#define SETUP_COLOR_SURFACE_PRESSED 0x252D32
#define SETUP_COLOR_TEXT            0xF2F5F6
#define SETUP_COLOR_MUTED           0x91A0A8
#define SETUP_COLOR_ACCENT          0x39C6C8

typedef enum
{
    SETUP_KEY_MODE_LOWER = 0,
    SETUP_KEY_MODE_UPPER,
    SETUP_KEY_MODE_SYMBOLS,
} setup_key_mode_t;

typedef struct setup_page_state setup_page_state_t;

typedef struct setup_network_action
{
    setup_page_state_t *state;
    uint64_t scan_generation;
    uint8_t index;
} setup_network_action_t;

typedef struct setup_key_action
{
    setup_page_state_t *state;
    char value;
} setup_key_action_t;

struct setup_page_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *detail_label;
    lv_obj_t *controls;
    lv_obj_t *password_mask;
    setup_wifi_adapter_t adapter;
    connectivity_manager_scan_record_t
    records[CONNECTIVITY_MANAGER_MAX_SCAN_RECORDS];
    setup_network_action_t
    network_actions[CONNECTIVITY_MANAGER_MAX_SCAN_RECORDS];
    setup_key_action_t key_actions[SETUP_KEY_DESCRIPTOR_COUNT];
    connectivity_manager_scan_record_t selected;
    uint8_t password[CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES];
    uint64_t scan_generation;
    size_t password_length;
    uint8_t record_count;
    uint8_t key_action_count;
    setup_key_mode_t key_mode;
    connectivity_manager_state_t manager_state;
    bool editing_password;
    bool globally_connected;
    bool saved_profile;
    bool profile_persisted;
    bool auto_connect;
    bool manual_hold;
    bool scan_results_visible;
    bool scan_outcome_visible;
};

_Static_assert(sizeof(setup_page_state_t) <= SETUP_PAGE_SLOT_BYTES,
               "Setup page state exceeds the fixed lifecycle arena slot");

static void _setup_render_scan_action(setup_page_state_t *state);
static void _setup_render_networks(setup_page_state_t *state);
static void _setup_render_keypad(setup_page_state_t *state);
static void _setup_cancel_operation_event(lv_event_t *event);
static esp_err_t _setup_start_adapter(setup_page_state_t *state);

static void _setup_secure_zero(void *data, size_t size)
{
    volatile uint8_t *bytes = data;
    while (size > 0U)
    {
        *bytes = 0U;
        ++bytes;
        --size;
    }
}

static void _setup_scrub_credentials(setup_page_state_t *state)
{
    _setup_secure_zero(state->password, sizeof(state->password));
    _setup_secure_zero(&state->selected, sizeof(state->selected));
    state->password_length = 0;
    state->editing_password = false;
    state->password_mask = NULL;
}

static const char *_setup_security_name(
    connectivity_manager_security_t security)
{
    const char *name = "未知";
    switch (security)
    {
    case CONNECTIVITY_MANAGER_SECURITY_OPEN:
        name = "无密码";
        break;
    case CONNECTIVITY_MANAGER_SECURITY_PERSONAL:
        name = "需密码";
        break;
    case CONNECTIVITY_MANAGER_SECURITY_UNSUPPORTED:
        name = "不支持";
        break;
    default:
        break;
    }
    return name;
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
        return "未报告具体原因";
    }
}

static const char *_setup_command_error(esp_err_t result)
{
    switch (result)
    {
    case ESP_ERR_INVALID_STATE:
        return "当前操作尚未完成";
    case ESP_ERR_NO_MEM:
        return "Wi-Fi 请求队列已满";
    case ESP_ERR_INVALID_ARG:
        return "Wi-Fi 请求参数无效";
    case ESP_ERR_NOT_FOUND:
        return "没有可用的保存网络";
    default:
        return "Wi-Fi 请求未能提交";
    }
}

static void _setup_set_status(setup_page_state_t *state, const char *status,
                              const char *detail)
{
    if (state->status_label != NULL)
    {
        lv_label_set_text(state->status_label, status);
    }
    if (state->detail_label != NULL)
    {
        lv_label_set_text(state->detail_label, detail);
    }
}

static lv_obj_t *_setup_create_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 40);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *_setup_add_compact_button(lv_obj_t *parent, const char *text,
        lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 38);
    lv_obj_set_width(button, 0);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(SETUP_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(SETUP_COLOR_SURFACE_PRESSED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(SETUP_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_center(label);
    return button;
}

static void _setup_update_password_mask(setup_page_state_t *state)
{
    if (state->password_mask == NULL)
    {
        return;
    }
    char mask[CONNECTIVITY_MANAGER_PASSWORD_MAX_BYTES + 16U];
    memset(mask, '*', state->password_length);
    (void)snprintf(&mask[state->password_length],
                   sizeof(mask) - state->password_length,
                   " (%u/63)", (unsigned)state->password_length);
    lv_label_set_text(state->password_mask, mask);
}

static void _setup_key_event(lv_event_t *event)
{
    setup_key_action_t *action = lv_event_get_user_data(event);
    if (action == NULL || action->state == NULL)
    {
        return;
    }
    setup_page_state_t *state = action->state;
    if (!state->editing_password ||
            state->password_length >= sizeof(state->password))
    {
        return;
    }
    state->password[state->password_length++] = (uint8_t)action->value;
    _setup_update_password_mask(state);
}

static void _setup_add_key_button(setup_page_state_t *state, lv_obj_t *row,
                                  const char *label, char value)
{
    if (state->key_action_count >= SETUP_KEY_DESCRIPTOR_COUNT)
    {
        return;
    }
    setup_key_action_t *action = &state->key_actions[state->key_action_count++];
    action->state = state;
    action->value = value;
    (void)_setup_add_compact_button(row, label, _setup_key_event, action);
}

static void _setup_add_key_row(setup_page_state_t *state, const char *keys)
{
    lv_obj_t *row = _setup_create_row(state->controls);
    for (const char *cursor = keys; *cursor != '\0'; ++cursor)
    {
        char label[2] = {*cursor, '\0'};
        _setup_add_key_button(state, row, label, *cursor);
    }
}

static void _setup_lower_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    state->key_mode = SETUP_KEY_MODE_LOWER;
    _setup_render_keypad(state);
}

static void _setup_upper_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    state->key_mode = SETUP_KEY_MODE_UPPER;
    _setup_render_keypad(state);
}

static void _setup_symbols_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    state->key_mode = SETUP_KEY_MODE_SYMBOLS;
    _setup_render_keypad(state);
}

static void _setup_backspace_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    if (state->editing_password && state->password_length > 0)
    {
        --state->password_length;
        state->password[state->password_length] = 0;
        _setup_update_password_mask(state);
    }
}

static void _setup_editor_cancel_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    _setup_scrub_credentials(state);
    state->scan_results_visible = true;
    state->scan_outcome_visible = false;
    _setup_set_status(state, "选择网络", "已取消密码输入");
    _setup_render_networks(state);
}

static void _setup_submit_connect(setup_page_state_t *state)
{
    if (state->selected.security == CONNECTIVITY_MANAGER_SECURITY_PERSONAL &&
            state->password_length < 8U)
    {
        _setup_secure_zero(state->password, sizeof(state->password));
        state->password_length = 0;
        _setup_update_password_mask(state);
        _setup_set_status(state, "密码过短", "请输入 8 至 63 个字符");
        return;
    }

    const size_t ssid_length = strnlen(
                                   state->selected.ssid,
                                   sizeof(state->selected.ssid));
    const connectivity_manager_security_t security = state->selected.security;
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    esp_err_t result = setup_wifi_adapter_connect(
                           &state->adapter, state->selected.ssid, ssid_length,
                           security, state->password, state->password_length);
    state->password_length = 0;
    state->editing_password = false;
    _setup_secure_zero(&state->selected, sizeof(state->selected));
    state->password_mask = NULL;

    if (result != ESP_OK)
    {
        state->scan_results_visible = true;
        state->scan_outcome_visible = false;
        _setup_set_status(state, "连接未开始",
                          _setup_command_error(result));
        _setup_render_networks(state);
        return;
    }
    _setup_set_status(state, "正在连接", "等待接入点响应");
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "取消", "停止本次连接", _setup_cancel_operation_event,
                             state);
}

static void _setup_connect_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    _setup_submit_connect(state);
}

static void _setup_render_keypad(setup_page_state_t *state)
{
    lv_obj_clean(state->controls);
    state->password_mask = lv_label_create(state->controls);
    lv_obj_set_width(state->password_mask, LV_PCT(100));
    lv_obj_set_style_text_color(state->password_mask,
                                lv_color_hex(SETUP_COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(state->password_mask,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    _setup_update_password_mask(state);

    lv_obj_t *modes = _setup_create_row(state->controls);
    (void)_setup_add_compact_button(modes, "abc", _setup_lower_event, state);
    (void)_setup_add_compact_button(modes, "ABC", _setup_upper_event, state);
    (void)_setup_add_compact_button(modes, "123", _setup_symbols_event, state);
    (void)_setup_add_compact_button(modes, LV_SYMBOL_BACKSPACE,
                                    _setup_backspace_event, state);

    state->key_action_count = 0;
    switch (state->key_mode)
    {
    case SETUP_KEY_MODE_LOWER:
        _setup_add_key_row(state, "qwertyuiop");
        _setup_add_key_row(state, "asdfghjkl");
        _setup_add_key_row(state, "zxcvbnm");
        break;
    case SETUP_KEY_MODE_UPPER:
        _setup_add_key_row(state, "QWERTYUIOP");
        _setup_add_key_row(state, "ASDFGHJKL");
        _setup_add_key_row(state, "ZXCVBNM");
        break;
    case SETUP_KEY_MODE_SYMBOLS:
        _setup_add_key_row(state, "1234567890");
        _setup_add_key_row(state, "!@#$%^&*()");
        _setup_add_key_row(state, "-_=+[]{}");
        _setup_add_key_row(state, ".,?/:;\\'");
        _setup_add_key_row(state, "\"<>`|~");
        break;
    }

    lv_obj_t *commands = _setup_create_row(state->controls);
    (void)_setup_add_compact_button(commands, "取消",
                                    _setup_editor_cancel_event, state);
    _setup_add_key_button(state, commands, "空格", ' ');
    (void)_setup_add_compact_button(commands, "连接",
                                    _setup_connect_event, state);
}

static void _setup_cancel_operation_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    _setup_scrub_credentials(state);
    esp_err_t result = setup_wifi_adapter_cancel(&state->adapter);
    if (result == ESP_OK)
    {
        _setup_set_status(state, "正在取消", "等待 Wi-Fi 操作结束");
        lv_obj_clean(state->controls);
    }
    else if (result == ESP_ERR_NOT_FOUND)
    {
        state->scan_results_visible = false;
        state->scan_outcome_visible = false;
        _setup_set_status(state, "已就绪", "操作已经结束");
        _setup_render_scan_action(state);
    }
    else
    {
        _setup_set_status(state, "取消失败", _setup_command_error(result));
    }
}

static void _setup_scan_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    _setup_scrub_credentials(state);
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    if (!setup_wifi_adapter_is_open(&state->adapter))
    {
        (void)_setup_start_adapter(state);
        return;
    }
    esp_err_t result = setup_wifi_adapter_scan(&state->adapter);
    if (result != ESP_OK)
    {
        _setup_set_status(state, "扫描未开始", _setup_command_error(result));
        return;
    }
    _setup_set_status(state, "正在扫描", "搜索附近的网络");
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "取消", "停止本次网络扫描",
                             _setup_cancel_operation_event, state);
}

static void _setup_disconnect_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    _setup_scrub_credentials(state);
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    esp_err_t result = setup_wifi_adapter_disconnect(&state->adapter);
    if (result != ESP_OK)
    {
        _setup_set_status(state, "断开未开始",
                          _setup_command_error(result));
        return;
    }
    _setup_set_status(state, "正在断开", "清理当前连接");
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "取消", "停止本次断开请求",
                             _setup_cancel_operation_event, state);
}

static void _setup_reconnect_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    esp_err_t result = setup_wifi_adapter_reconnect_saved(&state->adapter);
    if (result != ESP_OK)
    {
        _setup_set_status(state, "重新连接未开始",
                          _setup_command_error(result));
        return;
    }
    _setup_set_status(state, "正在连接", "使用已保存的网络配置");
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "取消", "停止本次连接",
                             _setup_cancel_operation_event, state);
}

static void _setup_forget_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    esp_err_t result = setup_wifi_adapter_forget(&state->adapter);
    if (result != ESP_OK)
    {
        _setup_set_status(state, "忘记网络未开始",
                          _setup_command_error(result));
        return;
    }
    _setup_set_status(state, "正在忘记网络", "删除保存配置并断开连接");
    lv_obj_clean(state->controls);
}

static void _setup_auto_connect_event(lv_event_t *event)
{
    setup_page_state_t *state = lv_event_get_user_data(event);
    lv_obj_t *toggle = lv_event_get_target_obj(event);
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    esp_err_t result = setup_wifi_adapter_set_auto_connect(&state->adapter,
                       enabled);
    if (result != ESP_OK)
    {
        if (state->auto_connect)
        {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(toggle, LV_STATE_CHECKED);
        }
        _setup_set_status(state, "自动连接未更新",
                          _setup_command_error(result));
        return;
    }
    _setup_set_status(state, enabled ? "正在启用自动连接" :
                      "正在关闭自动连接", "正在保存网络策略");
}

static void _setup_render_auto_connect(setup_page_state_t *state)
{
    if (!state->saved_profile)
    {
        return;
    }
    lv_obj_t *row = _setup_create_row(state->controls);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, "自动连接");
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_color(label, lv_color_hex(SETUP_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_obj_t *toggle = lv_switch_create(row);
    if (state->auto_connect)
    {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(toggle, _setup_auto_connect_event,
                        LV_EVENT_VALUE_CHANGED, state);
}

static void _setup_render_scan_action(setup_page_state_t *state)
{
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_REFRESH,
                             "扫描网络", "刷新附近的接入点",
                             _setup_scan_event, state);
}

static void _setup_render_saved_actions(setup_page_state_t *state)
{
    if (!state->globally_connected)
    {
        (void)app_ui_add_command(state->controls, LV_SYMBOL_LOOP,
                                 "重新连接", "连接已保存的网络",
                                 _setup_reconnect_event, state);
    }
    _setup_render_auto_connect(state);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_TRASH,
                             "忘记网络", "删除保存的网络配置",
                             _setup_forget_event, state);
}

static void _setup_render_connection_actions(setup_page_state_t *state)
{
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "断开连接", "结束当前 Wi-Fi 连接",
                             _setup_disconnect_event, state);
    if (state->saved_profile)
    {
        _setup_render_saved_actions(state);
    }
    (void)app_ui_add_command(state->controls, LV_SYMBOL_REFRESH,
                             "扫描网络", "查找其他接入点",
                             _setup_scan_event, state);
}

static void _setup_render_scan_outcome_actions(setup_page_state_t *state)
{
    if (state->globally_connected)
    {
        _setup_render_connection_actions(state);
    }
    else
    {
        _setup_render_scan_action(state);
    }
}

static void _setup_network_event(lv_event_t *event)
{
    setup_network_action_t *action = lv_event_get_user_data(event);
    if (action == NULL || action->state == NULL)
    {
        return;
    }
    setup_page_state_t *state = action->state;
    if (action->scan_generation != state->scan_generation ||
            action->index >= state->record_count)
    {
        return;
    }

    _setup_scrub_credentials(state);
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    state->selected = state->records[action->index];
    switch (state->selected.security)
    {
    case CONNECTIVITY_MANAGER_SECURITY_OPEN:
        _setup_submit_connect(state);
        break;
    case CONNECTIVITY_MANAGER_SECURITY_PERSONAL:
        state->editing_password = true;
        state->key_mode = SETUP_KEY_MODE_LOWER;
        _setup_set_status(state, "输入密码", state->selected.ssid);
        _setup_render_keypad(state);
        break;
    case CONNECTIVITY_MANAGER_SECURITY_UNSUPPORTED:
        state->scan_results_visible = true;
        state->scan_outcome_visible = false;
        _setup_set_status(state, "网络不受支持", "暂不支持该安全类型");
        _setup_render_networks(state);
        break;
    }
}

static void _setup_render_networks(setup_page_state_t *state)
{
    lv_obj_clean(state->controls);
    for (uint8_t index = 0; index < state->record_count; ++index)
    {
        const connectivity_manager_scan_record_t *record =
            &state->records[index];
        char detail[48];
        (void)snprintf(detail, sizeof(detail), "%d dBm | %s",
                       record->rssi, _setup_security_name(record->security));
        setup_network_action_t *action = &state->network_actions[index];
        action->state = state;
        action->scan_generation = state->scan_generation;
        action->index = index;
        (void)app_ui_add_command(state->controls, LV_SYMBOL_WIFI, record->ssid,
                                 detail, _setup_network_event, action);
    }
    if (state->record_count == 0)
    {
        _setup_render_scan_action(state);
    }
}

static void _setup_render_pending_connection(
    setup_page_state_t *state, const char *title, const char *ssid)
{
    _setup_set_status(state, title, ssid);
    lv_obj_clean(state->controls);
    (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                             "取消", "停止本次连接",
                             _setup_cancel_operation_event, state);
}

static void _setup_render_idle_status(
    setup_page_state_t *state,
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind)
{
    _setup_scrub_credentials(state);
    if (scope == SETUP_WIFI_STATUS_OPERATION &&
            operation_kind == SETUP_WIFI_OPERATION_CONNECT &&
            snapshot->last_error == ESP_ERR_NOT_FINISHED)
    {
        _setup_set_status(state, "连接已取消", snapshot->ssid);
    }
    else if (scope == SETUP_WIFI_STATUS_OPERATION &&
             operation_kind == SETUP_WIFI_OPERATION_CONNECT &&
             snapshot->last_error != ESP_OK)
    {
        _setup_set_status(state, "连接失败",
                          _setup_failure_detail(snapshot->failure));
    }
    else if (snapshot->saved_profile)
    {
        _setup_set_status(state, "已保存网络",
                          snapshot->manual_hold ?
                          "本次启动保持离线" : snapshot->ssid);
    }
    else
    {
        _setup_set_status(state, "Wi-Fi 已就绪", "当前未连接");
    }
    lv_obj_clean(state->controls);
    if (snapshot->saved_profile)
    {
        _setup_render_saved_actions(state);
    }
    (void)app_ui_add_command(state->controls, LV_SYMBOL_REFRESH,
                             "扫描网络", "刷新附近的接入点",
                             _setup_scan_event, state);
}

static void _setup_render_status_state(
    setup_page_state_t *state,
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind)
{
    switch (snapshot->state)
    {
    case CONNECTIVITY_MANAGER_STATE_OFFLINE:
        _setup_scrub_credentials(state);
        _setup_set_status(state, "Wi-Fi 不可用",
                          _setup_failure_detail(snapshot->failure));
        _setup_render_scan_action(state);
        break;
    case CONNECTIVITY_MANAGER_STATE_IDLE:
        _setup_render_idle_status(state, snapshot, scope, operation_kind);
        break;
    case CONNECTIVITY_MANAGER_STATE_SCANNING:
        break;
    case CONNECTIVITY_MANAGER_STATE_CONNECTING:
        _setup_render_pending_connection(state, "正在连接", snapshot->ssid);
        break;
    case CONNECTIVITY_MANAGER_STATE_WAITING_IP:
        _setup_render_pending_connection(state, "正在获取地址",
                                         snapshot->ssid);
        break;
    case CONNECTIVITY_MANAGER_STATE_IP_READY:
        _setup_scrub_credentials(state);
        _setup_set_status(state,
                          snapshot->failure ==
                          CONNECTIVITY_MANAGER_FAILURE_STORAGE ?
                          "已连接，但未保存" : "已连接",
                          snapshot->failure ==
                          CONNECTIVITY_MANAGER_FAILURE_STORAGE ?
                          _setup_failure_detail(snapshot->failure) :
                          snapshot->ssid);
        _setup_render_connection_actions(state);
        break;
    case CONNECTIVITY_MANAGER_STATE_RETRY_WAIT:
        if (snapshot->retry_delay_ms == 0U)
        {
            _setup_render_pending_connection(state, "正在重试连接",
                                             snapshot->ssid);
        }
        else
        {
            _setup_set_status(state, "等待重新连接", snapshot->ssid);
            lv_obj_clean(state->controls);
            (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                                     "停止重试", "本次启动保持离线",
                                     _setup_disconnect_event, state);
        }
        break;
    case CONNECTIVITY_MANAGER_STATE_SUSPENDED:
        _setup_scrub_credentials(state);
        _setup_set_status(state, "Wi-Fi 已暂停", "系统正在待机");
        lv_obj_clean(state->controls);
        break;
    }
}

static void _setup_status_snapshot(
    const connectivity_manager_status_snapshot_t *snapshot,
    setup_wifi_status_scope_t scope,
    setup_wifi_operation_kind_t operation_kind,
    void *user_data)
{
    setup_page_state_t *state = user_data;
    if (state->page.root == NULL)
    {
        return;
    }
    state->manager_state = snapshot->state;
    state->globally_connected = snapshot->state ==
                                CONNECTIVITY_MANAGER_STATE_IP_READY;
    state->saved_profile = snapshot->saved_profile;
    state->profile_persisted = snapshot->profile_persisted;
    state->auto_connect = snapshot->auto_connect;
    state->manual_hold = snapshot->manual_hold;

    if (state->scan_outcome_visible &&
            (snapshot->state == CONNECTIVITY_MANAGER_STATE_IDLE ||
             snapshot->state == CONNECTIVITY_MANAGER_STATE_IP_READY))
    {
        _setup_render_scan_outcome_actions(state);
        return;
    }
    if (state->scan_results_visible &&
            (snapshot->state == CONNECTIVITY_MANAGER_STATE_IDLE ||
             snapshot->state == CONNECTIVITY_MANAGER_STATE_IP_READY))
    {
        return;
    }
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    _setup_render_status_state(state, snapshot, scope, operation_kind);
}

static void _setup_scan_snapshot(
    const connectivity_manager_scan_snapshot_t *snapshot,
    void *user_data)
{
    setup_page_state_t *state = user_data;
    if (state->page.root == NULL)
    {
        return;
    }
    if (snapshot->running)
    {
        state->scan_results_visible = false;
        state->scan_outcome_visible = false;
        _setup_set_status(state, "正在扫描", "搜索附近的网络");
        lv_obj_clean(state->controls);
        (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                                 "取消", "停止本次网络扫描",
                                 _setup_cancel_operation_event, state);
    }
    else if (snapshot->last_error == ESP_OK)
    {
        state->scan_generation = snapshot->generation;
        state->record_count = snapshot->record_count;
        memcpy(state->records, snapshot->records,
               state->record_count * sizeof(state->records[0]));
        state->scan_results_visible = true;
        state->scan_outcome_visible = false;
        _setup_set_status(state, "选择网络",
                          snapshot->truncated ?
                          "仅显示信号最强的网络" :
                          "附近网络已就绪");
        _setup_render_networks(state);
    }
    else if (snapshot->last_error == ESP_ERR_NOT_FINISHED)
    {
        state->scan_results_visible = false;
        state->scan_outcome_visible = true;
        _setup_set_status(state, "扫描已取消", "可以重新扫描");
        _setup_render_scan_outcome_actions(state);
    }
    else
    {
        state->scan_results_visible = false;
        state->scan_outcome_visible = true;
        _setup_set_status(state, "扫描失败",
                          "请稍后重新扫描");
        _setup_render_scan_outcome_actions(state);
    }
}

static esp_err_t _setup_start_adapter(setup_page_state_t *state)
{
    const setup_wifi_adapter_callbacks_t callbacks =
    {
        .status = _setup_status_snapshot,
        .scan = _setup_scan_snapshot,
    };
    esp_err_t result = setup_wifi_adapter_open(&state->adapter, &callbacks,
                       state);
    if (result != ESP_OK)
    {
        _setup_set_status(state, "Wi-Fi 不可用",
                          _setup_command_error(result));
        _setup_render_scan_action(state);
        return result;
    }
    if (!state->globally_connected &&
            state->manager_state == CONNECTIVITY_MANAGER_STATE_IDLE)
    {
        result = setup_wifi_adapter_scan(&state->adapter);
        if (result == ESP_OK)
        {
            state->scan_results_visible = false;
            state->scan_outcome_visible = false;
            _setup_set_status(state, "正在扫描", "搜索附近的网络");
            lv_obj_clean(state->controls);
            (void)app_ui_add_command(state->controls, LV_SYMBOL_CLOSE,
                                     "取消", "停止本次网络扫描",
                                     _setup_cancel_operation_event, state);
        }
        else
        {
            _setup_set_status(state, "扫描未开始",
                              _setup_command_error(result));
            _setup_render_scan_action(state);
        }
    }
    return result;
}

static void _setup_page_mount(setup_page_state_t *state)
{
    app_ui_page_create(&state->page, "网络设置", true);
    (void)app_ui_add_section(state->page.content, "WI-FI");

    state->status_label = lv_label_create(state->page.content);
    lv_label_set_text(state->status_label, "正在启动");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_color(state->status_label,
                                lv_color_hex(SETUP_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);

    state->detail_label = lv_label_create(state->page.content);
    lv_label_set_text(state->detail_label, "正在加载 Wi-Fi 策略");
    lv_obj_set_width(state->detail_label, LV_PCT(100));
    lv_label_set_long_mode(state->detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->detail_label,
                                lv_color_hex(SETUP_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->detail_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    state->controls = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->controls);
    lv_obj_set_width(state->controls, LV_PCT(100));
    lv_obj_set_height(state->controls, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(state->controls, 8, 0);
    lv_obj_set_flex_flow(state->controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(state->controls, LV_OBJ_FLAG_SCROLLABLE);
}

static esp_err_t _setup_page_pause(setup_page_state_t *state)
{
    esp_err_t result = setup_wifi_adapter_close(&state->adapter);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
        LOG_W("WiFi session close incomplete: %s", esp_err_to_name(result));
        return result;
    }
    _setup_scrub_credentials(state);
    _setup_secure_zero(state->records, sizeof(state->records));
    state->record_count = 0;
    state->scan_generation = 0;
    state->globally_connected = false;
    state->saved_profile = false;
    state->profile_persisted = false;
    state->auto_connect = false;
    state->manual_hold = false;
    state->scan_results_visible = false;
    state->scan_outcome_visible = false;
    return result;
}

static void _setup_page_unmount(setup_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->detail_label = NULL;
    state->controls = NULL;
    state->password_mask = NULL;
    state->key_action_count = 0;
}

static void _setup_page_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    setup_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _setup_page_mount(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        (void)_setup_start_adapter(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_setup_page_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _setup_page_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        if (_setup_page_pause(state) == ESP_OK)
        {
            LOG_I("stopped");
        }
        break;
    default:
        break;
    }
}

static const app_manager_page_definition_t s_setup_root_definition =
{
    .handler = _setup_page_handler,
    .memory_size = sizeof(setup_page_state_t),
};

static const app_manager_page_route_t s_setup_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_setup_root_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT(setup, NULL, APP_MANAGER_ID_SETUP, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_setup_routes);
