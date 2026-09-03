#define DBG_TAG "settings_display"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "settings_app_internal.h"

typedef struct settings_display_state
{
    app_ui_page_t page;
    lv_obj_t *slider;
    lv_obj_t *percent_label;
    lv_obj_t *status_label;
} settings_display_state_t;

_Static_assert(sizeof(settings_display_state_t) <=
               APP_MANAGER_PAGE_STATE_BYTES,
               "Display page state exceeds the lifecycle arena slot");

static void _display_sync(settings_display_state_t *state)
{
    const uint8_t brightness = app_manager_screen_get_brightness();
    lv_slider_set_value(state->slider, brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(state->percent_label, "%u%%",
                          (unsigned)(((unsigned)brightness * 100U + 127U) /
                                     255U));
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

static void _display_mount(const app_manager_page_context_t *context)
{
    settings_display_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "显示与亮度", true);
    app_ui_page_set_subtitle(&state->page, "屏幕亮度");
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
    lv_obj_set_height(state->slider, 28);
    lv_slider_set_range(state->slider, 10, 255);
    lv_obj_set_style_bg_color(state->slider,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_TEXT),
                              LV_PART_KNOB);
    lv_obj_set_style_pad_all(state->slider, -10, LV_PART_KNOB);
    lv_obj_add_event_cb(state->slider, _display_event, LV_EVENT_VALUE_CHANGED,
                        state);
    lv_obj_add_event_cb(state->slider, _display_event, LV_EVENT_RELEASED,
                        state);

    state->status_label = app_ui_add_body_label(card, "拖动即时预览,松手保存");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

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
