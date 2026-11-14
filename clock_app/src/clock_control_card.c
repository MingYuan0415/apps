#define DBG_TAG "clock_control_card"
#include "mt_log.h"

#include "clock_app_internal.h"
#include "clock_ui_format.h"

#include <stdio.h>
#include <string.h>

#define CLOCK_CARD_PAD          8
#define CLOCK_CARD_RING         48
#define CLOCK_CARD_RING_W       5
#define CLOCK_CARD_ROW_HEIGHT   88
#define CLOCK_CARD_TOGGLE_W     64
#define CLOCK_CARD_ROW_COUNT    3U

typedef enum clock_control_kind
{
    CLOCK_CONTROL_COUNTDOWN = 0,
    CLOCK_CONTROL_STOPWATCH,
    CLOCK_CONTROL_FOCUS,
} clock_control_kind_t;

typedef struct clock_control_row
{
    clock_control_kind_t kind;
    lv_obj_t *root;
    lv_obj_t *ring;
    lv_obj_t *open;
    lv_obj_t *title;
    lv_obj_t *time;
    lv_obj_t *detail;
    lv_obj_t *toggle;
} clock_control_row_t;

typedef struct clock_control_card_state
{
    lv_obj_t *card;
    lv_obj_t *status;
    clock_control_row_t rows[CLOCK_CARD_ROW_COUNT];
} clock_control_card_state_t;

static const clock_control_kind_t s_clock_card_kinds[CLOCK_CARD_ROW_COUNT] =
{
    CLOCK_CONTROL_COUNTDOWN,
    CLOCK_CONTROL_STOPWATCH,
    CLOCK_CONTROL_FOCUS,
};

static clock_control_card_state_t s_clock_card;
static bool s_clock_card_mounted;
static bool s_clock_card_last_active;

static void _clock_card_render(clock_control_card_state_t *state);

static const char *_clock_card_page(clock_control_kind_t kind)
{
    switch (kind)
    {
    case CLOCK_CONTROL_STOPWATCH:
        return CLOCK_PAGE_STOPWATCH;
    case CLOCK_CONTROL_FOCUS:
        return CLOCK_PAGE_FOCUS;
    case CLOCK_CONTROL_COUNTDOWN:
    default:
        return CLOCK_PAGE_COUNTDOWN;
    }
}

static timer_service_state_t _clock_card_state(
    const timer_service_snapshot_t *snapshot, clock_control_kind_t kind)
{
    switch (kind)
    {
    case CLOCK_CONTROL_COUNTDOWN:
        return snapshot->countdown_state;
    case CLOCK_CONTROL_STOPWATCH:
        return snapshot->stopwatch_state;
    case CLOCK_CONTROL_FOCUS:
    default:
        return snapshot->focus_state;
    }
}

static const char *_clock_card_state_text(timer_service_state_t state)
{
    switch (state)
    {
    case TIMER_SERVICE_RUNNING:
        return "进行中";
    case TIMER_SERVICE_PAUSED:
        return "已暂停";
    case TIMER_SERVICE_COMPLETED:
        return "已完成";
    default:
        return "未开始";
    }
}

static void _clock_card_show_status(clock_control_card_state_t *state,
                                    const char *text)
{
    if (state->status == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(state->status, text);
    lv_obj_set_hidden(state->status, false);
}

static void _clock_card_hide_status(clock_control_card_state_t *state)
{
    if (state->status != NULL)
    {
        lv_obj_set_hidden(state->status, true);
    }
}

static void _clock_card_set_unavailable(clock_control_card_state_t *state)
{
    for (size_t index = 0U; index < CLOCK_CARD_ROW_COUNT; ++index)
    {
        lv_obj_set_hidden(state->rows[index].root, true);
    }
    _clock_card_show_status(state, "计时服务不可用");
}

static void _clock_card_open_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    const clock_control_row_t *row =
        (const clock_control_row_t *)lv_event_get_user_data(event);
    if (row != NULL)
    {
        app_ui_request_open_page(APP_MANAGER_ID_CLOCK,
                                 _clock_card_page(row->kind));
    }
}

static void _clock_card_toggle_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    clock_control_row_t *row =
        (clock_control_row_t *)lv_event_get_user_data(event);
    if (row == NULL)
    {
        return;
    }
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _clock_card_set_unavailable(&s_clock_card);
        return;
    }
    const timer_service_state_t state = _clock_card_state(&snapshot, row->kind);
    const bool running = state == TIMER_SERVICE_RUNNING;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    switch (row->kind)
    {
    case CLOCK_CONTROL_COUNTDOWN:
        result = running ? timer_service_countdown_pause() :
                 timer_service_countdown_resume();
        break;
    case CLOCK_CONTROL_STOPWATCH:
        result = running ? timer_service_stopwatch_pause() :
                 timer_service_stopwatch_start();
        break;
    case CLOCK_CONTROL_FOCUS:
        result = running ? timer_service_focus_pause() :
                 timer_service_focus_resume();
        break;
    }
    if (result != ESP_OK)
    {
        LOG_W("timer command rejected: %s", esp_err_to_name(result));
        _clock_card_show_status(&s_clock_card, "操作失败");
        return;
    }
    _clock_card_render(&s_clock_card);
}

