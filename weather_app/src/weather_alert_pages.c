#define DBG_TAG "weather_alerts"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "weather_app_internal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct weather_alerts_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *list;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_alerts_state_t;

typedef struct weather_alert_detail_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *title_label;
    lv_obj_t *type_value;
    lv_obj_t *severity_value;
    lv_obj_t *state_value;
    lv_obj_t *period_value;
    lv_obj_t *description_value;
    lv_obj_t *instruction_section;
    lv_obj_t *instruction_value;
    lv_obj_t *truncated_label;
    const weather_service_snapshot_t *snapshot;
    event_bus_sub_handle_t subscription;
} weather_alert_detail_state_t;

_Static_assert(sizeof(weather_alerts_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather alerts state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_alert_detail_state_t) <= WEATHER_PAGE_SLOT_BYTES,
               "Weather alert detail state exceeds the lifecycle arena slot");

static atomic_uint_fast64_t s_selected_alert_key;

static const weather_service_alert_t *_weather_alert_find_selected(
    const weather_service_snapshot_t *snapshot)
{
    if (snapshot == NULL || !snapshot->alerts.meta.available)
    {
        return NULL;
    }
    uint64_t key = atomic_load_explicit(&s_selected_alert_key,
                                        memory_order_acquire);
    for (uint8_t index = 0U; index < snapshot->alerts.count; ++index)
    {
        if (snapshot->alerts.items[index].key == key)
        {
            return &snapshot->alerts.items[index];
        }
    }
    return NULL;
}

static void _weather_alert_open(lv_event_t *event)
{
    weather_alerts_state_t *state = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->snapshot == NULL)
    {
        return;
    }
    uintptr_t index = (uintptr_t)lv_obj_get_user_data(
                          lv_event_get_target_obj(event));
    if (index == 0U || index > state->snapshot->alerts.count)
    {
        return;
    }
    atomic_store_explicit(&s_selected_alert_key,
                          state->snapshot->alerts.items[index - 1U].key,
                          memory_order_release);
    app_ui_request_open_page(APP_MANAGER_ID_WEATHER, WEATHER_PAGE_DETAIL);
}

