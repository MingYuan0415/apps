#define DBG_TAG "settings_display"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

#define SETTINGS_SCREEN_CHIP_COUNT 4U
#define SETTINGS_STANDBY_CHIP_COUNT 3U

typedef struct settings_display_chip
{
    int32_t timeout_ms;
    bool standby;
} settings_display_chip_t;

typedef struct settings_display_state
{
    app_ui_page_t page;
    lv_obj_t *slider;
    lv_obj_t *percent_label;
    lv_obj_t *status_label;
    lv_obj_t *pm_status_label;
    lv_obj_t *screen_chips[SETTINGS_SCREEN_CHIP_COUNT];
    lv_obj_t *standby_chips[SETTINGS_STANDBY_CHIP_COUNT];
    settings_display_chip_t chip_ctx[SETTINGS_SCREEN_CHIP_COUNT +
                                     SETTINGS_STANDBY_CHIP_COUNT];
} settings_display_state_t;

_Static_assert(sizeof(settings_display_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Display page state exceeds the lifecycle arena slot");

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

static void _display_sync(settings_display_state_t *state)
{
    const uint8_t brightness = app_manager_screen_get_brightness();
    lv_slider_set_value(state->slider, brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(state->percent_label, "%u%%",
                          (unsigned)(((unsigned)brightness * 100U + 127U) /
                                     255U));

    const int32_t screen = app_manager_pm_get_timeout_ms();
    const int32_t standby = app_manager_pm_get_standby_delay_ms();
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

static void _display_event(lv_event_t *event)
{
    settings_display_state_t *state = lv_event_get_user_data(event);
    const uint8_t brightness = (uint8_t)lv_slider_get_value(state->slider);
    lv_label_set_text_fmt(state->percent_label, "%u%%",
                          (unsigned)(((unsigned)brightness * 100U + 127U) /
                                     255U));
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
        const esp_err_t result =
            app_manager_screen_set_brightness_temp(brightness);
        app_ui_set_status_text(state->status_label,
                               result == ESP_OK ? "正在预览" : "预览队列繁忙",
                               result == ESP_OK ? APP_UI_STATUS_ACCENT :
                               APP_UI_STATUS_WARNING);
    }
    else if (lv_event_get_code(event) == LV_EVENT_RELEASED)
    {
        const esp_err_t result = app_manager_screen_set_brightness(brightness);
        app_ui_set_status_text(state->status_label,
                               result == ESP_OK ? "已保存" : "保存失败",
                               result == ESP_OK ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_ERROR);
    }
}

static void _display_chip_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    settings_display_state_t *state = lv_event_get_user_data(event);
    const settings_display_chip_t *chip =
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
        app_ui_set_status_text(state->pm_status_label, "保存失败",
                               APP_UI_STATUS_ERROR);
        LOG_W("timeout update failed: %s", esp_err_to_name(result));
        return;
    }
    app_ui_set_status_text(state->pm_status_label, " ",
                           APP_UI_STATUS_NEUTRAL);
    _display_sync(state);
}

static void _display_add_chips(settings_display_state_t *state, lv_obj_t *row,
                               lv_obj_t **chips, const char *const *texts,
                               const int32_t *values, size_t count,
                               size_t ctx_offset, bool standby)
{
    for (size_t index = 0U; index < count; ++index)
    {
        settings_display_chip_t *ctx = &state->chip_ctx[ctx_offset + index];
        ctx->timeout_ms = values[index];
        ctx->standby = standby;
        chips[index] = app_ui_chip_create(row, texts[index],
                                          _display_chip_event, state);
        lv_obj_set_user_data(chips[index], ctx);
    }
}

static void _display_mount(const app_manager_page_context_t *context)
{
    settings_display_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "显示与电源", true);
    app_ui_page_set_subtitle(&state->page, "亮度 · 熄屏 · 待机");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(card, false);

    lv_obj_t *heading = app_ui_button_row_create(card, 24);
    lv_obj_t *label = lv_label_create(heading);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(label, "亮度");
    state->percent_label = lv_label_create(heading);
    lv_obj_set_style_text_color(state->percent_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->percent_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->percent_label, "--%");

    state->slider = lv_slider_create(card);
    lv_obj_set_width(state->slider, LV_PCT(100));
    lv_obj_set_height(state->slider, 32);
    lv_slider_set_range(state->slider, 10, 255);
    lv_obj_set_style_bg_color(state->slider,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_TEXT),
                              LV_PART_KNOB);
    lv_obj_set_style_radius(state->slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(state->slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(state->slider, _display_event, LV_EVENT_VALUE_CHANGED,
                        state);
    lv_obj_add_event_cb(state->slider, _display_event, LV_EVENT_RELEASED,
                        state);

    state->status_label = app_ui_add_body_label(card, "拖动即时预览,松手保存");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

    app_ui_add_section(state->page.content, "自动熄屏");
    lv_obj_t *screen_row = app_ui_chip_row_create(state->page.content);
    _display_add_chips(state, screen_row, state->screen_chips, k_screen_text,
                       k_screen_ms, SETTINGS_SCREEN_CHIP_COUNT, 0U, false);

    app_ui_add_section(state->page.content, "熄屏后待机");
    lv_obj_t *standby_row = app_ui_chip_row_create(state->page.content);
    _display_add_chips(state, standby_row, state->standby_chips,
                       k_standby_text, k_standby_ms,
                       SETTINGS_STANDBY_CHIP_COUNT,
                       SETTINGS_SCREEN_CHIP_COUNT, true);

    state->pm_status_label = app_ui_add_body_label(state->page.content, " ");
    lv_obj_set_width(state->pm_status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->pm_status_label, LV_TEXT_ALIGN_CENTER,
                                0);

    _display_sync(state);
}

static void _display_resume(const app_manager_page_context_t *context)
{
    _display_sync(context->state);
}

static void _display_unmount(const app_manager_page_context_t *context)
{
    settings_display_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->slider = NULL;
    state->percent_label = NULL;
    state->status_label = NULL;
    state->pm_status_label = NULL;
}

static const app_manager_page_ops_t s_settings_display_ops =
{
    .mount = _display_mount,
    .resume = _display_resume,
    .unmount = _display_unmount,
};

const app_manager_page_definition_t settings_display_page_definition =
{
    .ops = &s_settings_display_ops,
    .memory_size = sizeof(settings_display_state_t),
};
