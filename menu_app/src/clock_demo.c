#define DBG_TAG "clock_demo"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "clock_demo_adapter.h"
#include "event_bus.h"
#include "menu_page_definitions.h"
#include "time_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOCK_REFRESH_PERIOD_MS 1000U
#define CLOCK_UTC8_OFFSET_SEC   (8 * 60 * 60)
#define CLOCK_PAGE_STATE_LIMIT  2728U

typedef struct clock_page_state
{
    app_ui_page_t page;
    lv_obj_t *local_value;
    lv_obj_t *utc8_value;
    lv_obj_t *quality_value;
    lv_obj_t *sync_status;
    lv_obj_t *alarm_status;
    lv_timer_t *refresh_timer;
    event_bus_sub_handle_t alarm_subscription;
    clock_demo_adapter_t adapter;
    uint32_t last_alarm_sequence;
    bool alarm_fired;
    bool alarm_disarm_pending;
} clock_page_state_t;

_Static_assert(sizeof(clock_page_state_t) <= CLOCK_PAGE_STATE_LIMIT,
               "clock page state exceeds the retained-page slot");
_Static_assert(sizeof(time_service_alarm_event_t) <=
               EVENT_BUS_MAX_UI_PAYLOAD_SIZE,
               "RTC alarm event exceeds the event-bus envelope");

static const char *_clock_quality_text(time_service_quality_t quality)
{
    const char *text = "不可用";
    switch (quality)
    {
    case TIME_SERVICE_QUALITY_RTC:
        text = "RTC 时间";
        break;
    case TIME_SERVICE_QUALITY_MANUAL:
        text = "手动设置";
        break;
    case TIME_SERVICE_QUALITY_NTP:
        text = "网络时间";
        break;
    case TIME_SERVICE_QUALITY_INVALID:
    default:
        break;
    }
    return text;
}

static const char *_clock_error_text(esp_err_t result)
{
    const char *text = esp_err_to_name(result);
    switch (result)
    {
    case ESP_ERR_INVALID_STATE:
        text = "当前状态不可用";
        break;
    case ESP_ERR_NOT_SUPPORTED:
        text = "硬件不支持";
        break;
    case ESP_ERR_NO_MEM:
        text = "资源不足";
        break;
    case ESP_ERR_TIMEOUT:
        text = "操作超时";
        break;
    default:
        break;
    }
    return text;
}

static void _clock_set_error(lv_obj_t *label, const char *operation,
                             esp_err_t result)
{
    char text[96];
    snprintf(text, sizeof(text), "%s：%s", operation,
             _clock_error_text(result));
    app_ui_set_status_text(label, text, APP_UI_STATUS_ERROR);
}

static void _clock_render_times(clock_page_state_t *state)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) == ESP_OK)
    {
        char text[32];
        if (strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S",
                     &local_time) > 0U)
        {
            lv_label_set_text(state->local_value, text);
        }
    }
    else
    {
        lv_label_set_text(state->local_value, "--");
    }

    struct tm utc_time;
    if (time_service_get_utc(&utc_time) == ESP_OK)
    {
        time_t utc8_epoch = time(NULL) + CLOCK_UTC8_OFFSET_SEC;
        struct tm utc8_time;
        char text[32];
        if (gmtime_r(&utc8_epoch, &utc8_time) != NULL &&
                strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S",
                         &utc8_time) > 0U)
        {
            lv_label_set_text(state->utc8_value, text);
        }
        else
        {
            lv_label_set_text(state->utc8_value, "--");
        }
    }
    else
    {
        lv_label_set_text(state->utc8_value, "--");
    }
    lv_label_set_text(state->quality_value,
                      _clock_quality_text(time_service_get_quality()));
}

static void _clock_render_sync(clock_page_state_t *state,
                               const clock_demo_adapter_snapshot_t *snapshot)
{
    const char *text = "尚未请求网络校时";
    switch (snapshot->sync_state)
    {
    case CLOCK_DEMO_OPERATION_QUEUED:
        text = "校时请求已排队";
        app_ui_set_status_text(state->sync_status, text,
                               APP_UI_STATUS_ACCENT);
        return;
    case CLOCK_DEMO_OPERATION_RUNNING:
        text = "正在发起网络校时";
        app_ui_set_status_text(state->sync_status, text,
                               APP_UI_STATUS_ACCENT);
        return;
    case CLOCK_DEMO_OPERATION_DONE:
        text = time_service_get_quality() == TIME_SERVICE_QUALITY_NTP ?
               "网络时间已同步" : "校时请求已发出";
        app_ui_set_status_text(state->sync_status, text,
                               APP_UI_STATUS_SUCCESS);
        return;
    case CLOCK_DEMO_OPERATION_FAILED:
        _clock_set_error(state->sync_status, "校时失败",
                         snapshot->sync_result);
        return;
    case CLOCK_DEMO_OPERATION_IDLE:
    default:
        break;
    }
    app_ui_set_status_text(state->sync_status, text, APP_UI_STATUS_NEUTRAL);
}

