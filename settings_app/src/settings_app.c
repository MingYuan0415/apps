#define DBG_TAG "settings_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "esp_app_desc.h"
#include "event_bus.h"
#include "power_service.h"
#include "settings_factory_reset_page.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_PAGE_POWER     "power"
#define SETTINGS_PAGE_ABOUT     "about"
#define SETTINGS_PAGE_FACTORY_RESET "factory-reset"
#define SETTINGS_SURFACE_COLOR  0x1A2024
#define SETTINGS_PRESSED_COLOR  0x252D32
#define SETTINGS_TEXT_COLOR     0xF2F5F6

typedef struct settings_root_state settings_root_state_t;

typedef struct settings_timeout_action
{
    settings_root_state_t *state;
    int32_t timeout_ms;
    bool standby;
} settings_timeout_action_t;

struct settings_root_state
{
    app_ui_page_t page;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value;
    lv_obj_t *save_status;
    lv_obj_t *screen_timeout_value;
    lv_obj_t *standby_timeout_value;
    settings_timeout_action_t timeout_actions[7];
    uint8_t brightness;
};

typedef struct settings_power_state
{
    app_ui_page_t page;
    lv_obj_t *battery_value;
    lv_obj_t *voltage_value;
    lv_obj_t *source_value;
    lv_obj_t *sample_value;
    event_bus_sub_handle_t power_subscription;
} settings_power_state_t;

typedef struct settings_about_state
{
    app_ui_page_t page;
} settings_about_state_t;

