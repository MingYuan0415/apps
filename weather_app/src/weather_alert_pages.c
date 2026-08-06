#define DBG_TAG "weather_alerts"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "weather_app_internal.h"

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
    uint64_t selected_alert_key;
} weather_alert_detail_state_t;

_Static_assert(sizeof(weather_alerts_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Weather alerts state exceeds the lifecycle arena slot");
_Static_assert(sizeof(weather_alert_detail_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Weather alert detail state exceeds the lifecycle arena slot");

static const weather_service_alert_t *_weather_alert_find_selected(
    const weather_service_snapshot_t *snapshot, uint64_t selected_alert_key)
{
    if (snapshot == NULL || !snapshot->alerts.meta.available)
    {
        return NULL;
    }
    for (uint8_t index = 0U; index < snapshot->alerts.count; ++index)
    {
        if (snapshot->alerts.items[index].key == selected_alert_key)
        {
            return &snapshot->alerts.items[index];
        }
    }
    return NULL;
}

static void _weather_alert_navigation_complete(esp_err_t result,
        void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        LOG_W("open alert detail failed: %s", esp_err_to_name(result));
    }
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
    const weather_alert_arguments_t arguments =
    {
        .alert_key = state->snapshot->alerts.items[index - 1U].key,
    };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_WEATHER,
        .page_id = WEATHER_PAGE_DETAIL,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = WEATHER_ARGUMENT_ALERT_KEY,
            .size = sizeof(arguments),
        },
        .transition =
        {
            .effect = APP_MANAGER_TRANSITION_DEFAULT,
        },
    };
    memcpy(request.arguments.payload, &arguments, sizeof(arguments));
    esp_err_t result = app_manager_navigate_async(
                           &request, _weather_alert_navigation_complete, NULL);
    if (result != ESP_OK)
    {
        LOG_W("failed to queue alert detail: %s", esp_err_to_name(result));
    }
}

static lv_obj_t *_weather_alert_add_action(weather_alerts_state_t *state,
        uint8_t index)
{
    const weather_service_alert_t *alert =
        &state->snapshot->alerts.items[index];
    lv_obj_t *button = lv_button_create(state->list);
    lv_obj_set_size(button, LV_PCT(100), LV_SIZE_CONTENT);
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
    lv_obj_t *warning = weather_ui_symbol_label(button);
    lv_obj_set_width(warning, 24);
    lv_obj_set_style_text_color(warning,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(warning, LV_SYMBOL_WARNING);
    lv_obj_t *content = weather_ui_container(button, LV_SIZE_CONTENT,
                        LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_row(content, 3, 0);
    lv_obj_t *title = weather_ui_text_label(content, APP_THEME_FONT_SMALL);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_label_set_text(title, alert->title);
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
    lv_obj_t *subtitle = weather_ui_text_label(content, APP_THEME_FONT_BODY);
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(WEATHER_COLOR_MUTED),
                                0);
    lv_label_set_text(subtitle, text);
    lv_obj_t *chevron = weather_ui_symbol_label(button);
    lv_obj_set_style_text_color(chevron, lv_color_hex(WEATHER_COLOR_MUTED), 0);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    return button;
}

static void _weather_alerts_render(weather_alerts_state_t *state)
{
    lv_obj_clean(state->list);
    if (state->snapshot == NULL || !state->snapshot->alerts.meta.available)
    {
        weather_service_status_snapshot_t status = {0};
        if (weather_service_get_status(&status) == ESP_OK &&
                status.state != WEATHER_SERVICE_STATE_READY)
        {
            app_ui_set_status_text(state->status_label,
                                   weather_ui_state_text(status.state),
                                   weather_ui_state_color(status.state));
        }
        else
        {
            app_ui_set_status_text(state->status_label, "预警数据不可用",
                                   APP_UI_STATUS_WARNING);
        }
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
    state->status_label = weather_ui_text_label(state->page.content,
                          APP_THEME_FONT_BODY);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_label_set_long_mode(state->status_label, LV_LABEL_LONG_WRAP);
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
    return result;
}

static void _weather_alerts_start(
    const app_manager_page_context_t *context)
{
    weather_alerts_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static void _weather_alerts_mount(
    const app_manager_page_context_t *context)
{
    _weather_alerts_build(context->state);
}

static void _weather_alerts_resume_op(
    const app_manager_page_context_t *context)
{
    _weather_alerts_resume(context->state);
}

static esp_err_t _weather_alerts_pause_op(
    const app_manager_page_context_t *context)
{
    return _weather_alerts_pause(context->state);
}

static void _weather_alerts_unmount(
    const app_manager_page_context_t *context)
{
    weather_alerts_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->list = NULL;
}

static const app_manager_page_ops_t s_weather_alerts_ops =
{
    .start = _weather_alerts_start,
    .mount = _weather_alerts_mount,
    .resume = _weather_alerts_resume_op,
    .pause = _weather_alerts_pause_op,
    .unmount = _weather_alerts_unmount,
};

static lv_obj_t *_weather_alert_detail_section(lv_obj_t *parent,
        const char *title)
{
    lv_obj_t *label = weather_ui_text_label(parent, APP_THEME_FONT_SMALL);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(label, title);
    return label;
}

static lv_obj_t *_weather_alert_detail_value(lv_obj_t *parent,
        const char *text)
{
    lv_obj_t *label = weather_ui_text_label(parent, APP_THEME_FONT_BODY);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_hex(WEATHER_COLOR_TEXT), 0);
    lv_obj_set_style_text_line_space(label, 5, 0);
    lv_label_set_text(label, text);
    return label;
}

static void _weather_alert_detail_render(weather_alert_detail_state_t *state)
{
    const weather_service_alert_t *alert =
        _weather_alert_find_selected(state->snapshot,
                                     state->selected_alert_key);
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
    state->status_label = weather_ui_text_label(state->page.content,
                          APP_THEME_FONT_BODY);
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_label_set_long_mode(state->status_label, LV_LABEL_LONG_WRAP);
    state->title_label = weather_ui_text_label(state->page.content,
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
    state->truncated_label = weather_ui_text_label(state->page.content,
                             APP_THEME_FONT_BODY);
    lv_obj_set_style_text_color(state->truncated_label,
                                lv_color_hex(WEATHER_COLOR_WARNING), 0);
    lv_label_set_text(state->truncated_label, "内容已截断");
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
    return result;
}

static void _weather_alert_detail_apply_arguments(
    weather_alert_detail_state_t *state,
    const app_manager_typed_blob_t *arguments)
{
    weather_alert_arguments_t decoded = {0};
    if (arguments != NULL &&
            arguments->type == WEATHER_ARGUMENT_ALERT_KEY &&
            arguments->size == sizeof(decoded))
    {
        memcpy(&decoded, arguments->payload, sizeof(decoded));
    }
    state->selected_alert_key = decoded.alert_key;
}

static void _weather_alert_detail_start(
    const app_manager_page_context_t *context)
{
    weather_alert_detail_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    _weather_alert_detail_apply_arguments(state, context->arguments);
}

static void _weather_alert_detail_mount(
    const app_manager_page_context_t *context)
{
    _weather_alert_detail_build(context->state);
}

static void _weather_alert_detail_resume_op(
    const app_manager_page_context_t *context)
{
    _weather_alert_detail_resume(context->state);
}

static esp_err_t _weather_alert_detail_pause_op(
    const app_manager_page_context_t *context)
{
    return _weather_alert_detail_pause(context->state);
}

static void _weather_alert_detail_unmount(
    const app_manager_page_context_t *context)
{
    weather_alert_detail_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->title_label = NULL;
    state->type_value = NULL;
    state->severity_value = NULL;
    state->state_value = NULL;
    state->period_value = NULL;
    state->description_value = NULL;
    state->instruction_section = NULL;
    state->instruction_value = NULL;
    state->truncated_label = NULL;
}

static void _weather_alert_detail_new_intent(
    const app_manager_page_context_t *context)
{
    weather_alert_detail_state_t *state = context->state;
    _weather_alert_detail_apply_arguments(state, context->arguments);
    if (state->page.root != NULL)
    {
        _weather_alert_detail_render(state);
    }
}

static const app_manager_page_ops_t s_weather_alert_detail_ops =
{
    .start = _weather_alert_detail_start,
    .mount = _weather_alert_detail_mount,
    .resume = _weather_alert_detail_resume_op,
    .pause = _weather_alert_detail_pause_op,
    .unmount = _weather_alert_detail_unmount,
    .new_intent = _weather_alert_detail_new_intent,
};

const app_manager_page_definition_t weather_alerts_page_definition =
{
    .ops = &s_weather_alerts_ops,
    .memory_size = sizeof(weather_alerts_state_t),
};

const app_manager_page_definition_t weather_alert_detail_page_definition =
{
    .ops = &s_weather_alert_detail_ops,
    .memory_size = sizeof(weather_alert_detail_state_t),
};
