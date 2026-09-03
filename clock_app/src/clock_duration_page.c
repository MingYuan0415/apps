#define DBG_TAG "clock_duration"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "clock_app_internal.h"

#define CLOCK_DURATION_HOURS 12U
#define CLOCK_DURATION_MAX_MINUTES 779U

typedef struct clock_duration_state
{
    app_ui_page_t page;
    lv_obj_t *hour_roller;
    lv_obj_t *minute_roller;
    lv_obj_t *summary_label;
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
    lv_obj_set_width(roller, 120);
    lv_obj_set_style_bg_color(roller, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(roller, 8, 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
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
    char text[32];
    (void)snprintf(text, sizeof(text), "共 %u 分钟",
                   (unsigned)_duration_total_minutes(state));
    lv_label_set_text(state->summary_label, text);
}

static void _duration_value_event(lv_event_t *event)
{
    _duration_render_summary(lv_event_get_user_data(event));
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

    lv_obj_t *pick_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(pick_row);
    lv_obj_set_width(pick_row, LV_PCT(100));
    lv_obj_set_height(pick_row, 108);
    lv_obj_set_flex_flow(pick_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pick_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pick_row, 8, 0);
    app_ui_make_passive(pick_row, false);

    state->hour_roller = _duration_roller(pick_row, s_hour_options,
                                          minutes / 60U);
    lv_obj_t *hour_unit = lv_label_create(pick_row);
    lv_obj_set_style_text_color(hour_unit, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(hour_unit,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(hour_unit, "时");
    state->minute_roller = _duration_roller(pick_row, s_minute_options,
                                            minutes % 60U);
    lv_obj_t *minute_unit = lv_label_create(pick_row);
    lv_obj_set_style_text_color(minute_unit,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(minute_unit,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(minute_unit, "分");

    state->summary_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->summary_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->summary_label, LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_style_text_color(state->summary_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->summary_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    lv_obj_t *controls = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(controls);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 52);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(controls, false);
    (void)app_ui_button_create(controls, "确定", _duration_confirm_event,
                               state);

    lv_obj_add_event_cb(state->hour_roller, _duration_value_event,
                        LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(state->minute_roller, _duration_value_event,
                        LV_EVENT_VALUE_CHANGED, state);
    _duration_render_summary(state);
}

static void _duration_new_intent(const app_manager_page_context_t *context)
{
    (void)context;
    uint32_t minutes = 0U;
    if (clock_ui_take_minutes_argument(&minutes))
    {
        clock_ui_minutes_set(minutes);
    }
}

static void _duration_unmount(const app_manager_page_context_t *context)
{
    clock_duration_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->hour_roller = NULL;
    state->minute_roller = NULL;
    state->summary_label = NULL;
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
