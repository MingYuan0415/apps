#define DBG_TAG "settings_sound"
#include "mt_log.h"

#include "settings_app_internal.h"

#include "audio_service.h"

typedef struct settings_sound_state
{
    app_ui_page_t page;
    lv_obj_t *slider;
    lv_obj_t *percent_label;
    lv_obj_t *status_label;
} settings_sound_state_t;

_Static_assert(sizeof(settings_sound_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Sound page state exceeds the lifecycle arena slot");

static void _sound_percent_text(settings_sound_state_t *state,
                                uint8_t percent)
{
    lv_label_set_text_fmt(state->percent_label, "%u%%", (unsigned)percent);
}

/* The row mirrors the service: a level it cannot read means the board has no
 * audio path, so the control is greyed instead of pretending to work. */
static void _sound_sync(settings_sound_state_t *state)
{
    uint8_t percent = 0U;
    if (audio_service_get_volume(&percent) != ESP_OK)
    {
        lv_obj_add_state(state->slider, LV_STATE_DISABLED);
        lv_label_set_text(state->percent_label, "--");
        app_ui_set_status_text(state->status_label, "音频不可用",
                               APP_UI_STATUS_WARNING);
        return;
    }
    lv_obj_remove_state(state->slider, LV_STATE_DISABLED);
    if (percent > 100U)
    {
        percent = 100U;
    }
    lv_slider_set_value(state->slider, (int32_t)percent, LV_ANIM_OFF);
    _sound_percent_text(state, percent);
    app_ui_set_status_text(state->status_label, "拖动即时生效,松手保存",
                           APP_UI_STATUS_NEUTRAL);
}

static void _sound_event(lv_event_t *event)
{
    settings_sound_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->slider == NULL)
    {
        return;
    }
    const uint8_t percent = (uint8_t)lv_slider_get_value(state->slider);
    _sound_percent_text(state, percent);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
        /* Preview only: set_volume never touches flash, so the drag stays
         * cheap and the codec is the single side effect. */
        const esp_err_t result = audio_service_set_volume(percent);
        app_ui_set_status_text(state->status_label,
                               result == ESP_OK ? "正在应用" : "应用失败",
                               result == ESP_OK ? APP_UI_STATUS_ACCENT :
                               APP_UI_STATUS_WARNING);
    }
    else if (lv_event_get_code(event) == LV_EVENT_RELEASED)
    {
        /* The settled level is applied now; audio_service queues the one-byte
         * NVS commit on a flash-safe worker. */
        const esp_err_t result =
            audio_service_set_volume_persisted(percent);
        app_ui_set_status_text(state->status_label,
                               result == ESP_OK ? "已保存" : "保存失败",
                               result == ESP_OK ? APP_UI_STATUS_SUCCESS :
                               APP_UI_STATUS_ERROR);
    }
}

static void _sound_mount(const app_manager_page_context_t *context)
{
    settings_sound_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "声音", true);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);
    lv_obj_set_style_pad_bottom(state->page.content, 8, 0);

    lv_obj_t *card = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(card, false);

    lv_obj_t *heading = app_ui_button_row_create(card, 28);
    lv_obj_t *label = lv_label_create(heading);
    lv_obj_set_style_text_color(label, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(label, "音量");
    state->percent_label = lv_label_create(heading);
    lv_obj_set_style_text_color(state->percent_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->percent_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->percent_label, "--%");

    state->slider = lv_slider_create(card);
    lv_obj_set_width(state->slider, LV_PCT(100));
    lv_obj_set_height(state->slider, 16);
    lv_slider_set_range(state->slider, 0, 100);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_TRACK),
                              LV_PART_MAIN);
    /* The knob overhangs the 16 px track by 6 px on each side, so inset the
     * track by the same radius and the handle stays inside the card at both
     * ends instead of being clipped by the surface edge. */
    lv_obj_set_style_pad_left(state->slider, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(state->slider, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->slider, lv_color_hex(APP_UI_COLOR_TEXT),
                              LV_PART_KNOB);
    lv_obj_set_style_radius(state->slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(state->slider, 6, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(state->slider, 0, LV_PART_KNOB);
    /* 16 px track + 14 px each side keeps the drag band at 44 px. */
    lv_obj_set_ext_click_area(state->slider, 14);
    lv_obj_add_event_cb(state->slider, _sound_event, LV_EVENT_VALUE_CHANGED,
                        state);
    lv_obj_add_event_cb(state->slider, _sound_event, LV_EVENT_RELEASED,
                        state);

    state->status_label = app_ui_add_body_label(card, " ");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);

    _sound_sync(state);
}

static void _sound_resume(const app_manager_page_context_t *context)
{
    _sound_sync(context->state);
}

static void _sound_unmount(const app_manager_page_context_t *context)
{
    settings_sound_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->slider = NULL;
    state->percent_label = NULL;
    state->status_label = NULL;
}

static const app_manager_page_ops_t s_settings_sound_ops =
{
    .mount = _sound_mount,
    .resume = _sound_resume,
    .unmount = _sound_unmount,
};

const app_manager_page_definition_t settings_sound_page_definition =
{
    .ops = &s_settings_sound_ops,
    .memory_size = sizeof(settings_sound_state_t),
};
