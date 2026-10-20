#define DBG_TAG "calendar_app"
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "calendar_math.h"
#include "time_service.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Calendar keeps every colour on the shared Emissive palette: the canvas and
 * surfaces come from the page skeleton, accents from app_ui_theme. */
#define CALENDAR_COLOR_TEXT      APP_UI_COLOR_TEXT
#define CALENDAR_COLOR_MUTED     APP_UI_COLOR_MUTED
#define CALENDAR_COLOR_SURFACE   APP_UI_COLOR_SURFACE
#define CALENDAR_COLOR_SURFACE_HI APP_UI_COLOR_SURFACE_HI
#define CALENDAR_COLOR_ACCENT    APP_UI_COLOR_RAIN
#define CALENDAR_COLOR_WARNING   APP_UI_COLOR_WARNING

#define CALENDAR_NAV_HEIGHT        44
#define CALENDAR_NAV_BUTTON_WIDTH  44
#define CALENDAR_TODAY_WIDTH       58
#define CALENDAR_WEEKDAY_HEIGHT    22
#define CALENDAR_CELL_HEIGHT       34
#define CALENDAR_GRID_ROW_GAP      4
#define CALENDAR_GRID_COLUMN_GAP   4
#define CALENDAR_REFRESH_MS        1000U
#define CALENDAR_DETAIL_BYTES      96

/* Selection is expressed as accent tint plus outline, never a solid block. */
#define CALENDAR_TODAY_OPA         LV_OPA_20
#define CALENDAR_RING_OPA          LV_OPA_40

static lv_style_t s_nav;
static lv_style_t s_nav_button;
static lv_style_t s_weekday_row;
static lv_style_t s_month_label;
static lv_style_t s_weekday_label;
static lv_style_t s_grid;
static lv_style_t s_grid_line;
static lv_style_t s_detail_label;
static lv_style_t s_cell_base;
static lv_style_t s_cell_today;
static lv_style_t s_cell_selected;
static lv_style_t s_cell_pressed;
static bool s_styles_ready;

