#define DBG_TAG "settings_device"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

typedef struct settings_device_state
{
    app_ui_page_t page;
    lv_obj_t *wifi_value;
    lv_obj_t *bluetooth_value;
    lv_obj_t *storage_value;
    lv_timer_t *refresh_timer;
} settings_device_state_t;

_Static_assert(sizeof(settings_device_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Device page state exceeds the lifecycle arena slot");

static void _device_refresh(settings_device_state_t *state)
{
    connectivity_manager_status_snapshot_t wifi;
    if (connectivity_manager_get_status(&wifi) == ESP_OK)
    {
        const char *text = wifi.state == CONNECTIVITY_MANAGER_STATE_IP_READY ?
                           (wifi.ssid[0] != '\0' ? wifi.ssid : "已连接") :
                           (wifi.state == CONNECTIVITY_MANAGER_STATE_CONNECTING ?
                            "连接中" : "未连接");
        app_ui_set_status_text(state->wifi_value, text,
                               wifi.state == CONNECTIVITY_MANAGER_STATE_IP_READY ?
                               APP_UI_STATUS_SUCCESS : APP_UI_STATUS_NEUTRAL);
    }
    else
    {
        app_ui_set_status_text(state->wifi_value, "不可用",
                               APP_UI_STATUS_ERROR);
    }

    device_link_service_status_t bluetooth;
    if (device_link_service_get_status(&bluetooth) == ESP_OK)
    {
        app_ui_set_status_text(state->bluetooth_value,
                               bluetooth.bound ? "已绑定" :
                               (bluetooth.active ? "绑定窗口开启" : "未绑定"),
                               bluetooth.bound ? APP_UI_STATUS_SUCCESS :
                               (bluetooth.active ? APP_UI_STATUS_ACCENT :
                                APP_UI_STATUS_NEUTRAL));
    }
    else
    {
        app_ui_set_status_text(state->bluetooth_value, "不可用",
                               APP_UI_STATUS_ERROR);
    }
    app_ui_set_status_text(state->storage_value,
                           sd_storage_service_is_mounted() ? "已挂载" : "未挂载",
                           sd_storage_service_is_mounted() ?
                           APP_UI_STATUS_SUCCESS : APP_UI_STATUS_WARNING);
}

static void _device_timer(lv_timer_t *timer)
{
    _device_refresh(lv_timer_get_user_data(timer));
}

static void _device_open_time(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_SETTINGS,
                                 SETTINGS_PAGE_TIME);
    }
}

static void _device_open_storage(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_SETTINGS,
                                 SETTINGS_PAGE_STORAGE);
    }
}

static void _device_open_connection(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_run(APP_MANAGER_ID_SETUP);
    }
}

static void _device_mount(const app_manager_page_context_t *context)
{
    settings_device_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "设备状态", true);
    app_ui_page_set_subtitle(&state->page, "连接与存储");
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_value_row(state->page.content, "Wi-Fi", "读取中",
                         &state->wifi_value);
    app_ui_add_value_row(state->page.content, "蓝牙", "读取中",
                         &state->bluetooth_value);
    app_ui_add_value_row(state->page.content, "SD 卡", "读取中",
                         &state->storage_value);
    app_ui_add_action(state->page.content, LV_SYMBOL_HOME, "时间设置",
                      "时区、时间来源与校时状态", _device_open_time, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_SD_CARD, "存储管理",
                      "SD 卡状态与容量信息", _device_open_storage, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_BLUETOOTH,
                      "连接管理", "BLE 绑定与 Wi-Fi 网络",
                      _device_open_connection, NULL);

    state->refresh_timer = lv_timer_create(_device_timer, 1000U, state);
    _device_refresh(state);
}

static esp_err_t _device_pause(const app_manager_page_context_t *context)
{
    settings_device_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _device_resume(const app_manager_page_context_t *context)
{
    settings_device_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _device_refresh(state);
}

static void _device_unmount(const app_manager_page_context_t *context)
{
    settings_device_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->wifi_value = NULL;
    state->bluetooth_value = NULL;
    state->storage_value = NULL;
}

static const app_manager_page_ops_t s_settings_device_ops =
{
    .mount = _device_mount,
    .resume = _device_resume,
    .pause = _device_pause,
    .unmount = _device_unmount,
};

const app_manager_page_definition_t settings_device_page_definition =
{
    .ops = &s_settings_device_ops,
    .memory_size = sizeof(settings_device_state_t),
};
