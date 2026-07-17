#define DBG_TAG "settings_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "event_bus.h"
#include "power_service.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_PAGE_POWER "power"
#define SETTINGS_PAGE_ABOUT "about"

typedef struct settings_root_state
{
    app_ui_page_t page;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value;
    lv_obj_t *save_status;
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
} settings_about_state_t;

static void _settings_power_handler(app_manager_msg_type_t message, void *param);
static void _settings_about_handler(app_manager_msg_type_t message, void *param);

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
    _settings_render_brightness(state, brightness);

    esp_err_t result;
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
        result = app_manager_screen_set_brightness_temp(brightness);
        lv_label_set_text(state->save_status,
                          result == ESP_OK ? "Preview queued" : "Preview queue busy");
    }
    else if (lv_event_get_code(event) == LV_EVENT_RELEASED)
    {
        result = app_manager_screen_set_brightness(brightness);
        if (result == ESP_OK)
        {
            lv_label_set_text(state->save_status, "Save queued");
        }
        else
        {
            lv_label_set_text(state->save_status, "Save queue busy");
        }
    }
}

static void _settings_open_power_on_worker(void *arg)
{
    (void)arg;
    esp_err_t result = app_manager_create_page_ext(
                           SETTINGS_PAGE_POWER, _settings_power_handler, NULL,
                           sizeof(settings_power_state_t));
    if (result != ESP_OK)
    {
        LOG_W("open power page failed: %s", esp_err_to_name(result));
    }
}

static void _settings_open_about_on_worker(void *arg)
{
    (void)arg;
    esp_err_t result = app_manager_create_page_ext(
                           SETTINGS_PAGE_ABOUT, _settings_about_handler, NULL,
                           sizeof(settings_about_state_t));
    if (result != ESP_OK)
    {
        LOG_W("open about page failed: %s", esp_err_to_name(result));
    }
}

static void _settings_open_power_event(lv_event_t *event)
{
    (void)event;
    esp_err_t result = app_manager_ui_post(_settings_open_power_on_worker, NULL);
    if (result != ESP_OK)
    {
        LOG_W("page navigation queue failed: %s", esp_err_to_name(result));
    }
}

static void _settings_open_about_event(lv_event_t *event)
{
    (void)event;
    esp_err_t result = app_manager_ui_post(_settings_open_about_on_worker, NULL);
    if (result != ESP_OK)
    {
        LOG_W("page navigation queue failed: %s", esp_err_to_name(result));
    }
}