static void _calendar_styles_init(void)
{
    if (s_styles_ready)
    {
        return;
    }
    lv_style_init(&s_nav);
    lv_style_set_pad_column(&s_nav, 8);

    lv_style_init(&s_nav_button);
    lv_style_set_radius(&s_nav_button, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&s_nav_button, lv_color_hex(CALENDAR_COLOR_SURFACE));
    lv_style_set_shadow_width(&s_nav_button, 0);

    lv_style_init(&s_weekday_row);
    lv_style_set_pad_column(&s_weekday_row, CALENDAR_GRID_COLUMN_GAP);

    lv_style_init(&s_month_label);
    lv_style_set_text_align(&s_month_label, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_month_label, lv_color_hex(CALENDAR_COLOR_TEXT));

    lv_style_init(&s_weekday_label);
    lv_style_set_text_align(&s_weekday_label, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_weekday_label, lv_color_hex(CALENDAR_COLOR_MUTED));

    lv_style_init(&s_grid);
    lv_style_set_pad_row(&s_grid, CALENDAR_GRID_ROW_GAP);

    lv_style_init(&s_grid_line);
    lv_style_set_pad_column(&s_grid_line, CALENDAR_GRID_COLUMN_GAP);

    lv_style_init(&s_detail_label);
    lv_style_set_text_align(&s_detail_label, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_color(&s_detail_label, lv_color_hex(CALENDAR_COLOR_TEXT));

    lv_style_init(&s_cell_base);
    lv_style_set_radius(&s_cell_base, APP_UI_RADIUS_CONTROL);
    lv_style_set_shadow_width(&s_cell_base, 0);
    lv_style_set_pad_all(&s_cell_base, 0);
    lv_style_set_border_width(&s_cell_base, 0);
    lv_style_set_bg_opa(&s_cell_base, LV_OPA_TRANSP);

    lv_style_init(&s_cell_today);
    lv_style_set_bg_color(&s_cell_today, lv_color_hex(CALENDAR_COLOR_ACCENT));
    lv_style_set_bg_opa(&s_cell_today, CALENDAR_TODAY_OPA);

    lv_style_init(&s_cell_selected);
    lv_style_set_border_width(&s_cell_selected, 1);
    lv_style_set_border_color(&s_cell_selected,
                              lv_color_hex(CALENDAR_COLOR_ACCENT));
    lv_style_set_border_opa(&s_cell_selected, CALENDAR_RING_OPA);

    lv_style_init(&s_cell_pressed);
    lv_style_set_bg_color(&s_cell_pressed,
                          lv_color_hex(CALENDAR_COLOR_SURFACE_HI));
    lv_style_set_bg_opa(&s_cell_pressed, LV_OPA_COVER);

    s_styles_ready = true;
}

typedef struct calendar_page_state
{
    app_ui_page_t page;
    lv_obj_t *month_label;
    lv_obj_t *detail_label;
    lv_obj_t *grid;
    lv_obj_t *cells[CALENDAR_MATH_MAX_CELLS];
    size_t cell_count;
    lv_timer_t *refresh_timer;
    int year;         /**< Displayed month year. */
    int month;        /**< Displayed month, 1 through 12. */
    int selected_day; /**< Selected day of the displayed month. */
    int today_year;   /**< Last observed local date, when have_today is set. */
    int today_month;
    int today_day;
    bool have_today;
} calendar_page_state_t;

_Static_assert(sizeof(calendar_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Calendar page state exceeds lifecycle arena slot");

static const char *const s_weekday_short[7] =
{
    "日", "一", "二", "三", "四", "五", "六",
};

static void _calendar_update_labels(calendar_page_state_t *state);
static void _calendar_day_event(lv_event_t *event);
static void _calendar_prev_event(lv_event_t *event);
static void _calendar_next_event(lv_event_t *event);
static void _calendar_today_event(lv_event_t *event);

static const char *_calendar_weekday_text(int weekday)
{
    return weekday >= 0 && weekday < 7 ? s_weekday_short[weekday] : "";
}

/* One cell of the month grid: accent tint marks today, an accent outline marks
 * the selected day, and both show together when they are the same date. */
static void _calendar_style_cell(calendar_page_state_t *state, size_t index)
{
    lv_obj_t *cell = state->cells[index];
    if (cell == NULL)
    {
        return;
    }
    const int day = (int)(intptr_t)lv_obj_get_user_data(cell);
    if (day <= 0)
    {
        return;
    }
    const bool is_today = state->have_today &&
                          state->year == state->today_year &&
                          state->month == state->today_month &&
                          day == state->today_day;
    const bool is_selected = day == state->selected_day;

    lv_obj_remove_style(cell, &s_cell_today, 0);
    lv_obj_remove_style(cell, &s_cell_selected, 0);
    if (is_today)
    {
        lv_obj_add_style(cell, &s_cell_today, 0);
    }
    if (is_selected)
    {
        lv_obj_add_style(cell, &s_cell_selected, 0);
    }
}

static void _calendar_apply_cell_styles(calendar_page_state_t *state)
{
    for (size_t index = 0U; index < state->cell_count; index++)
    {
        _calendar_style_cell(state, index);
    }
}

static void _calendar_set_detail(calendar_page_state_t *state,
                                 const char *text, uint32_t color)
{
    if (state->detail_label == NULL)
    {
        return;
    }
    lv_obj_set_style_text_color(state->detail_label, lv_color_hex(color), 0);
    app_ui_label_set_text_if(state->detail_label, text);
}

static void _calendar_update_labels(calendar_page_state_t *state)
{
    if (state->month_label != NULL)
    {
        char text[24];
        (void)snprintf(text, sizeof(text), "%d年%d月", state->year, state->month);
        app_ui_label_set_text_if(state->month_label, text);
    }
    if (state->detail_label == NULL)
    {
        return;
    }
    const int weekday = calendar_math_weekday(state->year, state->month,
                        state->selected_day);
    const int week = calendar_math_week_of_year(state->year, state->month,
                     state->selected_day);
    const int ordinal = calendar_math_day_of_year(state->year, state->month,
                        state->selected_day);
    char text[CALENDAR_DETAIL_BYTES];
    (void)snprintf(text, sizeof(text), "%d月%d日 周%s · 第%d周 · 第%d天",
                   state->month, state->selected_day,
                   _calendar_weekday_text(weekday), week, ordinal);
    _calendar_set_detail(state, text, CALENDAR_COLOR_TEXT);
}

static void _calendar_rebuild_grid(calendar_page_state_t *state)
{
    if (state->grid == NULL)
    {
        return;
    }
    lv_obj_clean(state->grid);
    memset(state->cells, 0, sizeof(state->cells));
    state->cell_count = 0U;

    int days[CALENDAR_MATH_MAX_CELLS];
    const int total = calendar_math_build_month(state->year, state->month,
                      days);
    if (total <= 0)
    {
        return;
    }
    const int rows = total / CALENDAR_MATH_COLUMNS;
    for (int row = 0; row < rows; row++)
    {
        lv_obj_t *line = lv_obj_create(state->grid);
        if (line == NULL)
        {
            return;
        }
        lv_obj_remove_style_all(line);
        lv_obj_set_width(line, LV_PCT(100));
        lv_obj_set_height(line, CALENDAR_CELL_HEIGHT);
        lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_style(line, &s_grid_line, 0);
        app_ui_make_passive(line, false);

        for (int column = 0; column < CALENDAR_MATH_COLUMNS; column++)
        {
            const size_t index =
                (size_t)(row * CALENDAR_MATH_COLUMNS + column);
            const int day = days[index];
            lv_obj_t *cell = lv_button_create(line);
            if (cell == NULL)
            {
                return;
            }
            app_ui_click_only(cell);
            lv_obj_set_width(cell, 0);
            lv_obj_set_flex_grow(cell, 1);
            lv_obj_set_height(cell, LV_PCT(100));
            lv_obj_add_style(cell, &s_cell_base, 0);
            lv_obj_add_style(cell, &s_cell_pressed, LV_STATE_PRESSED);
            lv_obj_set_user_data(cell, (void *)(intptr_t)day);
            state->cells[index] = cell;
            state->cell_count = index + 1U;

            if (day <= 0)
            {
                /* Leading/trailing position of an adjacent month: keep the
                 * grid geometry, never a tap target. */
                app_ui_make_passive(cell, false);
                continue;
            }
            lv_obj_add_event_cb(cell, _calendar_day_event, LV_EVENT_CLICKED,
                                state);
            lv_obj_t *label = lv_label_create(cell);
            if (label == NULL)
            {
                continue;
            }
            char text[12];
            (void)snprintf(text, sizeof(text), "%d", day);
            lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY),
                                       0);
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(CALENDAR_COLOR_TEXT), 0);
            lv_label_set_text(label, text);
            lv_obj_center(label);
        }
    }
    _calendar_apply_cell_styles(state);
}