static void _clock_card_render_row(clock_control_row_t *row,
                                   const timer_service_snapshot_t *snapshot)
{
    const timer_service_state_t state = _clock_card_state(snapshot, row->kind);
    if (state == TIMER_SERVICE_IDLE)
    {
        lv_obj_set_hidden(row->root, true);
        return;
    }
    lv_obj_set_hidden(row->root, false);

    const char *title = "计时";
    uint32_t remaining_ms = 0U;
    uint32_t total_ms = 0U;
    uint32_t color = APP_UI_COLOR_RAIN;
    char time_text[24] = {0};
    char detail[48] = {0};

    switch (row->kind)
    {
    case CLOCK_CONTROL_COUNTDOWN:
        title = "倒计时";
        remaining_ms = snapshot->countdown_remaining_ms;
        total_ms = snapshot->countdown_duration_ms;
        clock_ui_format_mmss(remaining_ms, time_text, sizeof(time_text));
        (void)snprintf(detail, sizeof(detail), "%s",
                       _clock_card_state_text(state));
        break;
    case CLOCK_CONTROL_STOPWATCH:
        title = "秒表";
        remaining_ms = (uint32_t)(snapshot->stopwatch_elapsed_ms % 60000ULL);
        total_ms = 60000U;
        color = APP_UI_COLOR_SUCCESS;
        clock_ui_format_elapsed(snapshot->stopwatch_elapsed_ms, time_text,
                                sizeof(time_text));
        (void)snprintf(detail, sizeof(detail), "%s",
                       _clock_card_state_text(state));
        break;
    case CLOCK_CONTROL_FOCUS:
        title = snapshot->focus_phase == TIMER_SERVICE_FOCUS_WORK ?
                "专注" : "休息";
        remaining_ms = snapshot->focus_remaining_ms;
        total_ms = snapshot->focus_phase == TIMER_SERVICE_FOCUS_WORK ?
                   snapshot->focus_work_ms : snapshot->focus_break_ms;
        color = APP_UI_COLOR_SUN;
        clock_ui_format_mmss(remaining_ms, time_text, sizeof(time_text));
        (void)snprintf(detail, sizeof(detail), "%s · 第 %u 轮",
                       _clock_card_state_text(state),
                       (unsigned)(snapshot->focus_completed_cycles + 1U));
        break;
    }

    app_ui_label_set_text_if(row->title, title);
    app_ui_label_set_text_if(row->time, time_text);
    app_ui_label_set_text_if(row->detail, detail);
    lv_obj_set_style_text_color(
        row->detail,
        lv_color_hex(state == TIMER_SERVICE_RUNNING ? color :
                     state == TIMER_SERVICE_COMPLETED ?
                     APP_UI_COLOR_SUN : APP_UI_COLOR_MUTED), 0);
    clock_ui_ring_update(row->ring,
                         state == TIMER_SERVICE_RUNNING ||
                         state == TIMER_SERVICE_PAUSED,
                         remaining_ms, total_ms, color);
    app_ui_button_set_text(
        row->toggle,
        state == TIMER_SERVICE_RUNNING ? "暂停" :
        state == TIMER_SERVICE_COMPLETED ? "已完成" : "继续");
    if (state == TIMER_SERVICE_RUNNING || state == TIMER_SERVICE_PAUSED)
    {
        lv_obj_remove_state(row->toggle, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(row->toggle, LV_STATE_DISABLED);
    }
}

static void _clock_card_render(clock_control_card_state_t *state)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _clock_card_set_unavailable(state);
        return;
    }
    for (size_t index = 0U; index < CLOCK_CARD_ROW_COUNT; ++index)
    {
        _clock_card_render_row(&state->rows[index], &snapshot);
    }
    _clock_card_hide_status(state);
}

static void _clock_card_refresh(void *opaque)
{
    _clock_card_render(opaque);
}

