#define DBG_TAG "clock_app"
#define DBG_LVL DBG_INFO
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
    lv_obj_t *source_label;
    lv_obj_t *card_summary[CLOCK_CARD_COUNT];
    lv_obj_t *card_ring[CLOCK_CARD_COUNT];
    lv_timer_t *refresh_timer;
    char last_summary[CLOCK_CARD_COUNT][32];
    uint32_t last_span[CLOCK_CARD_COUNT];
    uint32_t last_color[CLOCK_CARD_COUNT];
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
    if (arguments == NULL || arguments->type != CLOCK_ARGUMENT_MINUTES ||
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
                                    uint32_t remaining_ms)
{
    if (strcmp(state->last_summary[card], text) != 0)
    {
        (void)snprintf(state->last_summary[card],
                       sizeof(state->last_summary[card]), "%s", text);
        lv_label_set_text(state->card_summary[card], text);
        lv_obj_set_style_text_color(state->card_summary[card],
                                    lv_color_hex(color), 0);
    }
    if (state->last_span[card] != span || state->last_color[card] != color)
    {
        state->last_span[card] = span;
        state->last_color[card] = color;
        clock_ui_ring_update(state->card_ring[card], active, remaining_ms,
                             total_ms, color);
    }
}

static void _clock_root_render(clock_root_state_t *state)
{
    static const char *const weekdays[7] = {"日", "一", "二", "三", "四",
                                            "五", "六"
                                           };
    char text[64];
    struct tm local_time;
    if (time_service_get_local(&local_time) == ESP_OK)
    {
        (void)strftime(text, sizeof(text), "%H:%M", &local_time);
        lv_label_set_text(state->time_label, text);
        (void)strftime(text, sizeof(text), ":%S", &local_time);
        lv_label_set_text(state->seconds_label, text);
        (void)snprintf(text, sizeof(text), "%d月%d日 周%s",
                       local_time.tm_mon + 1, local_time.tm_mday,
                       weekdays[(unsigned)local_time.tm_wday % 7U]);
        lv_label_set_text(state->date_label, text);
    }
    else
    {
        lv_label_set_text(state->time_label, "--:--");
        lv_label_set_text(state->seconds_label, "");
        lv_label_set_text(state->date_label, "等待有效时间");
    }
    switch (time_service_get_quality())
    {
    case TIME_SERVICE_QUALITY_NTP:
        lv_label_set_text(state->source_label, "来源：NTP 校时");
        break;
    case TIME_SERVICE_QUALITY_RTC:
        lv_label_set_text(state->source_label, "来源：RTC 恢复");
        break;
    case TIME_SERVICE_QUALITY_MANUAL:
        lv_label_set_text(state->source_label, "来源：手动设置");
        break;
    default:
        lv_label_set_text(state->source_label, "来源：未就绪");
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
                                APP_UI_COLOR_MUTED, 0U, false, 0U, 0U);
        break;
    case TIMER_SERVICE_COMPLETED:
        _clock_root_set_summary(state, CLOCK_CARD_COUNTDOWN, "已完成",
                                APP_UI_COLOR_SUN, 0U, false, 0U, 0U);
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
                                snapshot.countdown_remaining_ms);
        break;
    }
    if (snapshot.stopwatch_state == TIMER_SERVICE_IDLE)
    {
        _clock_root_set_summary(state, CLOCK_CARD_STOPWATCH, "未开始",
                                APP_UI_COLOR_MUTED, 0U, false, 0U, 0U);
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
                                60000U - seconds_part);
    }
    if (snapshot.focus_state == TIMER_SERVICE_IDLE)
    {
        _clock_root_set_summary(state, CLOCK_CARD_FOCUS,
                                "25/5 · 45/10 · 50/10", APP_UI_COLOR_MUTED,
                                0U, false, 0U, 0U);
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
                                snapshot.focus_remaining_ms, true, 0U,
                                snapshot.focus_remaining_ms);
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

static void _clock_root_add_card(clock_root_state_t *state, const char *name,
                                 const char *page_id, clock_card_t card)
{
    lv_obj_t *row = lv_button_create(state->page.content);
    app_ui_click_only(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 68);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
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
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text, 2, 0);
    app_ui_make_passive(text, false);

    lv_obj_t *title = lv_label_create(text);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(title, app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(title, name);

    lv_obj_t *summary = lv_label_create(text);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(summary, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(summary, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(summary, "");
    state->card_summary[card] = summary;

    state->card_ring[card] = app_ui_ring_create(row, 44, 4,
                             APP_UI_COLOR_SURFACE_HI);
}

static void _clock_root_mount(const app_manager_page_context_t *context)
{
    clock_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "时钟", true);
    app_ui_page_set_subtitle(&state->page, "时间与计时");
    lv_obj_set_style_pad_bottom(state->page.content, 12, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *clock_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(clock_row);
    lv_obj_set_width(clock_row, LV_PCT(100));
    lv_obj_set_height(clock_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    app_ui_make_passive(clock_row, false);

    state->time_label = lv_label_create(clock_row);
    lv_obj_set_style_text_color(state->time_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->time_label,
                               app_ui_font(APP_THEME_FONT_TITLE), 0);
    lv_label_set_text(state->time_label, "--:--");

    state->seconds_label = lv_label_create(clock_row);
    lv_obj_set_style_text_color(state->seconds_label,
                                lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_text_font(state->seconds_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_obj_set_style_pad_top(state->seconds_label, 10, 0);
    lv_label_set_text(state->seconds_label, "");

    state->date_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->date_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->date_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->date_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->date_label, "");

    state->source_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->source_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->source_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(state->source_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->source_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->source_label, "");

    _clock_root_add_card(state, "倒计时", CLOCK_PAGE_COUNTDOWN,
                         CLOCK_CARD_COUNTDOWN);
    _clock_root_add_card(state, "秒表", CLOCK_PAGE_STOPWATCH,
                         CLOCK_CARD_STOPWATCH);
    _clock_root_add_card(state, "专注", CLOCK_PAGE_FOCUS, CLOCK_CARD_FOCUS);

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
    state->source_label = NULL;
    for (size_t index = 0U; index < CLOCK_CARD_COUNT; ++index)
    {
        state->card_summary[index] = NULL;
        state->card_ring[index] = NULL;
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