static void _calendar_show_month(calendar_page_state_t *state, int year,
                                 int month)
{
    state->year = year;
    state->month = month;
    const int days = calendar_math_days_in_month(year, month);
    if (state->selected_day > days)
    {
        state->selected_day = days;
    }
    if (state->selected_day < 1)
    {
        state->selected_day = 1;
    }
    _calendar_rebuild_grid(state);
    _calendar_update_labels(state);
}

static void _calendar_shift_month(calendar_page_state_t *state, int delta)
{
    if (!state->have_today)
    {
        return;
    }
    int year = state->year;
    int month = state->month + delta;
    if (month > 12)
    {
        month = 1;
        year++;
    }
    else if (month < 1)
    {
        month = 12;
        year--;
    }
    _calendar_show_month(state, year, month);
}

static void _calendar_show_today(calendar_page_state_t *state)
{
    if (!state->have_today)
    {
        return;
    }
    state->selected_day = state->today_day;
    if (state->year != state->today_year || state->month != state->today_month)
    {
        _calendar_show_month(state, state->today_year, state->today_month);
        return;
    }
    _calendar_apply_cell_styles(state);
    _calendar_update_labels(state);
}

/* Read the local clock every second: the page only has to notice a date
 * rollover, so a matching date is a no-op. */
static void _calendar_sync(calendar_page_state_t *state)
{
    struct tm local_time;
    if (time_service_get_local(&local_time) != ESP_OK)
    {
        if (state->have_today || state->cell_count != 0U)
        {
            state->have_today = false;
            state->cell_count = 0U;
            memset(state->cells, 0, sizeof(state->cells));
            if (state->grid != NULL)
            {
                lv_obj_clean(state->grid);
            }
            if (state->month_label != NULL)
            {
                app_ui_label_set_text_if(state->month_label, "--");
            }
            _calendar_set_detail(state, "等待有效时间", CALENDAR_COLOR_WARNING);
        }
        return;
    }

    const int year = local_time.tm_year + 1900;
    const int month = local_time.tm_mon + 1;
    const int day = local_time.tm_mday;
    const bool first_valid = !state->have_today;
    /* While the page still shows the previous "today" the view follows the
     * clock; once the user browses another month it is left alone. */
    const bool follows_today = first_valid ||
                               (state->year == state->today_year &&
                                state->month == state->today_month);
    const bool date_changed = first_valid || state->today_year != year ||
                              state->today_month != month ||
                              state->today_day != day;

    state->have_today = true;
    state->today_year = year;
    state->today_month = month;
    state->today_day = day;
    if (date_changed && follows_today &&
            (first_valid || state->year != year || state->month != month))
    {
        state->year = year;
        state->month = month;
        state->selected_day = day;
        _calendar_rebuild_grid(state);
        _calendar_update_labels(state);
        return;
    }
    if (!date_changed)
    {
        return;
    }
    const int days = calendar_math_days_in_month(state->year, state->month);
    if (state->selected_day > days)
    {
        state->selected_day = days;
    }
    _calendar_apply_cell_styles(state);
    _calendar_update_labels(state);
}

