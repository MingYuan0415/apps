#define DBG_TAG "clock_app"
#include "mt_log.h"

#include "clock_app_internal.h"

#include <string.h>
#include <time.h>

typedef enum
{
    CLOCK_CARD_COUNTDOWN = 0,
    CLOCK_CARD_STOPWATCH,
    CLOCK_CARD_FOCUS,
    CLOCK_CARD_COUNT
} clock_card_t;

typedef struct clock_root_state
{
    app_ui_page_t page;
    lv_obj_t *time_label;
    lv_obj_t *seconds_label;
    lv_obj_t *date_label;
    lv_obj_t *source_pill;
    lv_obj_t *day_label;
    lv_obj_t *day_remaining_label;
    lv_obj_t *day_bar;
    lv_obj_t *card_summary[CLOCK_CARD_COUNT];
    lv_obj_t *card_ring[CLOCK_CARD_COUNT];
    lv_obj_t *card_arrow[CLOCK_CARD_COUNT];
    lv_timer_t *refresh_timer;
    char last_summary[CLOCK_CARD_COUNT][32];
    char last_time_text[8];
    char last_seconds_text[4];
    char last_date_text[24];
    char last_day_text[24];
    char last_day_remaining[24];
    uint8_t last_timer_state[CLOCK_CARD_COUNT];
    uint32_t last_span[CLOCK_CARD_COUNT];
    uint32_t last_color[CLOCK_CARD_COUNT];
    uint32_t last_text_color[CLOCK_CARD_COUNT];
    uint32_t last_day_progress;
    bool card_initialized[CLOCK_CARD_COUNT];
    bool card_active[CLOCK_CARD_COUNT];
    bool pulse_used;
    bool day_progress_valid;
} clock_root_state_t;

