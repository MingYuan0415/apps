#define DBG_TAG "recorder_control_card"
#include "mt_log.h"

#include "recorder_app_internal.h"

#include <stdio.h>
#include <string.h>

#define RECORDER_CARD_PAD       16
#define RECORDER_CARD_HEADER_H  56
#define RECORDER_CARD_BUTTON_H  48
#define RECORDER_CARD_INDICATOR 12
#define RECORDER_CARD_BUSY_MS   1500U
#define RECORDER_CARD_NOTICE_MS 2500U

typedef struct recorder_control_card_state
{
    lv_obj_t *indicator;
    lv_obj_t *title;
    lv_obj_t *duration;
    lv_obj_t *detail;
    lv_obj_t *status;
    lv_obj_t *toggle;
    lv_obj_t *stop;
    uint32_t busy_started_ms;
    uint32_t notice_until_ms;
    bool operation_pending;
    bool unavailable;
} recorder_control_card_state_t;

static recorder_control_card_state_t s_recorder_card;
static bool s_recorder_card_mounted;
static bool s_recorder_card_last_active;

static void _recorder_card_open_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_RECORDER, "root");
    }
}

static void _recorder_card_set_actions_enabled(
    recorder_control_card_state_t *state, bool enabled)
{
    if (enabled)
    {
        lv_obj_remove_state(state->toggle, LV_STATE_DISABLED);
        lv_obj_remove_state(state->stop, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->toggle, LV_STATE_DISABLED);
        lv_obj_add_state(state->stop, LV_STATE_DISABLED);
    }
}

static void _recorder_card_render(recorder_control_card_state_t *state);

static void _recorder_card_show_error(recorder_control_card_state_t *state,
                                      const char *text)
{
    app_ui_set_status_text(state->status, text, APP_UI_STATUS_ERROR);
    lv_obj_set_hidden(state->status, false);
    state->notice_until_ms = lv_tick_get() + RECORDER_CARD_NOTICE_MS;
}

static void _recorder_card_set_unavailable(recorder_control_card_state_t *state)
{
    state->unavailable = true;
    state->operation_pending = false;
    app_ui_label_set_text_if(state->duration, "--:--");
    app_ui_label_set_text_if(state->detail, "存储卡未挂载");
    app_ui_set_status_text(state->status, "录音服务不可用",
                           APP_UI_STATUS_ERROR);
    lv_obj_set_hidden(state->status, false);
    lv_obj_set_style_bg_color(state->indicator,
                              lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_radius(state->indicator, LV_RADIUS_CIRCLE, 0);
    _recorder_card_set_actions_enabled(state, false);
}

static void _recorder_card_begin_command(recorder_control_card_state_t *state)
{
    state->operation_pending = true;
    state->busy_started_ms = lv_tick_get();
    app_ui_label_set_text_if(state->detail, "正在处理");
    lv_obj_set_hidden(state->status, true);
    _recorder_card_set_actions_enabled(state, false);
}

static void _recorder_card_finish_command(
    recorder_control_card_state_t *state, esp_err_t result)
{
    if (result != ESP_OK)
    {
        LOG_W("recorder command rejected: %s", esp_err_to_name(result));
        state->operation_pending = false;
        _recorder_card_show_error(
            state, recorder_ui_error_text(result));
        _recorder_card_render(state);
    }
}

static void _recorder_card_toggle_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_control_card_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->operation_pending || state->unavailable)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _recorder_card_set_unavailable(state);
        return;
    }
    if (snapshot.operation_pending)
    {
        return;
    }
    _recorder_card_begin_command(state);
    esp_err_t result;
    if (snapshot.state == RECORDER_SERVICE_RECORDING)
    {
        result = recorder_service_pause();
    }
    else if (snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        result = recorder_service_resume();
    }
    else
    {
        result = recorder_service_stop();
    }
    _recorder_card_finish_command(state, result);
}

static void _recorder_card_stop_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_control_card_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->operation_pending || state->unavailable)
    {
        return;
    }
    _recorder_card_begin_command(state);
    _recorder_card_finish_command(state, recorder_service_stop());
}