static void _calendar_day_event(lv_event_t *event)
{
    calendar_page_state_t *state = lv_event_get_user_data(event);
    lv_obj_t *cell = lv_event_get_target(event);
    if (state == NULL || cell == NULL)
    {
        return;
    }
    const int day = (int)(intptr_t)lv_obj_get_user_data(cell);
    if (day < 1 || day > calendar_math_days_in_month(state->year, state->month))
    {
        return;
    }
    if (day == state->selected_day)
    {
        return;
    }
    state->selected_day = day;
    _calendar_apply_cell_styles(state);
    _calendar_update_labels(state);
}

static void _calendar_prev_event(lv_event_t *event)
{
    _calendar_shift_month(lv_event_get_user_data(event), -1);
}

static void _calendar_next_event(lv_event_t *event)
{
    _calendar_shift_month(lv_event_get_user_data(event), 1);
}

static void _calendar_today_event(lv_event_t *event)
{
    _calendar_show_today(lv_event_get_user_data(event));
}

static void _calendar_refresh_timer(lv_timer_t *timer)
{
    _calendar_sync(lv_timer_get_user_data(timer));
}

static lv_obj_t *_calendar_make_button(lv_obj_t *row, int32_t width,
                                       const char *text, bool symbol,
                                       lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(row);
    if (button == NULL)
    {
        return NULL;
    }
    app_ui_click_only(button);
    lv_obj_set_size(button, width, CALENDAR_NAV_HEIGHT);
    lv_obj_add_style(button, &s_nav_button, 0);
    lv_obj_add_style(button, &s_cell_pressed, LV_STATE_PRESSED);
    if (callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = lv_label_create(button);
    if (label != NULL)
    {
        /* LVGL symbol glyphs only exist in the built-in default font. */
        lv_obj_set_style_text_font(label, symbol ? LV_FONT_DEFAULT :
                                   app_ui_font(APP_THEME_FONT_BODY), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(CALENDAR_COLOR_TEXT), 0);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }
    return button;
}

static void _calendar_build_nav(calendar_page_state_t *state)
{
    lv_obj_t *nav = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, CALENDAR_NAV_HEIGHT);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(nav, &s_nav, 0);
    app_ui_make_passive(nav, false);

    (void)_calendar_make_button(nav, CALENDAR_NAV_BUTTON_WIDTH, LV_SYMBOL_LEFT,
                                true, _calendar_prev_event, state);

    state->month_label = lv_label_create(nav);
    if (state->month_label != NULL)
    {
        lv_obj_set_width(state->month_label, 0);
        lv_obj_set_flex_grow(state->month_label, 1);
        lv_label_set_long_mode(state->month_label, LV_LABEL_LONG_DOT);
        lv_obj_add_style(state->month_label, &s_month_label, 0);
        lv_obj_set_style_text_font(state->month_label,
                                   app_ui_font(APP_THEME_FONT_HEAD), 0);
        lv_label_set_text(state->month_label, "--");
    }

    (void)_calendar_make_button(nav, CALENDAR_NAV_BUTTON_WIDTH, LV_SYMBOL_RIGHT,
                                true, _calendar_next_event, state);
    (void)_calendar_make_button(nav, CALENDAR_TODAY_WIDTH, "今天", false,
                                _calendar_today_event, state);
}

