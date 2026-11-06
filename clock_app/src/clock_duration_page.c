#define DBG_TAG "clock_duration"
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>

#define CLOCK_DURATION_HOURS 12U
#define CLOCK_DURATION_MAX_MINUTES 779U

static const uint32_t k_presets[4] = {5U, 10U, 25U, 45U};
static const char *const k_preset_text[4] = {"5 分", "10 分", "25 分",
                                             "45 分"
                                            };

typedef struct clock_duration_state
{
    app_ui_page_t page;
    lv_obj_t *hour_roller;
    lv_obj_t *minute_roller;
    lv_obj_t *summary_label;
    lv_obj_t *hint_label;
    lv_obj_t *presets[4];
} clock_duration_state_t;

_Static_assert(sizeof(clock_duration_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Duration page state exceeds the lifecycle arena slot");

static char s_hour_options[4 * (CLOCK_DURATION_HOURS + 1U)];
static char s_minute_options[3U * 60U];

static void _duration_build_options(void)
{
    size_t offset = 0U;
    for (uint32_t hour = 0U; hour <= CLOCK_DURATION_HOURS; ++hour)
    {
        offset += (size_t)snprintf(s_hour_options + offset,
                                   sizeof(s_hour_options) - offset,
                                   hour == 0U ? "%u" : "\n%u",
                                   (unsigned)hour);
    }
    offset = 0U;
    for (uint32_t minute = 0U; minute < 60U; ++minute)
    {
        offset += (size_t)snprintf(s_minute_options + offset,
                                   sizeof(s_minute_options) - offset,
                                   minute == 0U ? "%u" : "\n%u",
                                   (unsigned)minute);
    }
}

static lv_obj_t *_duration_roller(lv_obj_t *parent, const char *options,
                                  uint32_t selected)
{
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
    lv_obj_set_width(roller, 96);
    lv_obj_set_height(roller, 152);
    lv_obj_set_style_bg_color(roller, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(roller, 0, 0);
    lv_obj_set_style_radius(roller, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_text_line_space(roller, 18, 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(APP_UI_COLOR_TRACK),
                              LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_radius(roller, APP_UI_RADIUS_CONTROL, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_hex(APP_UI_COLOR_TEXT),
                                LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller, app_ui_font(APP_THEME_FONT_HEAD), 0);
    return roller;
}

static uint32_t _duration_total_minutes(const clock_duration_state_t *state)
{
    const uint32_t hours = (uint32_t)lv_roller_get_selected(state->hour_roller);
    const uint32_t minutes = (uint32_t)lv_roller_get_selected(
                                 state->minute_roller);
    return hours * 60U + minutes;
}

static void _duration_render_summary(clock_duration_state_t *state)
{
    const uint32_t total = _duration_total_minutes(state);
    char text[32];
    if (total == 0U)
    {
        (void)snprintf(text, sizeof(text), "至少选择 1 分钟");
        lv_label_set_text(state->hint_label, "旋转滚轮，或选一个常用时长");
    }
    else
    {
        (void)snprintf(text, sizeof(text), "共 %u 分钟", (unsigned)total);
        lv_label_set_text(state->hint_label,
                          total > CLOCK_DURATION_MAX_MINUTES ?
                          "将按 12 小时 59 分截取" : "旋转滚轮，或选一个常用时长");
    }
    lv_label_set_text(state->summary_label, text);
    for (size_t index = 0U; index < 4U; ++index)
    {
        app_ui_chip_set_selected(state->presets[index],
                                 total == k_presets[index]);
    }
}

static void _duration_value_event(lv_event_t *event)
{
    _duration_render_summary(lv_event_get_user_data(event));
}

static void _duration_preset_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_duration_state_t *state = lv_event_get_user_data(event);
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= 4U)
    {
        return;
    }
    const uint32_t minutes = k_presets[index];
    lv_roller_set_selected(state->hour_roller, minutes / 60U, LV_ANIM_OFF);
    lv_roller_set_selected(state->minute_roller, minutes % 60U, LV_ANIM_OFF);
    _duration_render_summary(state);
    clock_ui_pulse(state->summary_label);
}

static void _duration_confirm_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_duration_state_t *state = lv_event_get_user_data(event);
    uint32_t total = _duration_total_minutes(state);
    if (total == 0U)
    {
        total = 1U;
    }
    if (total > CLOCK_DURATION_MAX_MINUTES)
    {
        total = CLOCK_DURATION_MAX_MINUTES;
    }
    /* Confirming a duration always returns to an idle countdown so a finished
     * session cannot keep showing the old duration under a new preset. */
    (void)timer_service_countdown_reset();
    clock_ui_minutes_set(total);
    clock_ui_open_page_with_minutes(CLOCK_PAGE_COUNTDOWN, total);
}

static void _duration_mount(const app_manager_page_context_t *context)
{
    clock_duration_state_t *state = context->state;
    uint32_t minutes = 0U;
    if (!clock_ui_take_minutes_argument(&minutes))
    {
        minutes = clock_ui_minutes_get();
    }
    _duration_build_options();
    app_ui_page_create(&state->page, "自定义时长", true);
    lv_obj_set_style_pad_row(state->page.content, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_style_pad_bottom(state->page.content, APP_UI_PAGE_MARGIN, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    lv_obj_t *stage = clock_ui_card_create(state->page.content);
    lv_obj_set_height(stage, 0);
    lv_obj_set_flex_grow(stage, 1);
    lv_obj_set_style_pad_all(stage, 12, 0);
    lv_obj_set_style_pad_row(stage, 8, 0);

    state->summary_label = clock_ui_label_create(stage, "共 5 分钟",
        APP_THEME_FONT_HEAD, APP_UI_COLOR_TEXT);
    state->hint_label = clock_ui_label_create(stage,
        "旋转滚轮，或选一个常用时长", APP_THEME_FONT_SMALL,
        APP_UI_COLOR_MUTED);

    lv_obj_t *pick_row = lv_obj_create(stage);
    lv_obj_remove_style_all(pick_row);
    lv_obj_set_width(pick_row, LV_PCT(100));
    lv_obj_set_height(pick_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pick_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pick_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pick_row, 8, 0);
    app_ui_make_passive(pick_row, false);

    state->hour_roller = _duration_roller(pick_row, s_hour_options,
                                          minutes / 60U);
    lv_obj_t *hour_unit = lv_label_create(pick_row);
    lv_obj_set_style_text_color(hour_unit, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(hour_unit,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(hour_unit, "时");
    app_ui_make_passive(hour_unit, false);
    state->minute_roller = _duration_roller(pick_row, s_minute_options,
                                            minutes % 60U);
    lv_obj_t *minute_unit = lv_label_create(pick_row);
    lv_obj_set_style_text_color(minute_unit,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(minute_unit,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(minute_unit, "分");
    app_ui_make_passive(minute_unit, false);

    lv_obj_t *preset_row = app_ui_chip_row_create(stage);
    for (size_t index = 0U; index < 4U; ++index)
    {
        state->presets[index] = app_ui_chip_create(preset_row,
            k_preset_text[index], _duration_preset_event, state);
    }

    lv_obj_t *controls = app_ui_button_row_create(state->page.content,
        APP_UI_BUTTON_HEIGHT);
    app_ui_button_set_kind(app_ui_button_create(controls, "确定",
            _duration_confirm_event, state),
                           APP_UI_BUTTON_PRIMARY);

    lv_obj_add_event_cb(state->hour_roller, _duration_value_event,
                        LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(state->minute_roller, _duration_value_event,
                        LV_EVENT_VALUE_CHANGED, state);
    _duration_render_summary(state);
}

static void _duration_new_intent(const app_manager_page_context_t *context)
{
    clock_duration_state_t *state = context->state;
    uint32_t minutes = 0U;
    if (clock_ui_take_minutes_argument(&minutes))
    {
        clock_ui_minutes_set(minutes);
    }
    minutes = clock_ui_minutes_get();
    if (state->page.root == NULL)
    {
        /* Background instance: the value is stored; MOUNT renders it. */
        return;
    }
    lv_roller_set_selected(state->hour_roller, minutes / 60U, LV_ANIM_OFF);
    lv_roller_set_selected(state->minute_roller, minutes % 60U, LV_ANIM_OFF);
    _duration_render_summary(state);
}

static void _duration_unmount(const app_manager_page_context_t *context)
{
    clock_duration_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    memset(state->presets, 0, sizeof(state->presets));
    state->hour_roller = NULL;
    state->minute_roller = NULL;
    state->summary_label = NULL;
    state->hint_label = NULL;
}

static const app_manager_page_ops_t s_clock_duration_ops =
{
    .mount = _duration_mount,
    .new_intent = _duration_new_intent,
    .unmount = _duration_unmount,
};

const app_manager_page_definition_t clock_duration_page_definition =
{
    .ops = &s_clock_duration_ops,
    .memory_size = sizeof(clock_duration_state_t),
};