static lv_obj_t *_clock_card_row_create(lv_obj_t *parent,
                                        clock_control_row_t *row,
                                        bool first)
{
    row->root = lv_obj_create(parent);
    if (row->root == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(row->root);
    lv_obj_set_width(row->root, LV_PCT(100));
    lv_obj_set_height(row->root, CLOCK_CARD_ROW_HEIGHT);
    lv_obj_set_style_bg_opa(row->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row->root, first ? 0 : 1, 0);
    lv_obj_set_style_border_side(row->root, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(row->root,
                                  lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_set_style_pad_left(row->root, 8, 0);
    lv_obj_set_style_pad_right(row->root, 0, 0);
    lv_obj_set_style_pad_column(row->root, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(row->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row->root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(row->root, false);

    row->ring = app_ui_ring_create(row->root, CLOCK_CARD_RING,
                                   CLOCK_CARD_RING_W, APP_UI_COLOR_TRACK);
    if (row->ring == NULL)
    {
        return NULL;
    }

    row->open = lv_button_create(row->root);
    if (row->open == NULL)
    {
        return NULL;
    }
    app_ui_click_only(row->open);
    lv_obj_set_width(row->open, 0);
    lv_obj_set_flex_grow(row->open, 1);
    lv_obj_set_height(row->open, LV_PCT(100));
    lv_obj_set_style_bg_opa(row->open, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row->open, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row->open,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row->open, 0, 0);
    lv_obj_set_style_radius(row->open, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_pad_left(row->open, 4, 0);
    lv_obj_set_style_pad_right(row->open, 4, 0);
    lv_obj_set_style_pad_column(row->open, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(row->open, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row->open, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row->open, _clock_card_open_event,
                        LV_EVENT_CLICKED, row);

    lv_obj_t *text = lv_obj_create(row->open);
    if (text == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(text);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(text, 0, 0);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    app_ui_make_passive(text, false);

    row->title = clock_ui_label_create(text, "计时", APP_THEME_FONT_SMALL,
                                       APP_UI_COLOR_TEXT);
    row->time = clock_ui_label_create(text, "0:00", APP_THEME_FONT_BODY,
                                      APP_UI_COLOR_TEXT);
    row->detail = clock_ui_label_create(text, "未开始",
                                        APP_THEME_FONT_SMALL,
                                        APP_UI_COLOR_MUTED);
    if (row->title == NULL || row->time == NULL || row->detail == NULL)
    {
        return NULL;
    }
    lv_obj_set_style_text_align(row->title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(row->time, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(row->detail, LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t *arrow = lv_label_create(row->open);
    if (arrow != NULL)
    {
        lv_obj_set_style_text_font(arrow, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(arrow,
                                    lv_color_hex(APP_UI_COLOR_FAINT), 0);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        app_ui_make_passive(arrow, false);
    }

    lv_obj_t *toggle_row = app_ui_button_row_create(row->root,
        CLOCK_CARD_TOGGLE_W);
    if (toggle_row == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(toggle_row, CLOCK_CARD_TOGGLE_W);
    row->toggle = app_ui_button_create(toggle_row, "继续",
                                       _clock_card_toggle_event, row);
    if (row->toggle == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(row->toggle, CLOCK_CARD_TOGGLE_W);
    return row->root;
}

static esp_err_t _clock_card_mount(lv_obj_t *parent, void **out_state)
{
    if (parent == NULL || out_state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_clock_card, 0, sizeof(s_clock_card));
    *out_state = &s_clock_card;

    s_clock_card.card = clock_ui_card_create(parent);
    if (s_clock_card.card == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_pad_all(s_clock_card.card, CLOCK_CARD_PAD, 0);
    lv_obj_set_style_pad_row(s_clock_card.card, 0, 0);

    s_clock_card.status = clock_ui_label_create(
                              s_clock_card.card, "计时服务不可用", APP_THEME_FONT_SMALL,
                              APP_UI_COLOR_WARNING);
    if (s_clock_card.status == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_text_align(s_clock_card.status, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_hidden(s_clock_card.status, true);

    for (size_t index = 0U; index < CLOCK_CARD_ROW_COUNT; ++index)
    {
        clock_control_row_t *row = &s_clock_card.rows[index];
        row->kind = s_clock_card_kinds[index];
        if (_clock_card_row_create(s_clock_card.card, row, index == 0U) == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    _clock_card_render(&s_clock_card);
    s_clock_card_mounted = true;
    return ESP_OK;
}

static void _clock_card_unmount(void *opaque)
{
    (void)opaque;
    memset(&s_clock_card, 0, sizeof(s_clock_card));
    s_clock_card_mounted = false;
    s_clock_card_last_active = false;
}

static bool _clock_card_is_active(void)
{
    timer_service_snapshot_t snapshot;
    if (timer_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return s_clock_card_mounted && s_clock_card_last_active;
    }
    const bool active = snapshot.countdown_state != TIMER_SERVICE_IDLE ||
                        snapshot.stopwatch_state != TIMER_SERVICE_IDLE ||
                        snapshot.focus_state != TIMER_SERVICE_IDLE;
    s_clock_card_last_active = active;
    return active;
}

const app_manager_control_center_card_ops_t clock_control_center_card_ops =
{
    .is_active = _clock_card_is_active,
    .mount = _clock_card_mount,
    .refresh = _clock_card_refresh,
    .unmount = _clock_card_unmount,
};
