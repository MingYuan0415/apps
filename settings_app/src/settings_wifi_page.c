#define DBG_TAG "settings_wifi"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

typedef struct settings_wifi_state
{
    app_ui_page_t page;
    lv_obj_t *online_switch;
    lv_obj_t *auto_switch;
    lv_obj_t *status_value;
    lv_obj_t *ssid_value;
    lv_obj_t *ip_value;
    lv_obj_t *forget_action;
    lv_obj_t *forget_status;
    lv_obj_t *scan_hint;
    lv_obj_t *scan_list;
    lv_timer_t *refresh_timer;
    connectivity_manager_scan_snapshot_t scan;
    uint64_t rendered_scan_generation;
    bool forget_armed;
} settings_wifi_state_t;

_Static_assert(sizeof(settings_wifi_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Wi-Fi page state exceeds the lifecycle arena slot");

static const char *_wifi_state_text(
    const connectivity_manager_status_snapshot_t *status)
{
    if (status->manual_hold)
    {
        return "已关闭";
    }
    switch (status->state)
    {
    case CONNECTIVITY_MANAGER_STATE_IP_READY:
        return "已连接";
    case CONNECTIVITY_MANAGER_STATE_CONNECTING:
    case CONNECTIVITY_MANAGER_STATE_WAITING_IP:
        return "连接中";
    case CONNECTIVITY_MANAGER_STATE_SCANNING:
        return "扫描中";
    case CONNECTIVITY_MANAGER_STATE_RETRY_WAIT:
        return "重试等待";
    default:
        return "未连接";
    }
}

static void _wifi_render_status(
    settings_wifi_state_t *state,
    const connectivity_manager_status_snapshot_t *status)
{
    const bool connected = status->state == CONNECTIVITY_MANAGER_STATE_IP_READY;
    app_ui_set_status_text(state->status_value, _wifi_state_text(status),
                           connected ? APP_UI_STATUS_SUCCESS :
                           (status->manual_hold ? APP_UI_STATUS_NEUTRAL :
                            APP_UI_STATUS_ACCENT));
    lv_label_set_text(state->ssid_value,
                      status->ssid[0] != '\0' ? status->ssid : "--");
    if (connected)
    {
        const uint32_t ip = status->ipv4_address;
        lv_label_set_text_fmt(state->ip_value, "%u.%u.%u.%u",
                              (unsigned)(ip & 0xFFU),
                              (unsigned)((ip >> 8) & 0xFFU),
                              (unsigned)((ip >> 16) & 0xFFU),
                              (unsigned)((ip >> 24) & 0xFFU));
    }
    else
    {
        lv_label_set_text(state->ip_value, "--");
    }
    if (lv_obj_has_state(state->online_switch, LV_STATE_CHECKED) !=
            (!status->manual_hold))
    {
        if (status->manual_hold)
        {
            lv_obj_remove_state(state->online_switch, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_add_state(state->online_switch, LV_STATE_CHECKED);
        }
    }
    if (lv_obj_has_state(state->auto_switch, LV_STATE_CHECKED) !=
            status->auto_connect)
    {
        if (status->auto_connect)
        {
            lv_obj_add_state(state->auto_switch, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(state->auto_switch, LV_STATE_CHECKED);
        }
    }
}

static void _wifi_row_event(lv_event_t *event)
{
    settings_wifi_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || state == NULL)
    {
        return;
    }
    const size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(
                             lv_event_get_target(event));
    if (index >= state->scan.record_count)
    {
        return;
    }
    const connectivity_manager_scan_record_t *record = &state->scan.records[index];
    if (!record->saved)
    {
        return;
    }
    connectivity_manager_operation_id_t op = 0U;
    (void)connectivity_manager_request_reconnect_saved(&op);
    app_ui_set_status_text(state->scan_hint, "正在连接已保存网络",
                           APP_UI_STATUS_ACCENT);
}

static void _wifi_render_scan(settings_wifi_state_t *state)
{
    lv_obj_clean(state->scan_list);
    if (state->scan.record_count == 0U)
    {
        return;
    }
    for (size_t index = 0U; index < state->scan.record_count; ++index)
    {
        const connectivity_manager_scan_record_t *record =
            &state->scan.records[index];
        lv_obj_t *row = lv_button_create(state->scan_list);
        app_ui_click_only(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_left(row, 14, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(row, (void *)(uintptr_t)index);
        lv_obj_add_event_cb(row, _wifi_row_event, LV_EVENT_CLICKED, state);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, 0);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name, lv_color_hex(APP_UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(name,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(name, record->ssid);

        char detail[40];
        const char *sec = record->security ==
                          CONNECTIVITY_MANAGER_SECURITY_OPEN ? "开放" :
                          (record->security ==
                           CONNECTIVITY_MANAGER_SECURITY_PERSONAL ? "加密" : "不支持");
        (void)snprintf(detail, sizeof(detail), "%s%s %d", sec,
                       record->saved ? " 已保存" : "", (int)record->rssi);
        lv_obj_t *info = lv_label_create(row);
        lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(info,
                                    lv_color_hex(record->saved ?
                                            APP_UI_COLOR_RAIN : APP_UI_COLOR_MUTED),
                                    0);
        lv_obj_set_style_text_font(info, app_ui_font(APP_THEME_FONT_BODY), 0);
        lv_label_set_text(info, detail);
    }
}

static void _wifi_refresh(settings_wifi_state_t *state)
{
    connectivity_manager_status_snapshot_t status;
    if (connectivity_manager_get_status(&status) == ESP_OK)
    {
        _wifi_render_status(state, &status);
    }
    else
    {
        app_ui_set_status_text(state->status_value, "不可用",
                               APP_UI_STATUS_ERROR);
    }
    connectivity_manager_scan_snapshot_t scan;
    if (connectivity_manager_get_scan_snapshot(&scan) == ESP_OK)
    {
        if (scan.running)
        {
            app_ui_set_status_text(state->scan_hint, "正在扫描附近网络…",
                                   APP_UI_STATUS_ACCENT);
        }
        else if (scan.record_count == 0U)
        {
            app_ui_set_status_text(state->scan_hint,
                                   "未发现网络，可重新扫描",
                                   APP_UI_STATUS_NEUTRAL);
        }
        else
        {
            char hint[48];
            (void)snprintf(hint, sizeof(hint), "发现 %u 个网络%s",
                           (unsigned)scan.record_count,
                           scan.truncated ? "（仅显示前 5 个）" : "");
            app_ui_set_status_text(state->scan_hint, hint,
                                   APP_UI_STATUS_NEUTRAL);
        }
        if (scan.generation != state->rendered_scan_generation)
        {
            state->rendered_scan_generation = scan.generation;
            state->scan = scan;
            _wifi_render_scan(state);
        }
    }
}

static void _wifi_timer(lv_timer_t *timer)
{
    _wifi_refresh(lv_timer_get_user_data(timer));
}

static void _wifi_online_event(lv_event_t *event)
{
    settings_wifi_state_t *state = lv_event_get_user_data(event);
    connectivity_manager_operation_id_t op = 0U;
    if (lv_obj_has_state(state->online_switch, LV_STATE_CHECKED))
    {
        connectivity_manager_status_snapshot_t status;
        if (connectivity_manager_get_status(&status) == ESP_OK &&
                status.saved_profile)
        {
            (void)connectivity_manager_request_reconnect_saved(&op);
        }
        else
        {
            (void)connectivity_manager_request_scan(&op);
        }
    }
    else
    {
        (void)connectivity_manager_request_disconnect(&op);
    }
    _wifi_refresh(state);
}

static void _wifi_auto_event(lv_event_t *event)
{
    settings_wifi_state_t *state = lv_event_get_user_data(event);
    connectivity_manager_operation_id_t op = 0U;
    (void)connectivity_manager_set_auto_connect(
        lv_obj_has_state(state->auto_switch, LV_STATE_CHECKED), &op);
    _wifi_refresh(state);
}

static void _wifi_forget_event(lv_event_t *event)
{
    settings_wifi_state_t *state = lv_event_get_user_data(event);
    connectivity_manager_status_snapshot_t status;
    if (connectivity_manager_get_status(&status) != ESP_OK ||
            !status.saved_profile)
    {
        state->forget_armed = false;
        lv_obj_remove_flag(state->forget_status, LV_OBJ_FLAG_HIDDEN);
        app_ui_set_status_text(state->forget_status, "当前没有已保存的网络",
                               APP_UI_STATUS_NEUTRAL);
        return;
    }
    lv_obj_remove_flag(state->forget_status, LV_OBJ_FLAG_HIDDEN);
    if (!state->forget_armed)
    {
        state->forget_armed = true;
        app_ui_set_status_text(state->forget_status, "再次点击“忘记网络”以确认",
                               APP_UI_STATUS_WARNING);
        return;
    }
    state->forget_armed = false;
    connectivity_manager_operation_id_t op = 0U;
    const esp_err_t result = connectivity_manager_request_forget(&op);
    app_ui_set_status_text(state->forget_status,
                           result == ESP_OK ? "已忘记网络" : "操作失败",
                           result == ESP_OK ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_ERROR);
    _wifi_refresh(state);
}

static void _wifi_rescan_event(lv_event_t *event)
{
    settings_wifi_state_t *state = lv_event_get_user_data(event);
    connectivity_manager_operation_id_t op = 0U;
    (void)connectivity_manager_request_scan(&op);
    app_ui_set_status_text(state->scan_hint, "正在扫描附近网络…",
                           APP_UI_STATUS_ACCENT);
}

static void _wifi_mount(const app_manager_page_context_t *context)
{
    settings_wifi_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->rendered_scan_generation = UINT64_MAX;
    app_ui_page_create(&state->page, "Wi-Fi", true);
    app_ui_page_set_subtitle(&state->page, "无线网络");

    (void)app_ui_add_switch_row(state->page.content, "Wi-Fi 开关",
                                "关闭后保持离线至下次连接",
                                _wifi_online_event, state,
                                &state->online_switch);
    (void)app_ui_add_switch_row(state->page.content, "自动连接",
                                "开机后自动连接已保存网络",
                                _wifi_auto_event, state, &state->auto_switch);

    app_ui_add_value_row(state->page.content, "状态", "读取中",
                         &state->status_value);
    app_ui_add_value_row(state->page.content, "网络", "--",
                         &state->ssid_value);
    app_ui_add_value_row(state->page.content, "IP 地址", "--",
                         &state->ip_value);

    state->forget_action = app_ui_add_danger_action(
                               state->page.content, LV_SYMBOL_TRASH, "忘记网络",
                               "清除本机保存的凭据并断开", _wifi_forget_event, state);
    state->forget_status = app_ui_add_body_label(state->page.content, " ");
    lv_obj_add_flag(state->forget_status, LV_OBJ_FLAG_HIDDEN);

    app_ui_add_section(state->page.content, "可用网络");
    state->scan_hint = app_ui_add_body_label(state->page.content,
                       "点击重新扫描查看附近网络");
    (void)app_ui_add_command(state->page.content, LV_SYMBOL_REFRESH,
                             "重新扫描", NULL, _wifi_rescan_event, state);
    state->scan_list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->scan_list);
    lv_obj_set_width(state->scan_list, LV_PCT(100));
    lv_obj_set_height(state->scan_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->scan_list, 8, 0);
    app_ui_make_passive(state->scan_list, false);

    state->refresh_timer = lv_timer_create(_wifi_timer, 1000U, state);
    _wifi_refresh(state);
}

static void _wifi_resume(const app_manager_page_context_t *context)
{
    settings_wifi_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _wifi_refresh(state);
}

static esp_err_t _wifi_pause(const app_manager_page_context_t *context)
{
    settings_wifi_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _wifi_unmount(const app_manager_page_context_t *context)
{
    settings_wifi_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->online_switch = NULL;
    state->auto_switch = NULL;
    state->status_value = NULL;
    state->ssid_value = NULL;
    state->ip_value = NULL;
    state->forget_action = NULL;
    state->forget_status = NULL;
    state->scan_hint = NULL;
    state->scan_list = NULL;
}

static const app_manager_page_ops_t s_settings_wifi_ops =
{
    .mount = _wifi_mount,
    .resume = _wifi_resume,
    .pause = _wifi_pause,
    .unmount = _wifi_unmount,
};

const app_manager_page_definition_t settings_wifi_page_definition =
{
    .ops = &s_settings_wifi_ops,
    .memory_size = sizeof(settings_wifi_state_t),
};