_Static_assert(sizeof(settings_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(settings_power_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings power state exceeds the lifecycle arena slot");
_Static_assert(sizeof(settings_about_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings about state exceeds the lifecycle arena slot");

static const char *_settings_screen_timeout_text(int32_t timeout_ms)
{
    switch (timeout_ms)
    {
    case 30000:
        return "30 秒";
    case 60000:
        return "1 分钟";
    case 300000:
        return "5 分钟";
    case -1:
        return "从不";
    default:
        return "自定义";
    }
}

static const char *_settings_standby_timeout_text(int32_t timeout_ms)
{
    switch (timeout_ms)
    {
    case 5000:
        return "5 秒";
    case 30000:
        return "30 秒";
    case -1:
        return "从不";
    default:
        return "自定义";
    }
}

static void _settings_render_brightness(settings_root_state_t *state,
                                        uint8_t brightness)
{
    unsigned percent = ((unsigned)brightness * 100U + 127U) / 255U;
    lv_label_set_text_fmt(state->brightness_value, "%u%%", percent);
}

static void _settings_brightness_event(lv_event_t *event)
{
    settings_root_state_t *state = lv_event_get_user_data(event);
    uint8_t brightness = (uint8_t)lv_slider_get_value(state->brightness_slider);
    state->brightness = brightness;
    _settings_render_brightness(state, brightness);

    esp_err_t result = ESP_OK;
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
        result = app_manager_screen_set_brightness_temp(brightness);
        app_ui_set_status_text(state->save_status,
                               result == ESP_OK ? "正在预览" : "预览队列繁忙",
                               result == ESP_OK ? APP_UI_STATUS_ACCENT :
                               APP_UI_STATUS_WARNING);
    }
    else if (lv_event_get_code(event) == LV_EVENT_RELEASED)
    {
        result = app_manager_screen_set_brightness(brightness);
        app_ui_set_status_text(state->save_status,
                               result == ESP_OK ? "保存请求已提交" :
                               "保存请求失败",
                               result == ESP_OK ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_ERROR);
    }
}

static void _settings_timeout_event(lv_event_t *event)
{
    settings_timeout_action_t *action = lv_event_get_user_data(event);
    if (action == NULL || action->state == NULL)
    {
        return;
    }
    esp_err_t result = action->standby ?
                       app_manager_pm_set_standby_delay_ms(action->timeout_ms) :
                       app_manager_pm_set_timeout_ms(action->timeout_ms);
    lv_obj_t *label = action->standby ?
                      action->state->standby_timeout_value :
                      action->state->screen_timeout_value;
    if (result == ESP_OK)
    {
        const char *text = action->standby ?
                           _settings_standby_timeout_text(action->timeout_ms) :
                           _settings_screen_timeout_text(action->timeout_ms);
        app_ui_set_status_text(label, text, APP_UI_STATUS_SUCCESS);
    }
    else
    {
        app_ui_set_status_text(label, "保存失败", APP_UI_STATUS_ERROR);
        LOG_W("timeout update failed: %s", esp_err_to_name(result));
    }
}

static lv_obj_t *_settings_add_segment(settings_root_state_t *state,
                                       lv_obj_t *parent, const char *text,
                                       int32_t timeout_ms, bool standby,
                                       uint8_t action_index)
{
    settings_timeout_action_t *action = &state->timeout_actions[action_index];
    action->state = state;
    action->timeout_ms = timeout_ms;
    action->standby = standby;

    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_height(button, 44);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(SETTINGS_SURFACE_COLOR), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(SETTINGS_PRESSED_COLOR),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    lv_obj_add_event_cb(button, _settings_timeout_event, LV_EVENT_CLICKED,
                        action);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(SETTINGS_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *_settings_create_segment_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void _settings_open_power_event(lv_event_t *event)
{
    (void)event;
    app_ui_request_open_page(APP_MANAGER_ID_SETTINGS, SETTINGS_PAGE_POWER);
}

static void _settings_open_about_event(lv_event_t *event)
{
    (void)event;
    app_ui_request_open_page(APP_MANAGER_ID_SETTINGS, SETTINGS_PAGE_ABOUT);
}

static void _settings_open_factory_reset_event(lv_event_t *event)
{
    (void)event;
    app_ui_request_open_page(APP_MANAGER_ID_SETTINGS,
                             SETTINGS_PAGE_FACTORY_RESET);
}

static void _settings_root_build(settings_root_state_t *state)
{
    app_ui_page_create(&state->page, "系统设置", true);
    app_ui_add_section(state->page.content, "显示");

    lv_obj_t *brightness = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(brightness);
    lv_obj_set_width(brightness, LV_PCT(100));
    lv_obj_set_height(brightness, 116);
    lv_obj_set_style_bg_color(brightness,
                              lv_color_hex(SETTINGS_SURFACE_COLOR), 0);
    lv_obj_set_style_bg_opa(brightness, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(brightness, 6, 0);
    lv_obj_set_style_pad_all(brightness, 14, 0);
    lv_obj_set_style_pad_row(brightness, 8, 0);
    lv_obj_set_flex_flow(brightness, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(brightness, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_obj_create(brightness);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(heading, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(heading);
    lv_label_set_text(label, "亮度");
    lv_obj_set_style_text_color(label, lv_color_hex(SETTINGS_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);

    state->brightness_value = lv_label_create(heading);
    lv_obj_set_style_text_font(state->brightness_value,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    app_ui_set_status_text(state->brightness_value, "--",
                           APP_UI_STATUS_ACCENT);

    state->brightness_slider = lv_slider_create(brightness);
    lv_obj_set_width(state->brightness_slider, LV_PCT(100));
    lv_obj_set_height(state->brightness_slider, 20);
    lv_slider_set_range(state->brightness_slider, 10, 255);
    lv_slider_set_value(state->brightness_slider, state->brightness,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(0x30393E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(0x39C6C8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(SETTINGS_TEXT_COLOR), LV_PART_KNOB);
    lv_obj_add_event_cb(state->brightness_slider, _settings_brightness_event,
                        LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(state->brightness_slider, _settings_brightness_event,
                        LV_EVENT_RELEASED, state);

    state->save_status = lv_label_create(brightness);
    lv_obj_set_style_text_font(state->save_status,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(state->save_status, "已保存亮度",
                           APP_UI_STATUS_NEUTRAL);
    _settings_render_brightness(state, state->brightness);

    app_ui_add_section(state->page.content, "电源策略");
    app_ui_add_value_row(state->page.content, "自动熄屏", "--",
                         &state->screen_timeout_value);
    lv_obj_t *screen_options = _settings_create_segment_row(state->page.content);
    (void)_settings_add_segment(state, screen_options, "30 秒", 30000, false, 0);
    (void)_settings_add_segment(state, screen_options, "1 分钟", 60000, false, 1);
    (void)_settings_add_segment(state, screen_options, "5 分钟", 300000, false, 2);
    (void)_settings_add_segment(state, screen_options, "从不", -1, false, 3);

    app_ui_add_value_row(state->page.content, "熄屏后待机", "--",
                         &state->standby_timeout_value);
    lv_obj_t *standby_options = _settings_create_segment_row(state->page.content);
    (void)_settings_add_segment(state, standby_options, "5 秒", 5000, true, 4);
    (void)_settings_add_segment(state, standby_options, "30 秒", 30000, true, 5);
    (void)_settings_add_segment(state, standby_options, "从不", -1, true, 6);

    app_ui_add_section(state->page.content, "设备");
    app_ui_add_action(state->page.content, LV_SYMBOL_POWER, "电源状态",
                      "电池、供电与熄屏控制", _settings_open_power_event, NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_EYE_OPEN, "关于设备",
                      "硬件与固件构建信息", _settings_open_about_event, NULL);

    app_ui_add_section(state->page.content, "维护");
    app_ui_add_action(state->page.content, LV_SYMBOL_TRASH, "恢复出厂设置",
                      "清除本机数据并重新启动",
                      _settings_open_factory_reset_event, NULL);
}

static void _settings_root_resume(settings_root_state_t *state)
{
    state->brightness = app_manager_screen_get_brightness();
    lv_slider_set_value(state->brightness_slider, state->brightness,
                        LV_ANIM_OFF);
    _settings_render_brightness(state, state->brightness);
    app_ui_set_status_text(state->save_status, "已保存亮度",
                           APP_UI_STATUS_NEUTRAL);
    lv_label_set_text(state->screen_timeout_value,
                      _settings_screen_timeout_text(
                          app_manager_pm_get_timeout_ms()));
    lv_label_set_text(state->standby_timeout_value,
                      _settings_standby_timeout_text(
                          app_manager_pm_get_standby_delay_ms()));
}

static void _settings_root_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    settings_root_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->brightness = app_manager_screen_get_brightness();
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _settings_root_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _settings_root_resume(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        state->brightness_slider = NULL;
        state->brightness_value = NULL;
        state->save_status = NULL;
        state->screen_timeout_value = NULL;
        state->standby_timeout_value = NULL;
        break;
    case APP_MANAGER_MSG_ONSTOP:
        LOG_I("stopped");
        break;
    default:
        break;
    }
}

static void _settings_power_render(settings_power_state_t *state,
                                   const power_service_snapshot_t *snapshot)
{
    if (!snapshot->valid)
    {
        app_ui_set_status_text(state->battery_value, "不可用",
                               APP_UI_STATUS_ERROR);
        lv_label_set_text(state->voltage_value, "--");
        lv_label_set_text(state->source_value, "PMU 离线");
        lv_label_set_text(state->sample_value, "无有效采样");
        return;
    }
    if (snapshot->info.battery_percent >= 0)
    {
        char percent[8];
        (void)snprintf(percent, sizeof(percent), "%d%%",
                       snapshot->info.battery_percent);
        app_ui_set_status_text(
            state->battery_value, percent,
            snapshot->info.is_charging ? APP_UI_STATUS_SUCCESS :
            APP_UI_STATUS_NEUTRAL);
    }
    else
    {
        app_ui_set_status_text(state->battery_value, "未知",
                               APP_UI_STATUS_WARNING);
    }
    lv_label_set_text_fmt(state->voltage_value, "%u mV",
                          (unsigned)snapshot->info.battery_voltage_mv);
    lv_label_set_text(state->source_value,
                      snapshot->info.is_charging ? "充电中" :
                      (snapshot->info.is_vbus_connected ? "USB 供电" :
                       "电池供电"));
    lv_label_set_text_fmt(state->sample_value, "%lld ms",
                          (long long)snapshot->sampled_at_ms);
}

static void _settings_power_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                  const void *payload, size_t payload_size,
                                  void *user_data)
{
    settings_power_state_t *state = user_data;
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
    _settings_power_render(state, &snapshot);
}

static void _settings_screen_off_event(lv_event_t *event)
{
    (void)event;
    esp_err_t result = app_manager_pm_request_screen_off();
    if (result != ESP_OK)
    {
        LOG_W("screen-off request failed: %s", esp_err_to_name(result));
    }
}

static void _settings_power_build(settings_power_state_t *state)
{
    app_ui_page_create(&state->page, "电源状态", true);
    app_ui_add_section(state->page.content, "电池缓存");
    app_ui_add_value_row(state->page.content, "电量", "读取中",
                         &state->battery_value);
    app_ui_add_value_row(state->page.content, "电压", "--",
                         &state->voltage_value);
    app_ui_add_value_row(state->page.content, "供电来源", "--",
                         &state->source_value);
    app_ui_add_value_row(state->page.content, "采样时刻", "--",
                         &state->sample_value);

    app_ui_add_section(state->page.content, "电源管理");
    app_ui_add_body_label(state->page.content,
                          "熄屏或待机后使用 HOME 恢复。");
    app_ui_add_command(state->page.content, LV_SYMBOL_POWER, "立即熄屏",
                       "按当前策略继续进入待机", _settings_screen_off_event,
                       NULL);

    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) == ESP_OK)
    {
        _settings_power_render(state, &snapshot);
    }
    else
    {
        snapshot = (power_service_snapshot_t)
        {
            0
        };
        _settings_power_render(state, &snapshot);
    }
}

static void _settings_power_resume(settings_power_state_t *state)
{
    if (state->power_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_subscribe(
                               POWER_SERVICE_MSG,
                               POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE,
                               _settings_power_event, state,
                               EVENT_BUS_DISPATCH_UI,
                               &state->power_subscription);
        if (result != ESP_OK)
        {
            state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
            LOG_W("power subscription failed: %s", esp_err_to_name(result));
        }
    }
    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) == ESP_OK)
    {
        _settings_power_render(state, &snapshot);
    }
    else
    {
        snapshot = (power_service_snapshot_t)
        {
            0
        };
        _settings_power_render(state, &snapshot);
    }
}

static esp_err_t _settings_power_pause(settings_power_state_t *state)
{
    if (state->power_subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return ESP_OK;
    }
    esp_err_t result = event_bus_unsubscribe(state->power_subscription);
    if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
    {
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        return ESP_OK;
    }
    app_manager_this_page_report_cleanup_error(result);
    return result;
}

static void _settings_power_unmount(settings_power_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->battery_value = NULL;
    state->voltage_value = NULL;
    state->source_value = NULL;
    state->sample_value = NULL;
}

static void _settings_power_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    settings_power_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _settings_power_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _settings_power_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_settings_power_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _settings_power_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        (void)_settings_power_pause(state);
        break;
    default:
        break;
    }
}

static void _settings_about_build(settings_about_state_t *state)
{
    app_ui_page_create(&state->page, "关于设备", true);
    const esp_app_desc_t *description = esp_app_get_description();

    lv_obj_t *name = lv_label_create(state->page.content);
    lv_label_set_text(name, "MicroTech");
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_color(name, lv_color_hex(SETTINGS_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BIGL), 0);

    app_ui_add_body_label(state->page.content,
                          "Waveshare ESP32-S3 Touch AMOLED 1.8 阶段演示固件");
    app_ui_add_section(state->page.content, "硬件");
    app_ui_add_value_row(state->page.content, "显示", "368 x 448 AMOLED", NULL);
    app_ui_add_value_row(state->page.content, "平台", "ESP32-S3", NULL);

    app_ui_add_section(state->page.content, "固件");
    app_ui_add_value_row(state->page.content, "项目",
                         description != NULL ? description->project_name : "--",
                         NULL);
    app_ui_add_value_row(state->page.content, "版本",
                         description != NULL ? description->version : "--", NULL);
    app_ui_add_value_row(state->page.content, "ESP-IDF",
                         description != NULL ? description->idf_ver : "--", NULL);
    if (description != NULL)
    {
        char build[40];
        (void)snprintf(build, sizeof(build), "%s %s",
                       description->date, description->time);
        app_ui_add_value_row(state->page.content, "构建", build, NULL);
    }
}

static void _settings_about_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    settings_about_state_t *state = app_manager_this_page_memory();
    if (message == APP_MANAGER_MSG_ONSTART)
    {
        memset(state, 0, sizeof(*state));
    }
    else if (message == APP_MANAGER_MSG_ONMOUNT && state->page.root == NULL)
    {
        _settings_about_build(state);
    }
    else if (message == APP_MANAGER_MSG_ONUNMOUNT)
    {
        app_ui_page_destroy(&state->page);
    }
}

static const app_manager_page_definition_t s_settings_root_definition =
{
    .handler = _settings_root_handler,
    .memory_size = sizeof(settings_root_state_t),
};

static const app_manager_page_definition_t s_settings_power_definition =
{
    .handler = _settings_power_handler,
    .memory_size = sizeof(settings_power_state_t),
};

static const app_manager_page_definition_t s_settings_about_definition =
{
    .handler = _settings_about_handler,
    .memory_size = sizeof(settings_about_state_t),
};

static const app_manager_page_route_t s_settings_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_settings_root_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_POWER,
        .definition = &s_settings_power_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_ABOUT,
        .definition = &s_settings_about_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_FACTORY_RESET,
        .definition = &settings_factory_reset_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT(settings, NULL, "系统设置", APP_MANAGER_ID_SETTINGS, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_settings_routes);