static void _recorder_card_render(recorder_control_card_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        _recorder_card_set_unavailable(state);
        return;
    }
    if (state->operation_pending &&
            (snapshot.operation_pending ||
             (uint32_t)(lv_tick_get() - state->busy_started_ms) <
             RECORDER_CARD_BUSY_MS))
    {
        state->operation_pending = true;
    }
    else
    {
        state->operation_pending = snapshot.operation_pending;
    }
    state->unavailable = false;
    if (state->notice_until_ms != 0U &&
            (int32_t)(lv_tick_get() - state->notice_until_ms) >= 0)
    {
        state->notice_until_ms = 0U;
        lv_obj_set_hidden(state->status, true);
    }
    const bool recording = snapshot.state == RECORDER_SERVICE_RECORDING;
    const bool paused = snapshot.state == RECORDER_SERVICE_PAUSED;
    char duration[16];
    recorder_ui_format_mmss(snapshot.duration_ms, duration, sizeof(duration));
    char detail[48];
    if (!snapshot.storage_ready)
    {
        (void)snprintf(detail, sizeof(detail), "存储卡未挂载");
    }
    else if (snapshot.operation_pending)
    {
        (void)snprintf(detail, sizeof(detail), "正在处理");
    }
    else if (recording)
    {
        (void)snprintf(detail, sizeof(detail), "录音中 · 轻触卡片打开");
    }
    else if (paused)
    {
        (void)snprintf(detail, sizeof(detail), "已暂停 · 轻触卡片打开");
    }
    else
    {
        (void)snprintf(detail, sizeof(detail), "就绪 · 轻触卡片打开");
    }
    app_ui_label_set_text_if(state->title, recording || paused ?
                             "语音备忘录" : "录音");
    app_ui_label_set_text_if(state->duration, duration);
    app_ui_label_set_text_if(state->detail, detail);
    lv_obj_set_style_text_color(
        state->detail,
        lv_color_hex(recording ? APP_UI_COLOR_RAIN :
                     paused ? APP_UI_COLOR_MUTED :
                     snapshot.operation_pending ?
                     APP_UI_COLOR_SUN : APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_bg_color(
        state->indicator,
        lv_color_hex(recording ? APP_UI_COLOR_WARNING :
                     paused ? APP_UI_COLOR_MUTED :
                     APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_radius(state->indicator,
                            recording ? 6 : LV_RADIUS_CIRCLE, 0);

    app_ui_button_set_text(state->toggle,
                           recording ? "暂停" : "继续");
    if (snapshot.operation_pending || !snapshot.storage_ready ||
            (!recording && !paused))
    {
        lv_obj_add_state(state->toggle, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->toggle, LV_STATE_DISABLED);
    }
    if (snapshot.operation_pending || (!recording && !paused))
    {
        lv_obj_add_state(state->stop, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->stop, LV_STATE_DISABLED);
    }
    state->operation_pending = snapshot.operation_pending;
}

static void _recorder_card_refresh(void *opaque)
{
    _recorder_card_render(opaque);
}

static lv_obj_t *_recorder_card_label(lv_obj_t *parent, const char *text,
                                      app_theme_font_id_t font,
                                      uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, lv_font_get_line_height(app_ui_font(font)));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static esp_err_t _recorder_card_mount(lv_obj_t *parent, void **out_state)
{
    if (parent == NULL || out_state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_recorder_card, 0, sizeof(s_recorder_card));
    *out_state = &s_recorder_card;

    lv_obj_t *card = lv_obj_create(parent);
    if (card == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, RECORDER_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(card, false);

    lv_obj_t *header = lv_button_create(card);
    if (header == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    app_ui_click_only(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, RECORDER_CARD_HEADER_H);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, APP_UI_RADIUS_CONTROL, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, APP_UI_GAP, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(header, _recorder_card_open_event,
                        LV_EVENT_CLICKED, NULL);

    s_recorder_card.indicator = lv_obj_create(header);
    if (s_recorder_card.indicator == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(s_recorder_card.indicator);
    lv_obj_set_size(s_recorder_card.indicator, RECORDER_CARD_INDICATOR,
                    RECORDER_CARD_INDICATOR);
    lv_obj_set_style_bg_opa(s_recorder_card.indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_recorder_card.indicator,
                              lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_radius(s_recorder_card.indicator, LV_RADIUS_CIRCLE, 0);
    app_ui_make_passive(s_recorder_card.indicator, false);

    lv_obj_t *identity = lv_obj_create(header);
    if (identity == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(identity);
    lv_obj_set_width(identity, 0);
    lv_obj_set_flex_grow(identity, 1);
    lv_obj_set_height(identity, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(identity, 2, 0);
    app_ui_make_passive(identity, false);
    s_recorder_card.title = _recorder_card_label(identity, "录音",
        APP_THEME_FONT_BODY, APP_UI_COLOR_TEXT);
    s_recorder_card.duration = _recorder_card_label(identity, "0:00",
        APP_THEME_FONT_HEAD, APP_UI_COLOR_TEXT);
    lv_obj_t *open = lv_label_create(header);
    if (open != NULL)
    {
        lv_obj_set_style_text_font(open, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(open, lv_color_hex(APP_UI_COLOR_MUTED), 0);
        lv_label_set_text(open, LV_SYMBOL_RIGHT);
        app_ui_make_passive(open, false);
    }

    s_recorder_card.detail = _recorder_card_label(card, "就绪",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED);
    s_recorder_card.status = _recorder_card_label(card, "",
        APP_THEME_FONT_SMALL, APP_UI_COLOR_WARNING);
    if (s_recorder_card.status == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_hidden(s_recorder_card.status, true);
    lv_obj_t *actions = app_ui_button_row_create(card, RECORDER_CARD_BUTTON_H);
    if (actions == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    s_recorder_card.toggle = app_ui_button_create(actions, "暂停",
        _recorder_card_toggle_event, &s_recorder_card);
    lv_obj_t *stop = app_ui_button_create(actions, "停止",
                                          _recorder_card_stop_event,
                                          &s_recorder_card);
    if (stop != NULL)
    {
        s_recorder_card.stop = stop;
        app_ui_button_set_kind(stop, APP_UI_BUTTON_DANGER);
    }
    if (s_recorder_card.toggle == NULL || stop == NULL ||
            s_recorder_card.title == NULL ||
            s_recorder_card.duration == NULL ||
            s_recorder_card.detail == NULL ||
            s_recorder_card.status == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    _recorder_card_render(&s_recorder_card);
    s_recorder_card_mounted = true;
    return ESP_OK;
}

static void _recorder_card_unmount(void *opaque)
{
    (void)opaque;
    memset(&s_recorder_card, 0, sizeof(s_recorder_card));
    s_recorder_card_mounted = false;
    s_recorder_card_last_active = false;
}

static bool _recorder_card_is_active(void)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return s_recorder_card_mounted && s_recorder_card_last_active;
    }
    const bool active = snapshot.state == RECORDER_SERVICE_RECORDING ||
                        snapshot.state == RECORDER_SERVICE_PAUSED;
    s_recorder_card_last_active = active;
    return active;
}

const app_manager_control_center_card_ops_t recorder_control_center_card_ops =
{
    .is_active = _recorder_card_is_active,
    .mount = _recorder_card_mount,
    .refresh = _recorder_card_refresh,
    .unmount = _recorder_card_unmount,
};