static void _clock_render_alarm(clock_page_state_t *state,
                                const clock_demo_adapter_snapshot_t *snapshot)
{
    if (state->alarm_fired && !snapshot->alarm_owned)
    {
        app_ui_set_status_text(state->alarm_status,
                               "RTC 闹钟已触发并关闭",
                               APP_UI_STATUS_SUCCESS);
        return;
    }

    switch (snapshot->alarm_state)
    {
    case CLOCK_DEMO_OPERATION_QUEUED:
        app_ui_set_status_text(state->alarm_status, "闹钟请求已排队",
                               APP_UI_STATUS_ACCENT);
        break;
    case CLOCK_DEMO_OPERATION_RUNNING:
        app_ui_set_status_text(state->alarm_status, "正在访问 RTC",
                               APP_UI_STATUS_ACCENT);
        break;
    case CLOCK_DEMO_OPERATION_DONE:
        if (snapshot->alarm_owned)
        {
            char text[48];
            snprintf(text, sizeof(text), "已设定 UTC %02u:%02u:%02u",
                     (unsigned)snapshot->alarm_hour,
                     (unsigned)snapshot->alarm_minute,
                     (unsigned)snapshot->alarm_second);
            app_ui_set_status_text(state->alarm_status, text,
                                   APP_UI_STATUS_SUCCESS);
        }
        else
        {
            app_ui_set_status_text(state->alarm_status, "RTC 闹钟已关闭",
                                   APP_UI_STATUS_SUCCESS);
        }
        break;
    case CLOCK_DEMO_OPERATION_FAILED:
        _clock_set_error(state->alarm_status, "闹钟失败",
                         snapshot->alarm_result);
        break;
    case CLOCK_DEMO_OPERATION_IDLE:
    default:
        app_ui_set_status_text(state->alarm_status, "RTC 闹钟未启用",
                               APP_UI_STATUS_NEUTRAL);
        break;
    }
}

static void _clock_retry_alarm_disarm(
    clock_page_state_t *state,
    const clock_demo_adapter_snapshot_t *snapshot)
{
    if (!state->alarm_disarm_pending)
    {
        return;
    }
    if (!snapshot->alarm_owned)
    {
        state->alarm_disarm_pending = false;
        return;
    }
    if (snapshot->alarm_state == CLOCK_DEMO_OPERATION_QUEUED ||
            snapshot->alarm_state == CLOCK_DEMO_OPERATION_RUNNING)
    {
        app_ui_set_status_text(state->alarm_status,
                               "RTC 闹钟已触发，正在关闭",
                               APP_UI_STATUS_ACCENT);
        return;
    }

    const esp_err_t result = clock_demo_adapter_disarm_alarm(&state->adapter);
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->alarm_status,
                               "RTC 闹钟已触发，正在关闭",
                               APP_UI_STATUS_ACCENT);
    }
    else if (result == ESP_ERR_INVALID_STATE)
    {
        app_ui_set_status_text(state->alarm_status,
                               "RTC 闹钟已触发，等待关闭",
                               APP_UI_STATUS_WARNING);
    }
    else
    {
        _clock_set_error(state->alarm_status, "关闭失败", result);
    }
}

static void _clock_refresh(clock_page_state_t *state)
{
    _clock_render_times(state);
    clock_demo_adapter_snapshot_t snapshot;
    if (clock_demo_adapter_get_snapshot(&state->adapter, &snapshot) == ESP_OK)
    {
        _clock_render_sync(state, &snapshot);
        _clock_render_alarm(state, &snapshot);
        _clock_retry_alarm_disarm(state, &snapshot);
    }
}

static void _clock_refresh_timer(lv_timer_t *timer)
{
    clock_page_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL && state->page.root != NULL)
    {
        _clock_refresh(state);
    }
}

static void _clock_sync_event(lv_event_t *event)
{
    clock_page_state_t *state = lv_event_get_user_data(event);
    esp_err_t result = clock_demo_adapter_request_sync(&state->adapter);
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->sync_status, "校时请求已排队",
                               APP_UI_STATUS_ACCENT);
    }
    else
    {
        _clock_set_error(state->sync_status, "无法校时", result);
    }
}

static void _clock_alarm_event(lv_event_t *event)
{
    clock_page_state_t *state = lv_event_get_user_data(event);
    state->alarm_fired = false;
    state->alarm_disarm_pending = false;
    esp_err_t result = clock_demo_adapter_arm_alarm(&state->adapter);
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->alarm_status, "闹钟请求已排队",
                               APP_UI_STATUS_ACCENT);
    }
    else
    {
        _clock_set_error(state->alarm_status, "无法设置", result);
    }
}

