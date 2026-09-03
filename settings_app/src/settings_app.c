#define DBG_TAG "settings_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"
#include "settings_factory_reset_page.h"

#include "esp_app_desc.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

typedef struct settings_root_state
{
    app_ui_page_t page;
    lv_obj_t *display_summary;
    lv_obj_t *policy_summary;
    lv_obj_t *device_summary;
    lv_obj_t *power_summary;
    lv_obj_t *about_summary;
} settings_root_state_t;

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
    uint32_t last_tap_ms;
    uint8_t tap_count;
} settings_about_state_t;

typedef struct settings_info_state
{
    app_ui_page_t page;
    lv_obj_t *source_value;
    lv_obj_t *detail_value;
    lv_timer_t *refresh_timer;
    bool storage;
} settings_info_state_t;

_Static_assert(sizeof(settings_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings root state exceeds the lifecycle arena slot");
_Static_assert(sizeof(settings_power_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings power state exceeds the lifecycle arena slot");
_Static_assert(sizeof(settings_about_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings about state exceeds the lifecycle arena slot");
_Static_assert(sizeof(settings_info_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings info state exceeds the lifecycle arena slot");

const char *settings_ui_screen_timeout_text(int32_t timeout_ms)
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

const char *settings_ui_standby_timeout_text(int32_t timeout_ms)
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

static void _settings_open_page_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    app_ui_request_open_page(APP_MANAGER_ID_SETTINGS,
                             (const char *)lv_event_get_user_data(event));
}

static void _settings_root_render(settings_root_state_t *state)
{
    char text[96];
    const uint8_t brightness = app_manager_screen_get_brightness();
    (void)snprintf(text, sizeof(text), "%u%%",
                   (unsigned)(((unsigned)brightness * 100U + 127U) / 255U));
    lv_label_set_text(state->display_summary, text);

    (void)snprintf(text, sizeof(text), "熄屏 %s · 待机 %s",
                   settings_ui_screen_timeout_text(
                       app_manager_pm_get_timeout_ms()),
                   settings_ui_standby_timeout_text(
                       app_manager_pm_get_standby_delay_ms()));
    lv_label_set_text(state->policy_summary, text);

    const char *wifi = "未连接";
    connectivity_manager_status_snapshot_t wifi_status;
    if (connectivity_manager_get_status(&wifi_status) == ESP_OK)
    {
        wifi = wifi_status.state == CONNECTIVITY_MANAGER_STATE_IP_READY ?
               "已连接" : (wifi_status.state ==
                              CONNECTIVITY_MANAGER_STATE_CONNECTING ? "连接中" :
                              "未连接");
    }
    device_link_service_status_t bluetooth;
    const char *bt = "不可用";
    if (device_link_service_get_status(&bluetooth) == ESP_OK)
    {
        bt = bluetooth.bound ? "已绑定" : (bluetooth.active ? "绑定中" :
                                              "未绑定");
    }
    (void)snprintf(text, sizeof(text), "Wi-Fi %s · 蓝牙 %s · SD %s", wifi, bt,
                   sd_storage_service_is_mounted() ? "已挂载" : "未挂载");
    lv_label_set_text(state->device_summary, text);

    power_service_snapshot_t power;
    if (power_service_get_snapshot(&power) == ESP_OK && power.valid &&
            power.info.battery_percent >= 0)
    {
        (void)snprintf(text, sizeof(text), "%d%% %s",
                       power.info.battery_percent,
                       power.info.is_charging ? "充电中" :
                       (power.info.is_vbus_connected ? "USB 供电" : "电池"));
    }
    else
    {
        (void)snprintf(text, sizeof(text), "读取中");
    }
    lv_label_set_text(state->power_summary, text);

    const esp_app_desc_t *description = esp_app_get_description();
    lv_label_set_text(state->about_summary,
                      description != NULL ? description->version : "--");
}

static void _settings_root_mount(const app_manager_page_context_t *context)
{
    settings_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "系统设置", true);
    app_ui_page_set_subtitle(&state->page, "设备与电源");
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    (void)app_ui_add_entry_row(state->page.content, "显示与亮度",
                               &state->display_summary,
                               _settings_open_page_event,
                               (void *)SETTINGS_PAGE_DISPLAY);
    (void)app_ui_add_entry_row(state->page.content, "电源策略",
                               &state->policy_summary,
                               _settings_open_page_event,
                               (void *)SETTINGS_PAGE_POLICY);
    (void)app_ui_add_entry_row(state->page.content, "设备状态",
                               &state->device_summary,
                               _settings_open_page_event,
                               (void *)SETTINGS_PAGE_DEVICE);
    (void)app_ui_add_entry_row(state->page.content, "电源状态",
                               &state->power_summary,
                               _settings_open_page_event,
                               (void *)SETTINGS_PAGE_POWER);
    (void)app_ui_add_entry_row(state->page.content, "关于与维护",
                               &state->about_summary,
                               _settings_open_page_event,
                               (void *)SETTINGS_PAGE_ABOUT);
    _settings_root_render(state);
}

static void _settings_root_resume(const app_manager_page_context_t *context)
{
    _settings_root_render(context->state);
}

static void _settings_root_unmount(const app_manager_page_context_t *context)
{
    settings_root_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->display_summary = NULL;
    state->policy_summary = NULL;
    state->device_summary = NULL;
    state->power_summary = NULL;
    state->about_summary = NULL;
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

static void _settings_power_mount(const app_manager_page_context_t *context)
{
    settings_power_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    app_ui_page_create(&state->page, "电源状态", true);
    app_ui_page_set_subtitle(&state->page, "电池与熄屏");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_value_row(state->page.content, "电量", "读取中",
                         &state->battery_value);
    app_ui_add_value_row(state->page.content, "电压", "--",
                         &state->voltage_value);
    app_ui_add_value_row(state->page.content, "供电来源", "--",
                         &state->source_value);
    app_ui_add_value_row(state->page.content, "采样时刻", "--",
                         &state->sample_value);
    app_ui_add_command(state->page.content, LV_SYMBOL_POWER, "立即熄屏",
                       "熄屏或待机后使用 HOME 恢复",
                       _settings_screen_off_event, NULL);

    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) != ESP_OK)
    {
        snapshot = (power_service_snapshot_t)
        {
            0
        };
    }
    _settings_power_render(state, &snapshot);
}

static void _settings_power_resume(const app_manager_page_context_t *context)
{
    settings_power_state_t *state = context->state;
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
}

static esp_err_t _settings_power_pause(const app_manager_page_context_t *context)
{
    settings_power_state_t *state = context->state;
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
    return result;
}

static void _settings_power_unmount(const app_manager_page_context_t *context)
{
    settings_power_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->battery_value = NULL;
    state->voltage_value = NULL;
    state->source_value = NULL;
    state->sample_value = NULL;
}

static void _settings_info_refresh(settings_info_state_t *state)
{
    if (state->storage)
    {
        const bool mounted = sd_storage_service_is_mounted();
        app_ui_set_status_text(state->source_value,
                               mounted ? "已挂载" : "未挂载",
                               mounted ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_WARNING);
        lv_label_set_text(state->detail_value,
                          mounted ? sd_storage_service_get_mount_path() :
                          "可在重新插入 SD 卡后重试");
        return;
    }
    const time_service_quality_t quality = time_service_get_quality();
    struct tm local_time;
    char text[64];
    const char *quality_text = quality == TIME_SERVICE_QUALITY_NTP ? "网络校时" :
                               (quality == TIME_SERVICE_QUALITY_RTC ? "RTC" :
                                (quality == TIME_SERVICE_QUALITY_MANUAL ?
                                 "手动设置" : "无有效时间"));
    app_ui_set_status_text(state->source_value, quality_text,
                           quality == TIME_SERVICE_QUALITY_INVALID ?
                           APP_UI_STATUS_WARNING : APP_UI_STATUS_SUCCESS);
    if (time_service_get_local(&local_time) == ESP_OK)
    {
        (void)strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local_time);
        lv_label_set_text(state->detail_value, text);
    }
    else
    {
        lv_label_set_text(state->detail_value, "等待有效时间");
    }
}

static void _settings_info_timer(lv_timer_t *timer)
{
    _settings_info_refresh(lv_timer_get_user_data(timer));
}

static void _settings_info_mount(const app_manager_page_context_t *context)
{
    settings_info_state_t *state = context->state;
    const bool storage = context->route_user_data != NULL;
    memset(state, 0, sizeof(*state));
    state->storage = storage;
    app_ui_page_create(&state->page, storage ? "存储管理" : "时间设置", true);
    app_ui_page_set_subtitle(&state->page, storage ? "SD 卡与容量" : "时区与校时");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_value_row(state->page.content, storage ? "SD 卡" : "时间来源",
                         "读取中", &state->source_value);
    app_ui_add_value_row(state->page.content, storage ? "挂载路径" : "当前时间",
                         "读取中", &state->detail_value);
    if (storage)
    {
        app_ui_add_body_label(state->page.content,
                              "安全卸载将在录音停止后由存储服务执行。");
    }
    else
    {
        app_ui_add_body_label(state->page.content,
                              "网络校时需要设备已连接 Wi-Fi。");
    }
    state->refresh_timer = lv_timer_create(_settings_info_timer, 1000U, state);
    _settings_info_refresh(state);
}

static esp_err_t _settings_info_pause(const app_manager_page_context_t *context)
{
    settings_info_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _settings_info_resume(const app_manager_page_context_t *context)
{
    settings_info_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _settings_info_refresh(state);
}

static void _settings_info_unmount(const app_manager_page_context_t *context)
{
    settings_info_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->source_value = NULL;
    state->detail_value = NULL;
}

static void _settings_about_tap_event(lv_event_t *event)
{
    settings_about_state_t *state = lv_event_get_user_data(event);
    const uint32_t now = xTaskGetTickCount();
    state->tap_count = now - state->last_tap_ms > 1500U ? 1U :
                       (uint8_t)(state->tap_count + 1U);
    state->last_tap_ms = now;
    if (state->tap_count >= 5U)
    {
        state->tap_count = 0U;
        app_ui_request_run(APP_MANAGER_ID_DIAGNOSTICS);
    }
}

static void _settings_about_mount(const app_manager_page_context_t *context)
{
    settings_about_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "关于与维护", true);
    app_ui_page_set_subtitle(&state->page, "硬件与固件");

    const esp_app_desc_t *description = esp_app_get_description();

    lv_obj_t *name_button = lv_button_create(state->page.content);
    lv_obj_set_width(name_button, LV_PCT(100));
    lv_obj_set_height(name_button, 72);
    lv_obj_set_style_bg_opa(name_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(name_button, 0, 0);
    lv_obj_set_style_shadow_width(name_button, 0, 0);
    lv_obj_add_event_cb(name_button, _settings_about_tap_event,
                        LV_EVENT_CLICKED, state);
    lv_obj_t *name = lv_label_create(name_button);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_label_set_text(name, "MicroTech");
    lv_obj_center(name);

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

    app_ui_add_section(state->page.content, "维护");
    (void)app_ui_add_danger_action(state->page.content, LV_SYMBOL_TRASH,
                                   "恢复出厂设置", "清除本机数据并重新启动",
                                   _settings_open_page_event,
                                   (void *)SETTINGS_PAGE_FACTORY_RESET);
}

static void _settings_about_unmount(const app_manager_page_context_t *context)
{
    app_ui_page_destroy(&((settings_about_state_t *)context->state)->page);
}

static const app_manager_page_ops_t s_settings_root_ops =
{
    .mount = _settings_root_mount,
    .resume = _settings_root_resume,
    .unmount = _settings_root_unmount,
};

static const app_manager_page_ops_t s_settings_power_ops =
{
    .mount = _settings_power_mount,
    .resume = _settings_power_resume,
    .pause = _settings_power_pause,
    .unmount = _settings_power_unmount,
};

static const app_manager_page_ops_t s_settings_about_ops =
{
    .mount = _settings_about_mount,
    .unmount = _settings_about_unmount,
};

static const app_manager_page_ops_t s_settings_info_ops =
{
    .mount = _settings_info_mount,
    .resume = _settings_info_resume,
    .pause = _settings_info_pause,
    .unmount = _settings_info_unmount,
};

const app_manager_page_definition_t settings_root_page_definition =
{
    .ops = &s_settings_root_ops,
    .memory_size = sizeof(settings_root_state_t),
};

const app_manager_page_definition_t settings_power_page_definition =
{
    .ops = &s_settings_power_ops,
    .memory_size = sizeof(settings_power_state_t),
};

const app_manager_page_definition_t settings_about_page_definition =
{
    .ops = &s_settings_about_ops,
    .memory_size = sizeof(settings_about_state_t),
};

const app_manager_page_definition_t settings_time_page_definition =
{
    .ops = &s_settings_info_ops,
    .memory_size = sizeof(settings_info_state_t),
};

const app_manager_page_definition_t settings_storage_page_definition =
{
    .ops = &s_settings_info_ops,
    .memory_size = sizeof(settings_info_state_t),
};

static const app_manager_page_route_t s_settings_routes[] =
{
    {
        .page_id = "root",
        .definition = &settings_root_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_DISPLAY,
        .definition = &settings_display_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_POLICY,
        .definition = &settings_policy_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_DEVICE,
        .definition = &settings_device_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_POWER,
        .definition = &settings_power_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_ABOUT,
        .definition = &settings_about_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_FACTORY_RESET,
        .definition = &settings_factory_reset_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_TIME,
        .definition = &settings_time_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_STORAGE,
        .definition = &settings_storage_page_definition,
        .user_data = (void *)1,
    },
};

APP_MANAGER_APP_EXPORT_META(settings, APP_IMAGE_SETTINGS_ICON, "系统设置",
                            APP_MANAGER_ID_SETTINGS, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_settings_routes, 50U,
                            "设备与电源");
