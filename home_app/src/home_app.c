#define DBG_TAG "home_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "connectivity_manager.h"
#include "event_bus.h"
#include "power_service.h"
#include "sd_storage_service.h"
#include "time_service.h"
#include "timer_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct home_page_state
{
    app_ui_page_t page;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *quality_label;
    lv_obj_t *power_value;
    lv_obj_t *wifi_value;
    lv_obj_t *storage_value;
    lv_obj_t *timer_value;
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t power_subscription;
    event_bus_sub_handle_t wifi_subscription;
    bool wifi_initialization_elapsed;
} home_page_state_t;

_Static_assert(sizeof(home_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Home page state exceeds the lifecycle arena slot");

static const char *_home_time_quality_text(time_service_quality_t quality)
{
    switch (quality)
    {
    case TIME_SERVICE_QUALITY_RTC:
        return "RTC 时间";
    case TIME_SERVICE_QUALITY_MANUAL:
        return "手动时间";
    case TIME_SERVICE_QUALITY_NTP:
        return "网络时间";
    case TIME_SERVICE_QUALITY_INVALID:
    default:
        return "时间未校准";
    }
}

static void _home_update_clock(home_page_state_t *state)
{
    static const char *const weekdays[] =
    {
        "日", "一", "二", "三", "四", "五", "六",
    };
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        lv_label_set_text(state->time_label, "--:--");
        lv_label_set_text(state->date_label, "等待有效时间");
        app_ui_set_status_text(state->quality_label, "时间不可用",
                               APP_UI_STATUS_WARNING);
        return;
    }

    char text[48];
    if (strftime(text, sizeof(text), "%H:%M", &local_time) > 0)
    {
        lv_label_set_text(state->time_label, text);
    }
    int weekday = local_time.tm_wday;
    if (weekday < 0 || weekday >= (int)(sizeof(weekdays) / sizeof(weekdays[0])))
    {
        weekday = 0;
    }
    (void)snprintf(text, sizeof(text), "%d月%d日  周%s",
                   local_time.tm_mon + 1, local_time.tm_mday,
                   weekdays[weekday]);
    lv_label_set_text(state->date_label, text);

    const time_service_quality_t quality = time_service_get_quality();
    app_ui_set_status_text(
        state->quality_label, _home_time_quality_text(quality),
        quality == TIME_SERVICE_QUALITY_INVALID ? APP_UI_STATUS_WARNING :
        APP_UI_STATUS_ACCENT);
}

static void _home_render_power(home_page_state_t *state,
                               const power_service_snapshot_t *snapshot)
{
    if (!snapshot->valid)
    {
        app_ui_set_status_text(state->power_value, "不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }

    char text[48];
    if (snapshot->info.battery_percent >= 0)
    {
        (void)snprintf(text, sizeof(text), "%d%% · %s",
                       snapshot->info.battery_percent,
                       snapshot->info.is_charging ? "充电中" :
                       (snapshot->info.is_vbus_connected ? "USB 供电" :
                        "电池供电"));
    }
    else
    {
        (void)snprintf(text, sizeof(text), "%u mV · %s",
                       (unsigned)snapshot->info.battery_voltage_mv,
                       snapshot->info.is_vbus_connected ? "USB 供电" :
                       "电池供电");
    }
    app_ui_set_status_text(state->power_value, text,
                           snapshot->info.is_charging ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_NEUTRAL);
}

static void _home_render_wifi(home_page_state_t *state,
                              const connectivity_manager_status_snapshot_t *snapshot)
{
    const char *text = "初始化中";
    app_ui_status_t status = APP_UI_STATUS_ACCENT;
    if (!snapshot->available)
    {
        const bool initializing = snapshot->generation == 0U &&
                                  !state->wifi_initialization_elapsed;
        text = initializing ? "初始化中" : "不可用";
        status = initializing ? APP_UI_STATUS_ACCENT : APP_UI_STATUS_ERROR;
    }
    else
    {
        state->wifi_initialization_elapsed = false;
        switch (snapshot->state)
        {
        case CONNECTIVITY_MANAGER_STATE_IP_READY:
            text = snapshot->ssid[0] != '\0' ? snapshot->ssid : "已连接";
            status = APP_UI_STATUS_SUCCESS;
            break;
        case CONNECTIVITY_MANAGER_STATE_SCANNING:
            text = "正在扫描";
            break;
        case CONNECTIVITY_MANAGER_STATE_CONNECTING:
            text = "正在连接";
            break;
        case CONNECTIVITY_MANAGER_STATE_WAITING_IP:
            text = "正在获取地址";
            break;
        case CONNECTIVITY_MANAGER_STATE_RETRY_WAIT:
            text = "等待重试";
            status = APP_UI_STATUS_WARNING;
            break;
        case CONNECTIVITY_MANAGER_STATE_SUSPENDED:
            text = "已暂停";
            status = APP_UI_STATUS_WARNING;
            break;
        case CONNECTIVITY_MANAGER_STATE_OFFLINE:
            text = "不可用";
            status = APP_UI_STATUS_ERROR;
            break;
        case CONNECTIVITY_MANAGER_STATE_IDLE:
        default:
            text = "未连接";
            status = APP_UI_STATUS_NEUTRAL;
            break;
        }
    }
    app_ui_set_status_text(state->wifi_value, text, status);
}

static void _home_update_cached_status(home_page_state_t *state)
{
    power_service_snapshot_t power;
    if (power_service_get_snapshot(&power) == ESP_OK)
    {
        _home_render_power(state, &power);
    }
    else
    {
        app_ui_set_status_text(state->power_value, "不可用",
                               APP_UI_STATUS_ERROR);
    }

    connectivity_manager_status_snapshot_t wifi;
    if (connectivity_manager_get_status(&wifi) == ESP_OK)
    {
        _home_render_wifi(state, &wifi);
    }
    else
    {
        app_ui_set_status_text(state->wifi_value, "初始化中",
                               APP_UI_STATUS_ACCENT);
    }

    const bool mounted = sd_storage_service_is_mounted();
    app_ui_set_status_text(state->storage_value,
                           mounted ? "已挂载" : "未挂载",
                           mounted ? APP_UI_STATUS_SUCCESS :
                           APP_UI_STATUS_WARNING);

    timer_service_snapshot_t timer;
    if (timer_service_get_snapshot(&timer) == ESP_OK &&
            (timer.countdown_state == TIMER_SERVICE_RUNNING ||
             timer.focus_state == TIMER_SERVICE_RUNNING))
    {
        const uint32_t remaining = timer.focus_state == TIMER_SERVICE_RUNNING ?
                                   timer.focus_remaining_ms :
                                   timer.countdown_remaining_ms;
        char text[32];
        (void)snprintf(text, sizeof(text), "%02u:%02u",
                       (unsigned)(remaining / 60000U),
                       (unsigned)((remaining / 1000U) % 60U));
        app_ui_set_status_text(state->timer_value, text, APP_UI_STATUS_ACCENT);
    }
    else
    {
        app_ui_set_status_text(state->timer_value, "无", APP_UI_STATUS_NEUTRAL);
    }
}

static void _home_refresh_timer(lv_timer_t *timer)
{
    home_page_state_t *state = lv_timer_get_user_data(timer);
    if (state->page.root != NULL)
    {
        state->wifi_initialization_elapsed = true;
        _home_update_clock(state);
        _home_update_cached_status(state);
    }
}

static void _home_power_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                              const void *payload, size_t payload_size,
                              void *user_data)
{
    home_page_state_t *state = user_data;
    if (msg_id != POWER_SERVICE_MSG ||
            sub_type != POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE ||
            payload == NULL ||
            payload_size != sizeof(power_service_snapshot_t) ||
            state->page.root == NULL)
    {
        return;
    }

    power_service_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    _home_render_power(state, &snapshot);
}

static void _home_wifi_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                             const void *payload, size_t payload_size,
                             void *user_data)
{
    home_page_state_t *state = user_data;
    if (msg_id != CONNECTIVITY_MANAGER_MSG ||
            sub_type !=
            CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT ||
            payload == NULL ||
            payload_size != sizeof(connectivity_manager_status_snapshot_t) ||
            state->page.root == NULL)
    {
        return;
    }

    connectivity_manager_status_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    _home_render_wifi(state, &snapshot);
}

static void _home_open_app(lv_event_t *event)
{
    app_ui_request_run(lv_event_get_user_data(event));
}

static void _home_page_build(home_page_state_t *state)
{
    app_ui_page_create(&state->page, "MicroTech", false);

    lv_obj_t *clock = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(clock);
    lv_obj_set_width(clock, LV_PCT(100));
    lv_obj_set_height(clock, 132);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(clock, false);

    state->time_label = lv_label_create(clock);
    lv_obj_set_style_text_color(state->time_label, lv_color_hex(0xF4F7F8), 0);
    lv_obj_set_style_text_font(state->time_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    lv_label_set_text(state->time_label, "--:--");

    state->date_label = lv_label_create(clock);
    lv_obj_set_style_text_color(state->date_label, lv_color_hex(0xAAB5BA), 0);
    lv_obj_set_style_text_font(state->date_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->date_label, "等待有效时间");

    state->quality_label = lv_label_create(clock);
    lv_obj_set_style_text_font(state->quality_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(state->quality_label, "时间未校准",
                           APP_UI_STATUS_WARNING);

    app_ui_add_section(state->page.content, "设备状态");
    app_ui_add_value_row(state->page.content, "电源", "读取中",
                         &state->power_value);
    app_ui_add_value_row(state->page.content, "Wi-Fi", "初始化中",
                         &state->wifi_value);
    app_ui_add_value_row(state->page.content, "SD 卡", "检查中",
                         &state->storage_value);
    app_ui_add_value_row(state->page.content, "当前计时", "无",
                         &state->timer_value);

    app_ui_add_section(state->page.content, "快捷入口");
    app_ui_add_action(state->page.content, LV_SYMBOL_GPS, "天气",
                      "当前、24 小时、7 日与气象预警", _home_open_app,
                      (void *)APP_MANAGER_ID_WEATHER);
    app_ui_add_action(state->page.content, LV_SYMBOL_BELL, "时钟",
                      "倒计时、秒表与专注", _home_open_app,
                      (void *)APP_MANAGER_ID_CLOCK);
    app_ui_add_action(state->page.content, LV_SYMBOL_AUDIO, "录音",
                      "WAV 语音备忘", _home_open_app,
                      (void *)APP_MANAGER_ID_RECORDER);
    app_ui_add_action(state->page.content, LV_SYMBOL_SETTINGS, "系统设置",
                      "显示、电源与设备信息", _home_open_app,
                      (void *)APP_MANAGER_ID_SETTINGS);
    app_ui_add_action(state->page.content, LV_SYMBOL_LIST, "应用",
                      "查看设备上的全部功能", _home_open_app,
                      (void *)APP_MANAGER_ID_MENU);

    _home_update_clock(state);
    _home_update_cached_status(state);
}

static void _home_page_resume(home_page_state_t *state)
{
    esp_err_t result = ESP_OK;
    _home_update_clock(state);
    _home_update_cached_status(state);

    if (state->refresh_timer == NULL)
    {
        state->refresh_timer = lv_timer_create(_home_refresh_timer, 1000, state);
        if (state->refresh_timer == NULL)
        {
            LOG_W("refresh timer unavailable");
        }
    }
    if (state->power_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        result = event_bus_subscribe(
                     POWER_SERVICE_MSG,
                     POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE,
                     _home_power_event, state, EVENT_BUS_DISPATCH_UI,
                     &state->power_subscription);
        if (result != ESP_OK)
        {
            state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            LOG_W("power subscription failed: %s", esp_err_to_name(result));
        }
    }
    if (state->wifi_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        result = event_bus_subscribe(
                     CONNECTIVITY_MANAGER_MSG,
                     CONNECTIVITY_MANAGER_MSG_SUB_TYPE_STATUS_SNAPSHOT,
                     _home_wifi_event, state, EVENT_BUS_DISPATCH_UI,
                     &state->wifi_subscription);
        if (result != ESP_OK)
        {
            state->wifi_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            LOG_W("Wi-Fi subscription failed: %s", esp_err_to_name(result));
        }
    }
}

static esp_err_t _home_page_pause(home_page_state_t *state)
{
    esp_err_t first_error = ESP_OK;
    if (state->wifi_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_unsubscribe(state->wifi_subscription);
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->wifi_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
        else
        {
            first_error = result;
        }
    }
    if (state->power_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_unsubscribe(state->power_subscription);
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
        else if (first_error == ESP_OK)
        {
            first_error = result;
        }
    }
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    return first_error;
}

static void _home_page_unmount(home_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->time_label = NULL;
    state->date_label = NULL;
    state->quality_label = NULL;
    state->power_value = NULL;
    state->wifi_value = NULL;
    state->storage_value = NULL;
    state->timer_value = NULL;
}

static void _home_mount(const app_manager_page_context_t *context)
{
    _home_page_build(context->state);
}

static void _home_resume(const app_manager_page_context_t *context)
{
    _home_page_resume(context->state);
}

static esp_err_t _home_pause(const app_manager_page_context_t *context)
{
    return _home_page_pause(context->state);
}

static void _home_unmount(const app_manager_page_context_t *context)
{
    _home_page_unmount(context->state);
}

static const app_manager_page_ops_t s_home_ops =
{
    .mount = _home_mount,
    .resume = _home_resume,
    .pause = _home_pause,
    .unmount = _home_unmount,
};

static const app_manager_page_definition_t s_home_root_definition =
{
    .ops = &s_home_ops,
    .memory_size = sizeof(home_page_state_t),
};

static const app_manager_page_route_t s_home_routes[] =
{
    {
        .page_id = APP_MANAGER_ID_HOME_ROOT,
        .definition = &s_home_root_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(home, APP_IMAGE_HOME_ICON, "主页", APP_MANAGER_ID_HOME,
                            "root", APP_MANAGER_APP_FLAG_PINNED, s_home_routes,
                            1U, "今日概览");
