#define DBG_TAG "settings_policy"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

#define SETTINGS_SCREEN_CHIP_COUNT 4U
#define SETTINGS_STANDBY_CHIP_COUNT 3U

typedef struct settings_policy_chip
{
    struct settings_policy_state *state;
    int32_t timeout_ms;
    bool standby;
} settings_policy_chip_t;

typedef struct settings_policy_state
{
    app_ui_page_t page;
    lv_obj_t *screen_value;
    lv_obj_t *standby_value;
    lv_obj_t *screen_chips[SETTINGS_SCREEN_CHIP_COUNT];
    lv_obj_t *standby_chips[SETTINGS_STANDBY_CHIP_COUNT];
    settings_policy_chip_t chip_ctx[SETTINGS_SCREEN_CHIP_COUNT +
                                    SETTINGS_STANDBY_CHIP_COUNT];
} settings_policy_state_t;

_Static_assert(sizeof(settings_policy_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Policy page state exceeds the lifecycle arena slot");

static const int32_t k_screen_ms[SETTINGS_SCREEN_CHIP_COUNT] = {30000, 60000,
                                                                300000, -1
                                                               };
static const char *const k_screen_text[SETTINGS_SCREEN_CHIP_COUNT] =
{"30 秒", "1 分钟", "5 分钟", "从不"};
static const int32_t k_standby_ms[SETTINGS_STANDBY_CHIP_COUNT] = {5000, 30000,
                                                                  -1
                                                                 };
static const char *const k_standby_text[SETTINGS_STANDBY_CHIP_COUNT] =
{"5 秒", "30 秒", "从不"};

static void _policy_sync(settings_policy_state_t *state)
{
    const int32_t screen = app_manager_pm_get_timeout_ms();
    const int32_t standby = app_manager_pm_get_standby_delay_ms();

    app_ui_set_status_text(state->screen_value,
                           settings_ui_screen_timeout_text(screen),
                           APP_UI_STATUS_NEUTRAL);
    app_ui_set_status_text(state->standby_value,
                           settings_ui_standby_timeout_text(standby),
                           APP_UI_STATUS_NEUTRAL);
    for (size_t index = 0U; index < SETTINGS_SCREEN_CHIP_COUNT; ++index)
    {
        app_ui_chip_set_selected(state->screen_chips[index],
                                 screen == k_screen_ms[index]);
    }
    for (size_t index = 0U; index < SETTINGS_STANDBY_CHIP_COUNT; ++index)
    {
        app_ui_chip_set_selected(state->standby_chips[index],
                                 standby == k_standby_ms[index]);
    }
}

static void _policy_chip_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_policy_state_t *state = lv_event_get_user_data(event);
    const settings_policy_chip_t *chip =
        lv_obj_get_user_data(lv_event_get_target(event));
    if (state == NULL || chip == NULL)
    {
        return;
    }
    const esp_err_t result = chip->standby ?
                             app_manager_pm_set_standby_delay_ms(
                                 chip->timeout_ms) :
                             app_manager_pm_set_timeout_ms(chip->timeout_ms);
    if (result != ESP_OK)
    {
        app_ui_set_status_text(chip->standby ? state->standby_value :
                               state->screen_value, "保存失败",
                               APP_UI_STATUS_ERROR);
        LOG_W("timeout update failed: %s", esp_err_to_name(result));
        return;
    }
    _policy_sync(state);
}

static void _policy_add_chips(settings_policy_state_t *state,
                              lv_obj_t *row, lv_obj_t **chips,
                              const char *const *texts, const int32_t *values,
                              size_t count, bool standby)
{
    for (size_t index = 0U; index < count; ++index)
    {
        settings_policy_chip_t *ctx = &state->chip_ctx[standby ?
                                      SETTINGS_SCREEN_CHIP_COUNT + index : index];
        ctx->state = state;
        ctx->timeout_ms = values[index];
        ctx->standby = standby;
        chips[index] = app_ui_chip_create(row, texts[index],
                                          _policy_chip_event, state);
        lv_obj_set_user_data(chips[index], ctx);
    }
}

static void _policy_mount(const app_manager_page_context_t *context)
{
    settings_policy_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "电源策略", true);
    app_ui_page_set_subtitle(&state->page, "熄屏与待机");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_add_section(state->page.content, "自动熄屏");
    app_ui_add_value_row(state->page.content, "无操作熄屏", "--",
                         &state->screen_value);
    lv_obj_t *screen_row = app_ui_chip_row_create(state->page.content);
    _policy_add_chips(state, screen_row, state->screen_chips, k_screen_text,
                      k_screen_ms, SETTINGS_SCREEN_CHIP_COUNT, false);

    app_ui_add_section(state->page.content, "熄屏后待机");
    app_ui_add_value_row(state->page.content, "进入待机", "--",
                         &state->standby_value);
    lv_obj_t *standby_row = app_ui_chip_row_create(state->page.content);
    _policy_add_chips(state, standby_row, state->standby_chips,
                      k_standby_text, k_standby_ms,
                      SETTINGS_STANDBY_CHIP_COUNT, true);

    _policy_sync(state);
}

static void _policy_resume(const app_manager_page_context_t *context)
{
    _policy_sync(context->state);
}

static void _policy_unmount(const app_manager_page_context_t *context)
{
    settings_policy_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->screen_value = NULL;
    state->standby_value = NULL;
}

static const app_manager_page_ops_t s_settings_policy_ops =
{
    .mount = _policy_mount,
    .resume = _policy_resume,
    .unmount = _policy_unmount,
};

const app_manager_page_definition_t settings_policy_page_definition =
{
    .ops = &s_settings_policy_ops,
    .memory_size = sizeof(settings_policy_state_t),
};