static void _calendar_build_weekdays(calendar_page_state_t *state)
{
    lv_obj_t *row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, CALENDAR_WEEKDAY_HEIGHT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(row, &s_weekday_row, 0);
    app_ui_make_passive(row, false);

    for (int weekday = 0; weekday < CALENDAR_MATH_COLUMNS; weekday++)
    {
        lv_obj_t *label = lv_label_create(row);
        if (label == NULL)
        {
            return;
        }
        lv_obj_set_width(label, 0);
        lv_obj_set_flex_grow(label, 1);
        lv_obj_add_style(label, &s_weekday_label, 0);
        lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(label, _calendar_weekday_text(weekday));
    }
}

static void _calendar_build_body(calendar_page_state_t *state)
{
    _calendar_build_nav(state);
    _calendar_build_weekdays(state);

    state->grid = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->grid);
    lv_obj_set_width(state->grid, LV_PCT(100));
    lv_obj_set_height(state->grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state->grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_style(state->grid, &s_grid, 0);
    app_ui_make_passive(state->grid, false);

    state->detail_label = lv_label_create(state->page.content);
    if (state->detail_label != NULL)
    {
        lv_obj_set_width(state->detail_label, LV_PCT(100));
        lv_label_set_long_mode(state->detail_label, LV_LABEL_LONG_DOT);
        lv_obj_add_style(state->detail_label, &s_detail_label, 0);
        lv_obj_set_style_text_font(state->detail_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_label_set_text(state->detail_label, "");
    }
}

static void _calendar_mount(const app_manager_page_context_t *context)
{
    calendar_page_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    _calendar_styles_init();
    app_ui_page_create(&state->page, "日历", true);
    /* Six grid rows plus the header and detail line always fit one screen, so
     * the page never scrolls. */
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);
    _calendar_build_body(state);
    _calendar_sync(state);
    state->refresh_timer = lv_timer_create(_calendar_refresh_timer,
                                           CALENDAR_REFRESH_MS, state);
}

static void _calendar_resume(const app_manager_page_context_t *context)
{
    calendar_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _calendar_sync(state);
}

static esp_err_t _calendar_pause(const app_manager_page_context_t *context)
{
    calendar_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _calendar_unmount(const app_manager_page_context_t *context)
{
    calendar_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->month_label = NULL;
    state->detail_label = NULL;
    state->grid = NULL;
    state->cell_count = 0U;
    memset(state->cells, 0, sizeof(state->cells));
}

static const app_manager_page_ops_t s_calendar_ops =
{
    .mount = _calendar_mount,
    .resume = _calendar_resume,
    .pause = _calendar_pause,
    .unmount = _calendar_unmount,
};

static const app_manager_page_definition_t s_calendar_definition =
{
    .ops = &s_calendar_ops,
    .memory_size = sizeof(calendar_page_state_t),
};

static const app_manager_page_route_t s_calendar_routes[] =
{
    {
        .page_id = "root", .definition = &s_calendar_definition,
        .user_data = NULL
    },
};

APP_MANAGER_APP_EXPORT_META(calendar, APP_IMAGE_CALENDAR_ICON, "日历",
                            APP_MANAGER_ID_CALENDAR, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_calendar_routes, 25U,
                            "月历与日期");