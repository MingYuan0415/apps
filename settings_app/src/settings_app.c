#define DBG_TAG "settings_app"
#include "mt_log.h"

#include "settings_app_internal.h"
#include "audio_service.h"
#include "file_service.h"
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
    lv_obj_t *sound_summary;
    lv_obj_t *wifi_summary;
    lv_obj_t *bluetooth_summary;
    lv_obj_t *device_summary;
    lv_obj_t *about_summary;
} settings_root_state_t;

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
} settings_info_state_t;

_Static_assert(sizeof(settings_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Settings root state exceeds the lifecycle arena slot");
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

static void _settings_open_setup_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_run(APP_MANAGER_ID_SETUP);
    }
}

static void _settings_root_render(settings_root_state_t *state)
{
    char text[96];
    const uint8_t brightness = app_manager_screen_get_brightness();
    /* The trailing value is short by design; the full timeout set lives on the
     * display page. */
    (void)snprintf(text, sizeof(text), "%u%% · %s",
                   (unsigned)(((unsigned)brightness * 100U + 127U) / 255U),
                   settings_ui_screen_timeout_text(
                       app_manager_pm_get_timeout_ms()));
    lv_label_set_text(state->display_summary, text);

    uint8_t volume = 0U;
    if (audio_service_get_volume(&volume) == ESP_OK && volume <= 100U)
    {
        (void)snprintf(text, sizeof(text), "%u%%", (unsigned)volume);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "不可用");
    }
    lv_label_set_text(state->sound_summary, text);

    const char *wifi = "未连接";
    connectivity_manager_status_snapshot_t wifi_status;
    if (connectivity_manager_get_status(&wifi_status) == ESP_OK)
    {
        if (wifi_status.manual_hold)
        {
            wifi = "已关闭";
        }
        else
        {
            wifi = wifi_status.state == CONNECTIVITY_MANAGER_STATE_IP_READY ?
                   "已连接" :
                   (wifi_status.state == CONNECTIVITY_MANAGER_STATE_CONNECTING ||
                    wifi_status.state == CONNECTIVITY_MANAGER_STATE_WAITING_IP ?
                    "连接中" : "未连接");
        }
    }
    lv_label_set_text(state->wifi_summary, wifi);

    const char *bt = "不可用";
    device_link_service_status_t bluetooth;
    if (device_link_service_get_status(&bluetooth) == ESP_OK)
    {
        bt = !bluetooth.enabled ? "已关闭" : (bluetooth.bound ? "已绑定" :
            (bluetooth.active ? "配对中" : "未绑定"));
    }
    lv_label_set_text(state->bluetooth_summary, bt);

    file_service_snapshot_t files;
    const char *storage = "未知";
    if (file_service_get_snapshot(&files) == ESP_OK)
    {
        storage = files.volume == FILE_SERVICE_VOLUME_READY ? "已挂载" :
                  (files.volume == FILE_SERVICE_VOLUME_BUSY ? "使用中" :
                   "未挂载");
    }
    (void)snprintf(text, sizeof(text), "SD %s", storage);
    lv_label_set_text(state->device_summary, text);

    const esp_app_desc_t *description = esp_app_get_description();
    lv_label_set_text(state->about_summary,
                      description != NULL ? description->version : "--");
}