static void _settings_root_build(settings_root_state_t *state)
{
    app_ui_page_create(&state->page, "Settings", true);
    app_ui_add_section(state->page.content, "DISPLAY");

    lv_obj_t *brightness = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(brightness);
    lv_obj_set_width(brightness, LV_PCT(100));
    lv_obj_set_height(brightness, 116);
    lv_obj_set_style_bg_color(brightness, lv_color_hex(0x1A2024), 0);
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
    lv_label_set_text(label, "Brightness");
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F5F6), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);

    state->brightness_value = lv_label_create(heading);
    lv_obj_set_style_text_color(state->brightness_value,
                                lv_color_hex(0x39C6C8), 0);
    lv_obj_set_style_text_font(state->brightness_value,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    state->brightness_slider = lv_slider_create(brightness);
    lv_obj_set_width(state->brightness_slider, LV_PCT(100));
    lv_obj_set_height(state->brightness_slider, 20);
    lv_slider_set_range(state->brightness_slider, 10, 255);
    uint8_t brightness_value = app_manager_screen_get_brightness();
    lv_slider_set_value(state->brightness_slider, brightness_value,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(0x30393E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(0x39C6C8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->brightness_slider,
                              lv_color_hex(0xF2F5F6), LV_PART_KNOB);
    lv_obj_add_event_cb(state->brightness_slider, _settings_brightness_event,
                        LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(state->brightness_slider, _settings_brightness_event,
                        LV_EVENT_RELEASED, state);

    state->save_status = lv_label_create(brightness);
    lv_label_set_text(state->save_status, "Stored brightness");
    lv_obj_set_style_text_color(state->save_status, lv_color_hex(0x91A0A8), 0);
    lv_obj_set_style_text_font(state->save_status,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    _settings_render_brightness(state, brightness_value);

    app_ui_add_section(state->page.content, "SYSTEM");
    app_ui_add_action(state->page.content, LV_SYMBOL_POWER, "Power",
                      "Battery and sleep status", _settings_open_power_event,
                      NULL);
    app_ui_add_action(state->page.content, LV_SYMBOL_EYE_OPEN, "About",
                      "Hardware and firmware", _settings_open_about_event,
                      NULL);
}

static void _settings_root_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    settings_root_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONRESUME:
        if (state->page.root == NULL)
        {
            _settings_root_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        app_ui_page_destroy(&state->page);
        state->brightness_slider = NULL;
        state->brightness_value = NULL;
        state->save_status = NULL;
        break;
    case APP_MANAGER_MSG_ONSTOP:
        app_ui_page_destroy(&state->page);
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
        lv_label_set_text(state->battery_value, "Unavailable");
        lv_label_set_text(state->voltage_value, "--");
        lv_label_set_text(state->source_value, "PMU offline");
        lv_label_set_text(state->sample_value, "No valid sample");
        goto exit;
    }

    if (snapshot->info.battery_percent >= 0)
    {
        lv_label_set_text_fmt(state->battery_value, "%d%%",
                              snapshot->info.battery_percent);
    }
    else
    {
        lv_label_set_text(state->battery_value, "Unknown");
    }
    lv_label_set_text_fmt(state->voltage_value, "%u mV",
                          (unsigned)snapshot->info.battery_voltage_mv);
    lv_label_set_text(state->source_value,
                      snapshot->info.is_charging ? "Charging" :
                      (snapshot->info.is_vbus_connected ? "USB power" :
                       "Battery"));
    lv_label_set_text_fmt(state->sample_value, "%lld ms",
                          (long long)snapshot->sampled_at_ms);

exit:
    return;
}

static void _settings_power_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                  const void *payload, size_t payload_size,
                                  void *user_data)
{
    settings_power_state_t *state = user_data;
    if (msg_id != POWER_SERVICE_MSG ||
            sub_type != POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE ||
            payload_size != sizeof(power_service_snapshot_t) ||
            state->page.root == NULL)
    {
        goto exit;
    }
    power_service_snapshot_t snapshot;
    memcpy(&snapshot, payload, sizeof(snapshot));
    _settings_power_render(state, &snapshot);

exit:
    return;
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
    app_ui_page_create(&state->page, "Power", true);
    app_ui_add_section(state->page.content, "BATTERY CACHE");
    app_ui_add_value_row(state->page.content, "Charge", "Reading...",
                         &state->battery_value);
    app_ui_add_value_row(state->page.content, "Voltage", "--",
                         &state->voltage_value);
    app_ui_add_value_row(state->page.content, "Source", "--",
                         &state->source_value);
    app_ui_add_value_row(state->page.content, "Sample time", "--",
                         &state->sample_value);

    app_ui_add_section(state->page.content, "POWER MANAGEMENT");
    lv_obj_t *pm_value;
    app_ui_add_value_row(state->page.content, "Screen off", "--", &pm_value);
    int32_t timeout = app_manager_pm_get_timeout_ms();
    if (timeout < 0)
    {
        lv_label_set_text(pm_value, "Disabled");
    }
    else
    {
        lv_label_set_text_fmt(pm_value, "%ld s", (long)(timeout / 1000));
    }
    app_ui_add_action(state->page.content, LV_SYMBOL_POWER, "Turn screen off",
                      "Touch or HOME wakes the display",
                      _settings_screen_off_event, NULL);

    esp_err_t result = event_bus_subscribe(
                           POWER_SERVICE_MSG,
                           POWER_SERVICE_MSG_SUB_TYPE_SNAPSHOT_UPDATE,
                           _settings_power_event, state, EVENT_BUS_DISPATCH_UI,
                           &state->power_subscription);
    if (result != ESP_OK)
    {
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_W("power subscription failed: %s", esp_err_to_name(result));
    }

    power_service_snapshot_t snapshot;
    if (power_service_get_snapshot(&snapshot) == ESP_OK)
    {
        _settings_power_render(state, &snapshot);
    }
}

static void _settings_power_teardown(settings_power_state_t *state)
{
    if (state->power_subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        esp_err_t result = event_bus_unsubscribe(state->power_subscription);
        if (result != ESP_OK && result != ESP_ERR_NOT_FOUND)
        {
            LOG_W("power unsubscribe failed: %s", esp_err_to_name(result));
        }
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
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
        state->power_subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONRESUME:
        if (state->page.root == NULL)
        {
            _settings_power_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONPAUSE:
    case APP_MANAGER_MSG_ONSTOP:
        _settings_power_teardown(state);
        break;
    default:
        break;
    }
}

static void _settings_about_build(settings_about_state_t *state)
{
    app_ui_page_create(&state->page, "About", true);

    lv_obj_t *name = lv_label_create(state->page.content);
    lv_label_set_text(name, "MicroTech");
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_text_color(name, lv_color_hex(0xF2F5F6), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BIGL), 0);

    app_ui_add_body_label(state->page.content,
                          "Compact ESP32-S3 wearable platform");
    app_ui_add_section(state->page.content, "DEVICE");
    app_ui_add_value_row(state->page.content, "Display", "368 x 448", NULL);
    app_ui_add_value_row(state->page.content, "Panel", "AMOLED", NULL);
    app_ui_add_value_row(state->page.content, "Platform", "ESP32-S3", NULL);
    app_ui_add_section(state->page.content, "FIRMWARE");
    app_ui_add_value_row(state->page.content, "Channel", "stabilization/v1",
                         NULL);
}

static void _settings_about_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    settings_about_state_t *state = app_manager_this_page_memory();
    if (message == APP_MANAGER_MSG_ONRESUME && state->page.root == NULL)
    {
        _settings_about_build(state);
    }
    else if (message == APP_MANAGER_MSG_ONPAUSE ||
             message == APP_MANAGER_MSG_ONSTOP)
    {
        app_ui_page_destroy(&state->page);
    }
}

static esp_err_t _settings_entry(void)
{
    return app_manager_regist_msg_handler_ext("root", _settings_root_handler,
            NULL, sizeof(settings_root_state_t));
}

BUILTIN_APP_EXPORT(settings, NULL, APP_MANAGER_ID_SETTINGS, _settings_entry);