_Static_assert(sizeof(clock_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Clock root state exceeds the lifecycle arena slot");

void clock_ui_format_mmss(uint32_t milliseconds, char *output,
                          size_t output_size)
{
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(milliseconds / 60000U),
                   (unsigned)((milliseconds / 1000U) % 60U));
}

void clock_ui_ring_update(lv_obj_t *ring, bool active, uint32_t remaining_ms,
                          uint32_t total_ms, uint32_t color)
{
    if (ring == NULL)
    {
        return;
    }
    lv_obj_set_style_arc_color(ring, lv_color_hex(color), LV_PART_INDICATOR);
    if (!active)
    {
        lv_arc_set_angles(ring, 0, 0);
        lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
        return;
    }
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
    /* 64-bit: 360 * 779 min in ms would wrap a uint32 product. */
    const uint32_t span = (total_ms > 0U && remaining_ms <= total_ms) ?
                          (uint32_t)(((uint64_t)remaining_ms * 360U) /
                                     total_ms) : 360U;
    lv_arc_set_angles(ring, 0, (lv_value_precise_t)span);
}

static void _clock_ui_opa_animation(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

void clock_ui_pulse(lv_obj_t *object)
{
    if (object == NULL)
    {
        return;
    }
    (void)lv_anim_delete(object, NULL);
    lv_obj_set_style_opa(object, LV_OPA_60, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, LV_OPA_60, LV_OPA_COVER);
    lv_anim_set_duration(&animation, CLOCK_UI_PULSE_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, _clock_ui_opa_animation);
    (void)lv_anim_start(&animation);
}

lv_obj_t *clock_ui_card_create(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (card == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(card, false);
    return card;
}

lv_obj_t *clock_ui_label_create(lv_obj_t *parent, const char *text,
                                app_theme_font_id_t font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static uint32_t s_minutes = 5U;

uint32_t clock_ui_minutes_get(void)
{
    return s_minutes;
}

void clock_ui_minutes_set(uint32_t minutes)
{
    if (minutes == 0U)
    {
        minutes = 1U;
    }
    if (minutes > 779U)
    {
        minutes = 779U;
    }
    s_minutes = minutes;
}

static void _clock_nav_complete(esp_err_t result, void *context)
{
    (void)context;
    if (result != ESP_OK)
    {
        LOG_W("clock navigation failed: %s", esp_err_to_name(result));
    }
}

void clock_ui_open_page_with_minutes(const char *page_id, uint32_t minutes)
{
    const clock_duration_arguments_t payload = { .minutes = minutes };
    app_manager_nav_request_t request =
    {
        .operation = APP_MANAGER_NAV_OP_OPEN_PAGE,
        .app_id = APP_MANAGER_ID_CLOCK,
        .page_id = page_id,
        .has_arguments = true,
        .arguments =
        {
            .version = APP_MANAGER_TYPED_BLOB_VERSION,
            .type = CLOCK_ARGUMENT_MINUTES,
            .size = sizeof(payload),
        },
        .transition = { .effect = APP_MANAGER_TRANSITION_DEFAULT },
    };
    memcpy(request.arguments.payload, &payload, sizeof(payload));
    const esp_err_t result = app_manager_navigate_async(&request,
        _clock_nav_complete, NULL);
    if (result != ESP_OK)
    {
        LOG_W("clock page %s navigation failed: %s", page_id,
              esp_err_to_name(result));
    }
}

bool clock_ui_take_minutes_argument(uint32_t *minutes)
{
    const app_manager_typed_blob_t *arguments =
        app_manager_this_page_arguments();
    if (arguments == NULL ||
            arguments->version != APP_MANAGER_TYPED_BLOB_VERSION ||
            arguments->type != CLOCK_ARGUMENT_MINUTES ||
            arguments->size != sizeof(clock_duration_arguments_t))
    {
        return false;
    }
    clock_duration_arguments_t payload;
    memcpy(&payload, arguments->payload, sizeof(payload));
    if (payload.minutes == 0U || payload.minutes > 779U)
    {
        return false;
    }
    *minutes = payload.minutes;
    return true;
}

static void _clock_root_set_summary(clock_root_state_t *state,
                                    clock_card_t card, const char *text,
                                    uint32_t color, uint32_t span,
                                    bool active, uint32_t total_ms,
                                    uint32_t remaining_ms,
                                    uint8_t timer_state)
{
    if (strcmp(state->last_summary[card], text) != 0)
    {
        (void)snprintf(state->last_summary[card],
                       sizeof(state->last_summary[card]), "%s", text);
        lv_label_set_text(state->card_summary[card], text);
    }
    if (state->last_text_color[card] != color)
    {
        state->last_text_color[card] = color;
        lv_obj_set_style_text_color(state->card_summary[card],
                                    lv_color_hex(color), 0);
    }
    if (!state->card_initialized[card] || state->card_active[card] != active)
    {
        state->card_active[card] = active;
        lv_obj_set_hidden(state->card_ring[card], !active);
        lv_obj_set_hidden(state->card_arrow[card], active);
    }
    const bool state_changed = state->last_timer_state[card] != timer_state;
    if (state->last_span[card] != span || state->last_color[card] != color ||
            state_changed)
    {
        state->last_span[card] = span;
        state->last_color[card] = color;
        state->last_timer_state[card] = timer_state;
        clock_ui_ring_update(state->card_ring[card], active, remaining_ms,
                             total_ms, color);
        /* A ring pulse acknowledges a live transition. The one-shot budget is
         * consumed per render so a burst of timer changes cannot animate more
         * than one object, and the first paint never animates. */
        if (state_changed && state->card_initialized[card] &&
                timer_state != TIMER_SERVICE_IDLE && !state->pulse_used)
        {
            clock_ui_pulse(state->card_ring[card]);
            state->pulse_used = true;
        }
    }
    state->card_initialized[card] = true;
}

static void _clock_root_render_day(clock_root_state_t *state,
                                   const struct tm *local_time)
{
    const uint32_t elapsed_seconds = (uint32_t)local_time->tm_hour * 3600U +
                                     (uint32_t)local_time->tm_min * 60U +
                                     (uint32_t)local_time->tm_sec;
    const uint32_t progress = (uint32_t)(((uint64_t)elapsed_seconds * 100U) /
                                         86400U);
    char text[24];
    (void)snprintf(text, sizeof(text), "今日 %u%%", (unsigned)progress);
    if (strcmp(state->last_day_text, text) != 0)
    {
        (void)snprintf(state->last_day_text, sizeof(state->last_day_text),
                       "%s", text);
        lv_label_set_text(state->day_label, text);
    }
    const uint32_t remaining_minutes =
        (86400U - elapsed_seconds + 59U) / 60U;
    char remaining_text[24];
    if (remaining_minutes >= 60U)
    {
        (void)snprintf(remaining_text, sizeof(remaining_text), "剩 %u 小时",
                       (unsigned)(remaining_minutes / 60U));
    }
    else
    {
        (void)snprintf(remaining_text, sizeof(remaining_text), "剩 %u 分钟",
                       (unsigned)remaining_minutes);
    }
    if (strcmp(state->last_day_remaining, remaining_text) != 0)
    {
        (void)snprintf(state->last_day_remaining,
                       sizeof(state->last_day_remaining), "%s",
                       remaining_text);
        lv_label_set_text(state->day_remaining_label, remaining_text);
    }
    if (!state->day_progress_valid || state->last_day_progress != progress)
    {
        state->day_progress_valid = true;
        state->last_day_progress = progress;
        lv_bar_set_value(state->day_bar, (int32_t)progress, LV_ANIM_OFF);
    }
}

static void _clock_root_render(clock_root_state_t *state)
{
    static const char *const weekdays[7] = {"日", "一", "二", "三", "四",
                                            "五", "六"
                                           };
    struct tm local_time;
    state->pulse_used = false;
    if (time_service_get_local(&local_time) == ESP_OK)
    {
        char time_text[8];
        char seconds_text[4];
        char date_text[24];
        (void)strftime(time_text, sizeof(time_text), "%H:%M", &local_time);
        if (strcmp(state->last_time_text, time_text) != 0)
        {
            (void)snprintf(state->last_time_text,
                           sizeof(state->last_time_text), "%s", time_text);
            lv_label_set_text(state->time_label, time_text);
        }
        (void)strftime(seconds_text, sizeof(seconds_text), ":%S", &local_time);
        if (strcmp(state->last_seconds_text, seconds_text) != 0)
        {
            (void)snprintf(state->last_seconds_text,
                           sizeof(state->last_seconds_text), "%s",
                           seconds_text);
            lv_label_set_text(state->seconds_label, seconds_text);
        }
        (void)snprintf(date_text, sizeof(date_text), "%d月%d日 周%s",
                       local_time.tm_mon + 1, local_time.tm_mday,
                       weekdays[(unsigned)local_time.tm_wday % 7U]);
        if (strcmp(state->last_date_text, date_text) != 0)
        {
            (void)snprintf(state->last_date_text,
                           sizeof(state->last_date_text), "%s", date_text);
            lv_label_set_text(state->date_label, date_text);
        }
        _clock_root_render_day(state, &local_time);
    }
    else
    {
        lv_label_set_text(state->time_label, "--:--");
        lv_label_set_text(state->seconds_label, "");
        lv_label_set_text(state->date_label, "等待有效时间");
        lv_label_set_text(state->day_label, "今日 --");
        lv_label_set_text(state->day_remaining_label, "等待校时");
        lv_bar_set_value(state->day_bar, 0, LV_ANIM_OFF);
        state->last_time_text[0] = '\0';
        state->last_seconds_text[0] = '\0';
        state->last_date_text[0] = '\0';
        state->last_day_text[0] = '\0';
        state->last_day_remaining[0] = '\0';
        state->day_progress_valid = false;
    }
    switch (time_service_get_quality())
    {
    case TIME_SERVICE_QUALITY_NTP:
        app_ui_pill_set(state->source_pill, "NTP 校时",
                        APP_UI_STATUS_SUCCESS);
        break;
    case TIME_SERVICE_QUALITY_RTC:
        app_ui_pill_set(state->source_pill, "RTC 时间",
                        APP_UI_STATUS_ACCENT);
        break;
    case TIME_SERVICE_QUALITY_MANUAL:
        app_ui_pill_set(state->source_pill, "手动设置",
                        APP_UI_STATUS_NEUTRAL);
        break;
    default:
        app_ui_pill_set(state->source_pill, "未校准",
                        APP_UI_STATUS_WARNING);
        break;
    }

    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    char mmss[12];
    char summary[24];
    switch (snapshot.countdown_state)
    {
    case TIMER_SERVICE_IDLE:
        _clock_root_set_summary(state, CLOCK_CARD_COUNTDOWN,
                                "1 / 5 / 10 / 25 分钟",
                                APP_UI_COLOR_MUTED, 0U, false, 0U, 0U,
                                TIMER_SERVICE_IDLE);
        break;
    case TIMER_SERVICE_COMPLETED:
        _clock_root_set_summary(state, CLOCK_CARD_COUNTDOWN, "已完成",
                                APP_UI_COLOR_SUN, 0U, false, 0U, 0U,
                                TIMER_SERVICE_COMPLETED);
        break;
    default:
        clock_ui_format_mmss(snapshot.countdown_remaining_ms, mmss,
                             sizeof(mmss));
        (void)snprintf(summary, sizeof(summary),
                       snapshot.countdown_state == TIMER_SERVICE_PAUSED ?
                       "暂停 %s" : "剩余 %s", mmss);
        _clock_root_set_summary(state, CLOCK_CARD_COUNTDOWN, summary,
                                snapshot.countdown_state ==
                                TIMER_SERVICE_PAUSED ?
                                APP_UI_COLOR_MUTED : APP_UI_COLOR_RAIN,
                                snapshot.countdown_remaining_ms, true,
                                snapshot.countdown_duration_ms,
                                snapshot.countdown_remaining_ms,
                                (uint8_t)snapshot.countdown_state);
        break;
    }
    if (snapshot.stopwatch_state == TIMER_SERVICE_IDLE)
    {
        _clock_root_set_summary(state, CLOCK_CARD_STOPWATCH, "未开始",
                                APP_UI_COLOR_MUTED, 0U, false, 0U, 0U,
                                TIMER_SERVICE_IDLE);
    }
    else
    {
        clock_ui_format_mmss((uint32_t)(snapshot.stopwatch_elapsed_ms /
                                        1000U * 1000U), mmss, sizeof(mmss));
        const uint32_t seconds_part = (uint32_t)(snapshot.stopwatch_elapsed_ms
            % 60000ULL);
        _clock_root_set_summary(state, CLOCK_CARD_STOPWATCH, mmss,
                                snapshot.stopwatch_state ==
                                TIMER_SERVICE_PAUSED ? APP_UI_COLOR_MUTED :
                                APP_UI_COLOR_TEXT,
                                seconds_part, true, 60000U,
                                60000U - seconds_part,
                                (uint8_t)snapshot.stopwatch_state);
    }
    if (snapshot.focus_state == TIMER_SERVICE_IDLE)
    {
        _clock_root_set_summary(state, CLOCK_CARD_FOCUS,
                                "25/5 · 45/10 · 50/10", APP_UI_COLOR_MUTED,
                                0U, false, 0U, 0U, TIMER_SERVICE_IDLE);
    }
    else
    {
        clock_ui_format_mmss(snapshot.focus_remaining_ms, mmss,
                             sizeof(mmss));
        const bool paused = snapshot.focus_state == TIMER_SERVICE_PAUSED;
        (void)snprintf(summary, sizeof(summary),
                       paused ? "暂停 %s" :
                       (snapshot.focus_phase == TIMER_SERVICE_FOCUS_WORK ?
                        "专注 %s" : "休息 %s"), mmss);
        _clock_root_set_summary(state, CLOCK_CARD_FOCUS, summary,
                                paused ? APP_UI_COLOR_MUTED :
                                APP_UI_COLOR_SUN,
                                snapshot.focus_remaining_ms, true,
                                snapshot.focus_phase ==
                                TIMER_SERVICE_FOCUS_WORK ?
                                snapshot.focus_work_ms :
                                snapshot.focus_break_ms,
                                snapshot.focus_remaining_ms,
                                (uint8_t)snapshot.focus_state);
    }
}

static void _clock_root_refresh(lv_timer_t *timer)
{
    _clock_root_render(lv_timer_get_user_data(timer));
}

static void _clock_open_card(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    const char *page_id = (const char *)lv_event_get_user_data(event);
    app_ui_request_open_page(APP_MANAGER_ID_CLOCK, page_id);
}

static lv_obj_t *_clock_root_group_create(lv_obj_t *parent)
{
    lv_obj_t *group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_width(group, LV_PCT(100));
    lv_obj_set_height(group, 0);
    lv_obj_set_flex_grow(group, 1);
    lv_obj_set_style_min_height(group, APP_UI_ROW_HEIGHT * 3 + 8, 0);
    lv_obj_set_style_radius(group, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(group, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(group, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(group, 4, 0);
    lv_obj_set_style_pad_row(group, 0, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    app_ui_make_passive(group, false);
    return group;
}

static void _clock_root_add_card(clock_root_state_t *state, lv_obj_t *group,
                                 const char *name, const char *page_id,
                                 clock_card_t card)
{
    lv_obj_t *row = lv_button_create(group);
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 0);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_min_height(row, APP_UI_ROW_HEIGHT, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, card != CLOCK_CARD_COUNTDOWN ? 1 : 0,
                                  0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(APP_UI_COLOR_HAIRLINE),
                                  0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, _clock_open_card, LV_EVENT_CLICKED,
                        (void *)page_id);

    lv_obj_t *text = lv_obj_create(row);
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(text, 2, 0);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    app_ui_make_passive(text, false);

    lv_obj_t *title = lv_label_create(text);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_height(title,
                      lv_font_get_line_height(app_ui_font(APP_THEME_FONT_BODY)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(title, name);
    app_ui_make_passive(title, false);

    state->card_summary[card] = lv_label_create(text);
    lv_obj_set_width(state->card_summary[card], LV_PCT(100));
    lv_obj_set_height(state->card_summary[card], LV_SIZE_CONTENT);
    lv_label_set_long_mode(state->card_summary[card], LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->card_summary[card],
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->card_summary[card],
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->card_summary[card], "");
    app_ui_make_passive(state->card_summary[card], false);

    state->card_ring[card] = app_ui_ring_create(row, 40, 5,
        APP_UI_COLOR_TRACK);

    state->card_arrow[card] = lv_label_create(row);
    lv_obj_set_style_text_font(state->card_arrow[card], LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(state->card_arrow[card],
                                lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_label_set_text(state->card_arrow[card], LV_SYMBOL_RIGHT);
    app_ui_make_passive(state->card_arrow[card], false);
}

static void _clock_root_mount(const app_manager_page_context_t *context)
{
    clock_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "时钟", true);
    lv_obj_set_style_pad_bottom(state->page.content, 12, 0);
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_set_scrollable(state->page.content, false);

    lv_obj_t *hero = clock_ui_card_create(state->page.content);
    lv_obj_set_style_pad_all(hero, 12, 0);
    lv_obj_set_style_pad_row(hero, 4, 0);

    lv_obj_t *clock_row = lv_obj_create(hero);
    lv_obj_remove_style_all(clock_row);
    lv_obj_set_width(clock_row, LV_PCT(100));
    lv_obj_set_height(clock_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(clock_row, 6, 0);
    app_ui_make_passive(clock_row, false);

    state->time_label = lv_label_create(clock_row);
    lv_obj_set_style_text_color(state->time_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->time_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    lv_label_set_text(state->time_label, "--:--");
    app_ui_make_passive(state->time_label, false);

    state->seconds_label = lv_label_create(clock_row);
    lv_obj_set_style_text_color(state->seconds_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->seconds_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_obj_set_style_pad_bottom(state->seconds_label, 10, 0);
    lv_label_set_text(state->seconds_label, "");
    app_ui_make_passive(state->seconds_label, false);

    lv_obj_t *meta = lv_obj_create(hero);
    lv_obj_remove_style_all(meta);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_obj_set_height(meta, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta, 8, 0);
    app_ui_make_passive(meta, false);

    state->date_label = lv_label_create(meta);
    lv_obj_set_width(state->date_label, 0);
    lv_obj_set_height(state->date_label, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(state->date_label, 1);
    lv_label_set_long_mode(state->date_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state->date_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->date_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->date_label, "");
    app_ui_make_passive(state->date_label, false);

    state->source_pill = app_ui_add_pill(meta, "未校准",
                                         APP_UI_STATUS_WARNING);

    lv_obj_t *day_row = lv_obj_create(hero);
    lv_obj_remove_style_all(day_row);
    lv_obj_set_width(day_row, LV_PCT(100));
    lv_obj_set_height(day_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(day_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(day_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(day_row, 8, 0);
    app_ui_make_passive(day_row, false);

    state->day_label = lv_label_create(day_row);
    lv_obj_set_width(state->day_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(state->day_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->day_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->day_label, "今日 --");
    app_ui_make_passive(state->day_label, false);

    state->day_bar = lv_bar_create(hero);
    lv_obj_set_width(state->day_bar, LV_PCT(100));
    lv_obj_set_height(state->day_bar, 4);
    lv_bar_set_range(state->day_bar, 0, 100);
    lv_bar_set_value(state->day_bar, 0, LV_ANIM_OFF);
    lv_bar_set_mode(state->day_bar, LV_BAR_MODE_RANGE);
    lv_obj_set_style_radius(state->day_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->day_bar,
                              lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_set_style_bg_opa(state->day_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->day_bar, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->day_bar,
                              lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(state->day_bar, LV_OPA_COVER,
                            LV_PART_INDICATOR);
    app_ui_make_passive(state->day_bar, false);

    state->day_remaining_label = lv_label_create(day_row);
    lv_obj_set_width(state->day_remaining_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(state->day_remaining_label,
                                lv_color_hex(APP_UI_COLOR_FAINT), 0);
    lv_obj_set_style_text_font(state->day_remaining_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->day_remaining_label, "等待校时");
    app_ui_make_passive(state->day_remaining_label, false);

    lv_obj_t *group = _clock_root_group_create(state->page.content);
    _clock_root_add_card(state, group, "倒计时", CLOCK_PAGE_COUNTDOWN,
                         CLOCK_CARD_COUNTDOWN);
    _clock_root_add_card(state, group, "秒表", CLOCK_PAGE_STOPWATCH,
                         CLOCK_CARD_STOPWATCH);
    _clock_root_add_card(state, group, "专注", CLOCK_PAGE_FOCUS,
                         CLOCK_CARD_FOCUS);

    state->refresh_timer = lv_timer_create(_clock_root_refresh, 250U, state);
    _clock_root_render(state);
}

static esp_err_t _clock_root_pause(const app_manager_page_context_t *context)
{
    clock_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _clock_root_resume(const app_manager_page_context_t *context)
{
    clock_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _clock_root_render(state);
}

static void _clock_root_unmount(const app_manager_page_context_t *context)
{
    clock_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->time_label = NULL;
    state->seconds_label = NULL;
    state->date_label = NULL;
    state->source_pill = NULL;
    state->day_label = NULL;
    state->day_remaining_label = NULL;
    state->day_bar = NULL;
    for (size_t index = 0U; index < CLOCK_CARD_COUNT; ++index)
    {
        state->card_summary[index] = NULL;
        state->card_ring[index] = NULL;
        state->card_arrow[index] = NULL;
    }
}

static const app_manager_page_ops_t s_clock_root_ops =
{
    .mount = _clock_root_mount,
    .resume = _clock_root_resume,
    .pause = _clock_root_pause,
    .unmount = _clock_root_unmount,
};

const app_manager_page_definition_t clock_root_page_definition =
{
    .ops = &s_clock_root_ops,
    .memory_size = sizeof(clock_root_state_t),
};

static const app_manager_page_route_t s_clock_routes[] =
{
    {
        .page_id = "root",
        .definition = &clock_root_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = CLOCK_PAGE_COUNTDOWN,
        .definition = &clock_countdown_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = CLOCK_PAGE_STOPWATCH,
        .definition = &clock_stopwatch_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = CLOCK_PAGE_FOCUS,
        .definition = &clock_focus_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = CLOCK_PAGE_DURATION,
        .definition = &clock_duration_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(clock, APP_IMAGE_CLOCK_ICON, "时钟",
                            APP_MANAGER_ID_CLOCK, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_clock_routes, 20U,
                            "倒计时与专注");