static void _settings_root_mount(const app_manager_page_context_t *context)
{
    settings_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "系统设置", true);
    lv_obj_t *root = state->page.content;
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollable(root, true);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(root, 8, 0);

    /* One card: the hub scrolls once it exceeds one screen, so the row order
     * follows the user journey (display, sound, connection, device). */
    lv_obj_t *group = app_ui_group_create(root);
    (void)app_ui_group_add_entry(group, "显示与电源",
                                 &state->display_summary,
                                 _settings_open_page_event,
                                 (void *)SETTINGS_PAGE_DISPLAY);
    (void)app_ui_group_add_entry(group, "声音",
                                 &state->sound_summary,
                                 _settings_open_page_event,
                                 (void *)SETTINGS_PAGE_SOUND);
    (void)app_ui_group_add_entry(group, "Wi-Fi",
                                 &state->wifi_summary,
                                 _settings_open_page_event,
                                 (void *)SETTINGS_PAGE_WIFI);
    (void)app_ui_group_add_entry(group, "蓝牙",
                                 &state->bluetooth_summary,
                                 _settings_open_page_event,
                                 (void *)SETTINGS_PAGE_BLUETOOTH);
    (void)app_ui_group_add_nav(group, NULL, "连接向导", NULL,
                               _settings_open_setup_event, NULL);
    (void)app_ui_group_add_entry(group, "设备状态",
                                 &state->device_summary,
                                 _settings_open_page_event,
                                 (void *)SETTINGS_PAGE_DEVICE);
    (void)app_ui_group_add_entry(group, "关于与维护",
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
    state->sound_summary = NULL;
    state->wifi_summary = NULL;
    state->bluetooth_summary = NULL;
    state->device_summary = NULL;
    state->about_summary = NULL;
}

static void _settings_info_refresh(settings_info_state_t *state)
{
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
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "时间设置", true);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    app_ui_add_value_row(state->page.content, "时间来源", "读取中",
                         &state->source_value);

    /* The timestamp is 239 px at BODY size and the row's value column is
     * 196 px, so it gets a full-width card instead of wrapping mid-time. */
    app_ui_add_section(state->page.content, "当前时间");
    lv_obj_t *time_card = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(time_card);
    lv_obj_set_width(time_card, LV_PCT(100));
    lv_obj_set_height(time_card, APP_UI_ROW_HEIGHT);
    lv_obj_set_style_bg_color(time_card,
                              lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(time_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(time_card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_pad_left(time_card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_right(time_card, 14, 0);
    lv_obj_set_flex_flow(time_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(time_card, false);

    state->detail_value = lv_label_create(time_card);
    lv_obj_set_width(state->detail_value, LV_PCT(100));
    lv_label_set_long_mode(state->detail_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(state->detail_value,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_obj_set_style_text_color(state->detail_value,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_label_set_text(state->detail_value, "读取中");
    app_ui_make_passive(state->detail_value, false);

    app_ui_add_body_label(state->page.content,
                          "网络校时需要设备连接 Wi-Fi");
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
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);

    const esp_app_desc_t *description = esp_app_get_description();

    lv_obj_t *name_button = lv_button_create(state->page.content);
    app_ui_click_only(name_button);
    lv_obj_set_width(name_button, LV_PCT(100));
    lv_obj_set_height(name_button, 56);
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
    lv_obj_t *hardware_group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_value(hardware_group, "显示", "368 x 448 AMOLED",
                                 NULL);
    (void)app_ui_group_add_value(hardware_group, "平台", "ESP32-S3", NULL);

    app_ui_add_section(state->page.content, "固件");
    lv_obj_t *firmware_group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_value(firmware_group, "项目",
                                 description != NULL ? description->project_name :
                                 "--", NULL);
    (void)app_ui_group_add_value(firmware_group, "版本",
                                 description != NULL ? description->version : "--",
                                 NULL);
    (void)app_ui_group_add_value(firmware_group, "ESP-IDF",
                                 description != NULL ? description->idf_ver : "--",
                                 NULL);
    if (description != NULL)
    {
        char build[40];
        (void)snprintf(build, sizeof(build), "%s %s",
                       description->date, description->time);
        (void)app_ui_group_add_value(firmware_group, "构建", build, NULL);
    }

    app_ui_add_section(state->page.content, "维护");
    lv_obj_t *maintenance_group = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_danger(maintenance_group, LV_SYMBOL_TRASH,
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
        .page_id = SETTINGS_PAGE_WIFI,
        .definition = &settings_wifi_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_BLUETOOTH,
        .definition = &settings_bluetooth_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_DEVICE,
        .definition = &settings_device_page_definition,
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
        .user_data = NULL,
    },
    {
        .page_id = SETTINGS_PAGE_SOUND,
        .definition = &settings_sound_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(settings, APP_IMAGE_SETTINGS_ICON, "系统设置",
                            APP_MANAGER_ID_SETTINGS, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_settings_routes, 50U,
                            "设备与电源");