static void _clock_rtc_alarm_event(event_bus_msg_id_t msg_id,
                                   uint32_t sub_type,
                                   const void *payload,
                                   size_t payload_size,
                                   void *user_data)
{
    clock_page_state_t *state = user_data;
    if (state == NULL || state->page.root == NULL ||
            msg_id != TIME_SERVICE_MSG ||
            sub_type != TIME_SERVICE_MSG_SUB_TYPE_RTC_ALARM ||
            payload == NULL ||
            payload_size != sizeof(time_service_alarm_event_t))
    {
        return;
    }

    clock_demo_adapter_snapshot_t snapshot;
    if (clock_demo_adapter_get_snapshot(&state->adapter, &snapshot) != ESP_OK ||
            !snapshot.alarm_owned)
    {
        return;
    }
    time_service_alarm_event_t alarm_event;
    memcpy(&alarm_event, payload, sizeof(alarm_event));
    if (alarm_event.sequence == 0U ||
            alarm_event.sequence == state->last_alarm_sequence)
    {
        return;
    }

    state->last_alarm_sequence = alarm_event.sequence;
    state->alarm_fired = true;
    state->alarm_disarm_pending = true;
    app_ui_set_status_text(state->alarm_status,
                           "RTC 闹钟已触发，正在关闭",
                           APP_UI_STATUS_ACCENT);
    _clock_retry_alarm_disarm(state, &snapshot);
}

static void _clock_page_build(clock_page_state_t *state)
{
    app_ui_page_create(&state->page, "时间实验", true);
    app_ui_add_section(state->page.content, "当前时间");
    app_ui_add_value_row(state->page.content, "本地", "--",
                         &state->local_value);
    app_ui_add_value_row(state->page.content, "UTC+8", "--",
                         &state->utc8_value);
    app_ui_add_value_row(state->page.content, "时间来源", "不可用",
                         &state->quality_value);

    app_ui_add_section(state->page.content, "网络校时");
    app_ui_add_command(state->page.content, LV_SYMBOL_REFRESH,
                       "立即校时", "通过 SNTP 更新系统时间",
                       _clock_sync_event, state);
    state->sync_status = app_ui_add_body_label(
                             state->page.content, "尚未请求网络校时");

    app_ui_add_section(state->page.content, "RTC 闹钟");
    app_ui_add_command(state->page.content, LV_SYMBOL_BELL,
                       "10 秒闹钟", "仅验证设备清醒时的 RTC 事件",
                       _clock_alarm_event, state);
    state->alarm_status = app_ui_add_body_label(
                              state->page.content, "RTC 闹钟未启用");
    _clock_render_times(state);
}

static esp_err_t _clock_page_subscribe(clock_page_state_t *state)
{
    if (state->alarm_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return ESP_OK;
    }
    esp_err_t result = event_bus_subscribe(
                           TIME_SERVICE_MSG,
                           TIME_SERVICE_MSG_SUB_TYPE_RTC_ALARM,
                           _clock_rtc_alarm_event, state,
                           EVENT_BUS_DISPATCH_UI,
                           &state->alarm_subscription);
    if (result != ESP_OK)
    {
        state->alarm_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
    return result;
}

static void _clock_page_resume(clock_page_state_t *state)
{
    esp_err_t result = ESP_OK;
    if (!clock_demo_adapter_is_open(&state->adapter))
    {
        result = clock_demo_adapter_open(&state->adapter);
        if (result != ESP_OK)
        {
            _clock_set_error(state->sync_status, "工作线程不可用", result);
            _clock_set_error(state->alarm_status, "工作线程不可用", result);
        }
    }
    result = _clock_page_subscribe(state);
    if (result != ESP_OK)
    {
        _clock_set_error(state->alarm_status, "事件订阅失败", result);
    }
    if (state->refresh_timer == NULL)
    {
        state->refresh_timer = lv_timer_create(
                                   _clock_refresh_timer,
                                   CLOCK_REFRESH_PERIOD_MS, state);
        if (state->refresh_timer == NULL)
        {
            LOG_W("refresh timer unavailable");
        }
    }
    _clock_refresh(state);
}

static void _clock_record_cleanup_error(esp_err_t *first_error,
                                        esp_err_t result)
{
    if (*first_error == ESP_OK && result != ESP_OK)
    {
        *first_error = result;
    }
}

static esp_err_t _clock_page_pause(clock_page_state_t *state)
{
    esp_err_t first_error = ESP_OK;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    if (state->alarm_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_unsubscribe(state->alarm_subscription);
        if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
        {
            state->alarm_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        }
        else
        {
            _clock_record_cleanup_error(&first_error, result);
        }
    }
    if (clock_demo_adapter_is_open(&state->adapter))
    {
        esp_err_t result = clock_demo_adapter_close(&state->adapter);
        _clock_record_cleanup_error(&first_error, result);
    }
    if (first_error != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(first_error);
        LOG_W("cleanup incomplete: %s", esp_err_to_name(first_error));
    }
    return first_error;
}

static void _clock_page_unmount(clock_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->local_value = NULL;
    state->utc8_value = NULL;
    state->quality_value = NULL;
    state->sync_status = NULL;
    state->alarm_status = NULL;
}

static void _clock_page_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    clock_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->alarm_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _clock_page_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _clock_page_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_clock_page_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _clock_page_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        if (_clock_page_pause(state) == ESP_OK)
        {
            LOG_I("stopped");
        }
        break;
    default:
        break;
    }
}

const app_manager_page_definition_t menu_clock_page_definition =
{
    .handler = _clock_page_handler,
    .memory_size = sizeof(clock_page_state_t),
};
