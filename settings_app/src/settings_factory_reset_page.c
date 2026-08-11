#define DBG_TAG "settings_factory_reset"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_factory_reset_page.h"

#include "app_ui.h"
#include "factory_reset_service.h"

#include <stdbool.h>

typedef struct settings_factory_reset_state
{
    app_ui_page_t page;
    lv_obj_t *confirm_button;
    lv_obj_t *status_label;
    bool accepted;
} settings_factory_reset_state_t;

_Static_assert(sizeof(settings_factory_reset_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Settings factory-reset state exceeds the lifecycle arena slot");

static void _settings_factory_reset_confirm(lv_event_t *event)
{
    settings_factory_reset_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->accepted || state->confirm_button == NULL)
    {
        return;
    }

    state->accepted = true;
    lv_obj_add_state(state->confirm_button, LV_STATE_DISABLED);
    app_ui_set_status_text(state->status_label, "正在保存恢复请求",
                           APP_UI_STATUS_ACCENT);

    esp_err_t result = factory_reset_service_request();
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               "恢复请求已受理，正在重启",
                               APP_UI_STATUS_SUCCESS);
        return;
    }

    state->accepted = false;
    lv_obj_remove_state(state->confirm_button, LV_STATE_DISABLED);
    app_ui_set_status_text(state->status_label,
                           "无法保存恢复请求，请重试",
                           APP_UI_STATUS_ERROR);
    LOG_E("factory-reset request failed: %s", esp_err_to_name(result));
}

static void _settings_factory_reset_mount(
    const app_manager_page_context_t *context)
{
    settings_factory_reset_state_t *state = context->state;
    app_ui_page_create(&state->page, "恢复出厂设置", true);

    app_ui_add_section(state->page.content, "将清除的内容");
    app_ui_add_body_label(
        state->page.content,
        "设备绑定授权、蓝牙配对与 CCCD 状态、Wi-Fi 配置和本地传输状态将全部清除，随后设备会重新启动。此操作无法撤销。");

    app_ui_add_section(state->page.content, "确认操作");
    state->confirm_button = app_ui_add_command(
                                state->page.content, LV_SYMBOL_TRASH,
                                "确认恢复出厂设置", "清除本机数据并重新启动",
                                _settings_factory_reset_confirm, state);
    state->status_label = app_ui_add_body_label(state->page.content,
                          "等待确认");
}

static void _settings_factory_reset_unmount(
    const app_manager_page_context_t *context)
{
    settings_factory_reset_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->confirm_button = NULL;
    state->status_label = NULL;
    state->accepted = false;
}

static const app_manager_page_ops_t s_settings_factory_reset_ops =
{
    .mount = _settings_factory_reset_mount,
    .unmount = _settings_factory_reset_unmount,
};

const app_manager_page_definition_t settings_factory_reset_page_definition =
{
    .ops = &s_settings_factory_reset_ops,
    .memory_size = sizeof(settings_factory_reset_state_t),
};
