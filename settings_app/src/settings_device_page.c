#define DBG_TAG "settings_device"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

typedef struct settings_device_state
{
    app_ui_page_t page;
    lv_obj_t *battery_value;
    lv_obj_t *source_value;
    lv_obj_t *storage_value;
    lv_timer_t *refresh_timer;
} settings_device_state_t;

_Static_assert(sizeof(settings_device_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Device page state exceeds the lifecycle arena slot");

static void _device_refresh(settings_device_state_t *state)
{
    power_service_snapshot_t power;
    if (power_service_get_snapshot(&power) == ESP_OK && power.valid &&
            power.info.battery_percent >= 0)
    {
        char percent[12];
        (void)snprintf(percent, sizeof(percent), "%d%%",
                       power.info.battery_percent);
        app_ui_set_status_text(state->battery_value, percent,
                               power.info.is_charging ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_NEUTRAL);
        app_ui_set_status_text(state->source_value,
                               power.info.is_charging ? "充电中" :
                               (power.info.is_vbus_connected ? "USB 供电" :
                                "电池供电"), APP_UI_STATUS_NEUTRAL);
    }
    else
    {
        app_ui_set_status_text(state->battery_value, "未知",
                               APP_UI_STATUS_WARNING);
        app_ui_set_status_text(state->source_value, "PMU 离线",
                               APP_UI_STATUS_WARNING);
    }
    const bool mounted = sd_storage_service_is_mounted();
    app_ui_set_status_text(state->storage_value, mounted ? "已挂载" : "未挂载",
                           mounted ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_WARNING);
}

static void _device_timer(lv_timer_t *timer)
{
    _device_refresh(lv_timer_get_user_data(timer));
}

static void _device_screen_off_event(lv_event_t *event)
{
    (void)event;
    const esp_err_t result = app_manager_pm_request_screen_off();
    if (result != ESP_OK)
    {
        LOG_W("screen-off request failed: %s", esp_err_to_name(result));
    }
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
    app_ui_page_set_subtitle(&state->page, "电量与存储");
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_value_row(state->page.content, "电量", "读取中",
                         &state->battery_value);
    app_ui_add_value_row(state->page.content, "供电来源", "读取中",
                         &state->source_value);
    app_ui_add_value_row(state->page.content, "SD 卡", "读取中",
                         &state->storage_value);
    app_ui_add_command(state->page.content, LV_SYMBOL_POWER, "立即熄屏",
                       "熄屏或待机后使用 HOME 恢复",
                       _device_screen_off_event, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_HOME, "时间设置",
                      "时区、时间来源与校时状态", _device_open_time, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_SD_CARD, "存储管理",
                      "SD 卡状态与容量信息", _device_open_storage, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_WIFI, "连接向导",
                      "BLE 绑定与 Wi-Fi 配网", _device_open_connection, NULL);

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
    state->battery_value = NULL;
    state->source_value = NULL;
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