static lv_obj_t *_weather_alert_add_action(weather_alerts_state_t *state,
        uint8_t index)
{
    const weather_service_alert_t *alert =
        &state->snapshot->alerts.items[index];
    lv_obj_t *button = lv_button_create(state->list);
    lv_obj_set_size(button, LV_PCT(100), 82);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(WEATHER_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 10, 0);
    lv_obj_set_style_pad_column(button, 10, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(button, (void *)(uintptr_t)(index + 1U));
    lv_obj_add_event_cb(button, _weather_alert_open, LV_EVENT_CLICKED, state);
    lv_obj_t *warning = weather_ui_symbol_label(button, LV_SYMBOL_WARNING);
    lv_obj_set_width(warning, 24);
    lv_obj_set_style_text_color(warning,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_obj_t *content = weather_ui_container(button, LV_SIZE_CONTENT,
                        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_row(content, 3, 0);
    lv_obj_t *title = weather_ui_text_label(
                          content, alert->title, APP_THEME_FONT_SMALL);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(title, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    char start[24];
    char end[24];
    char text[128];
    weather_ui_format_time(&alert->starts_at, "%m-%d %H:%M", start,
                           sizeof(start));
    weather_ui_format_time(&alert->ends_at, "%m-%d %H:%M", end, sizeof(end));
    (void)snprintf(text, sizeof(text), "%s · %s\n%s - %s",
                   alert->severity[0] != '\0' ? alert->severity : "级别未知",
                   alert->status[0] != '\0' ? alert->status : "状态未知",
                   start[0] != '\0' ? start : "未提供",
                   end[0] != '\0' ? end : "未提供");
    lv_obj_t *subtitle = weather_ui_text_label(
                             content, text, APP_THEME_FONT_BODY);
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_obj_set_style_text_color(subtitle, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    lv_obj_t *chevron = weather_ui_symbol_label(button, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    return button;
}

static void _weather_alerts_render(weather_alerts_state_t *state)
{
    lv_obj_clean(state->list);
    if (state->snapshot == NULL || !state->snapshot->alerts.meta.available)
    {
        app_ui_set_status_text(state->status_label, "预警数据不可用",
                               APP_UI_STATUS_WARNING);
        return;
    }
    if (state->snapshot->alerts.count == 0U)
    {
        app_ui_set_status_text(state->status_label, "当前没有生效预警",
                               APP_UI_STATUS_SUCCESS);
        return;
    }
    char text[80];
    const char *freshness = state->snapshot->alerts.meta.expired ?
                            "，数据已过期" :
                            (state->snapshot->alerts.meta.stale ?
                             "，使用缓存数据" : "");
    (void)snprintf(text, sizeof(text), "共 %u 条气象预警%s%s",
                   (unsigned)state->snapshot->alerts.count,
                   freshness,
                   state->snapshot->alerts.truncated ? "，列表已截断" : "");
    app_ui_set_status_text(state->status_label, text, APP_UI_STATUS_WARNING);
    for (uint8_t index = 0U; index < state->snapshot->alerts.count; ++index)
    {
        (void)_weather_alert_add_action(state, index);
    }
}

static void _weather_alerts_refresh(weather_alerts_state_t *state)
{
    weather_ui_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_alerts_render(state);
}

static void _weather_alerts_event(event_bus_msg_id_t msg_id, uint32_t sub_type,
                                  const void *payload, size_t payload_size,
                                  void *user_data)
{
    weather_alerts_state_t *state = user_data;
    if (weather_ui_is_snapshot_event(msg_id, sub_type, payload,
                                     payload_size) &&
            state->page.root != NULL)
    {
        _weather_alerts_refresh(state);
    }
}

static void _weather_alerts_build(weather_alerts_state_t *state)
{
    app_ui_page_create(&state->page, "气象预警", true);
    state->status_label = weather_ui_text_label(
                              state->page.content, "读取预警",
                              APP_THEME_FONT_BODY);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    state->list = weather_ui_container(state->page.content, LV_SIZE_CONTENT,
                                       LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 8, 0);
}

static void _weather_alerts_resume(weather_alerts_state_t *state)
{
    _weather_alerts_refresh(state);
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return;
    }
    esp_err_t result = event_bus_subscribe(
                           WEATHER_SERVICE_MSG,
                           WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                           _weather_alerts_event, state, EVENT_BUS_DISPATCH_UI,
                           &state->subscription);
    if (result != ESP_OK)
    {
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_W("weather subscription failed: %s", esp_err_to_name(result));
    }
}

static esp_err_t _weather_alerts_pause(weather_alerts_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    weather_ui_release_snapshot(&state->snapshot);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
    }
    return result;
}

static void _weather_alerts_handler(app_manager_msg_type_t message,
                                    void *param)
{
    (void)param;
    weather_alerts_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _weather_alerts_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _weather_alerts_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_weather_alerts_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        (void)_weather_alerts_pause(state);
        break;
    default:
        break;
    }
}

static lv_obj_t *_weather_alert_detail_section(lv_obj_t *parent,
        const char *title)
{
    lv_obj_t *label = weather_ui_text_label(parent, title,
                                            APP_THEME_FONT_SMALL);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_WARNING), 0);
    return label;
}

static lv_obj_t *_weather_alert_detail_value(lv_obj_t *parent,
        const char *text)
{
    lv_obj_t *label = weather_ui_text_label(parent, text,
                                            APP_THEME_FONT_BODY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_obj_set_style_text_line_space(label, 5, 0);
    return label;
}

static void _weather_alert_detail_render(weather_alert_detail_state_t *state)
{
    const weather_service_alert_t *alert =
        _weather_alert_find_selected(state->snapshot);
    if (alert == NULL)
    {
        app_ui_set_status_text(state->status_label, "该预警已失效或被撤销",
                               APP_UI_STATUS_WARNING);
        lv_label_set_text(state->title_label, "预警不可用");
        lv_label_set_text(state->type_value, "--");
        lv_label_set_text(state->severity_value, "--");
        lv_label_set_text(state->state_value, "--");
        lv_label_set_text(state->period_value, "--");
        lv_label_set_text(state->description_value, "暂无说明");
        lv_label_set_text(state->instruction_value, "暂无建议");
        lv_obj_add_flag(state->truncated_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    app_ui_set_status_text(
        state->status_label,
        state->snapshot->alerts.meta.expired ? "预警数据已过期" :
        (state->snapshot->alerts.meta.stale ? "预警来自缓存" : "预警详情"),
        APP_UI_STATUS_WARNING);
    lv_label_set_text(state->title_label, alert->title);
    lv_label_set_text(state->type_value,
                      alert->type_name[0] != '\0' ? alert->type_name : "--");
    lv_label_set_text(state->severity_value,
                      alert->severity[0] != '\0' ? alert->severity : "--");
    lv_label_set_text(state->state_value,
                      alert->status[0] != '\0' ? alert->status : "--");
    char start[24];
    char end[24];
    char period[64];
    weather_ui_format_time(&alert->starts_at, "%m-%d %H:%M", start,
                           sizeof(start));
    weather_ui_format_time(&alert->ends_at, "%m-%d %H:%M", end, sizeof(end));
    (void)snprintf(period, sizeof(period), "%s - %s",
                   start[0] != '\0' ? start : "未提供",
                   end[0] != '\0' ? end : "未提供");
    lv_label_set_text(state->period_value, period);
    lv_label_set_text(state->description_value,
                      alert->description[0] != '\0' ?
                      alert->description : "暂无说明");
    lv_label_set_text(state->instruction_value,
                      alert->instruction[0] != '\0' ?
                      alert->instruction : "暂无建议");
    if (alert->content_truncated)
    {
        lv_obj_remove_flag(state->truncated_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(state->truncated_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _weather_alert_detail_refresh(
    weather_alert_detail_state_t *state)
{
    weather_ui_release_snapshot(&state->snapshot);
    (void)weather_service_snapshot_acquire(&state->snapshot);
    _weather_alert_detail_render(state);
}

static void _weather_alert_detail_event(event_bus_msg_id_t msg_id,
                                        uint32_t sub_type,
                                        const void *payload,
                                        size_t payload_size,
                                        void *user_data)
{
    weather_alert_detail_state_t *state = user_data;
    if (weather_ui_is_snapshot_event(msg_id, sub_type, payload,
                                     payload_size) &&
            state->page.root != NULL)
    {
        _weather_alert_detail_refresh(state);
    }
}

static void _weather_alert_detail_build(weather_alert_detail_state_t *state)
{
    app_ui_page_create(&state->page, "预警详情", true);
    state->status_label = weather_ui_text_label(
                              state->page.content, "读取预警详情",
                              APP_THEME_FONT_BODY);
    state->title_label = weather_ui_text_label(
                             state->page.content, "预警不可用",
                             APP_THEME_FONT_SMALL);
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);

    (void)_weather_alert_detail_section(state->page.content, "类型");
    state->type_value = _weather_alert_detail_value(state->page.content, "--");
    (void)_weather_alert_detail_section(state->page.content, "级别");
    state->severity_value = _weather_alert_detail_value(state->page.content,
                            "--");
    (void)_weather_alert_detail_section(state->page.content, "状态");
    state->state_value = _weather_alert_detail_value(state->page.content,
                         "--");
    (void)_weather_alert_detail_section(state->page.content, "有效时段");
    state->period_value = _weather_alert_detail_value(state->page.content,
                          "--");
    (void)_weather_alert_detail_section(state->page.content, "说明");
    state->description_value = _weather_alert_detail_value(
                                   state->page.content, "暂无说明");
    state->instruction_section = _weather_alert_detail_section(
                                     state->page.content, "建议");
    state->instruction_value = _weather_alert_detail_value(
                                   state->page.content, "暂无建议");
    state->truncated_label = weather_ui_text_label(
                                 state->page.content, "内容已截断",
                                 APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(state->truncated_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
}

static void _weather_alert_detail_resume(weather_alert_detail_state_t *state)
{
    _weather_alert_detail_refresh(state);
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return;
    }
    esp_err_t result = event_bus_subscribe(
                           WEATHER_SERVICE_MSG,
                           WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT,
                           _weather_alert_detail_event, state,
                           EVENT_BUS_DISPATCH_UI, &state->subscription);
    if (result != ESP_OK)
    {
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        LOG_W("weather subscription failed: %s", esp_err_to_name(result));
    }
}

static esp_err_t _weather_alert_detail_pause(
    weather_alert_detail_state_t *state)
{
    esp_err_t result = weather_ui_unsubscribe(&state->subscription);
    weather_ui_release_snapshot(&state->snapshot);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
    }
    return result;
}

static void _weather_alert_detail_handler(app_manager_msg_type_t message,
        void *param)
{
    (void)param;
    weather_alert_detail_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _weather_alert_detail_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _weather_alert_detail_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_weather_alert_detail_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        (void)_weather_alert_detail_pause(state);
        break;
    default:
        break;
    }
}

const app_manager_page_definition_t weather_alerts_page_definition =
{
    .handler = _weather_alerts_handler,
    .memory_size = sizeof(weather_alerts_state_t),
};

const app_manager_page_definition_t weather_alert_detail_page_definition =
{
    .handler = _weather_alert_detail_handler,
    .memory_size = sizeof(weather_alert_detail_state_t),
};
