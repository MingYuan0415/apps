#define DBG_TAG "recorder_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "recorder_app_internal.h"

void recorder_ui_set_visible(lv_obj_t *control, bool visible)
{
    if (visible)
    {
        lv_obj_remove_flag(control, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(control, LV_OBJ_FLAG_HIDDEN);
    }
}

void recorder_ui_format_mmss(uint32_t milliseconds, char *output,
                             size_t output_size)
{
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(milliseconds / 60000U),
                   (unsigned)((milliseconds / 1000U) % 60U));
}

const char *recorder_ui_display_name(const char *name)
{
    const char *base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    char *dot = strstr(base, ".wav");
    static char display[64];
    size_t keep = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (keep >= sizeof(display))
    {
        keep = sizeof(display) - 1U;
    }
    memcpy(display, base, keep);
    display[keep] = '\0';
    return display;
}

typedef struct recorder_root_state
{
    app_ui_page_t page;
    lv_obj_t *duration_label;
    lv_obj_t *status_label;
    lv_obj_t *record_button;
    lv_obj_t *record_indicator;
    lv_obj_t *secondary_row;
    lv_obj_t *btn_secondary;
    lv_obj_t *btn_stop;
    lv_obj_t *space_label;
    lv_obj_t *library_count_label;
    lv_timer_t *refresh_timer;
} recorder_root_state_t;

_Static_assert(sizeof(recorder_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder root state exceeds the lifecycle arena slot");

static void _recorder_root_set_indicator(recorder_root_state_t *state,
        uint32_t color, int32_t radius)
{
    lv_obj_set_style_bg_color(state->record_indicator, lv_color_hex(color),
                              0);
    lv_obj_set_style_radius(state->record_indicator, radius, 0);
}

static void _recorder_root_render(recorder_root_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    const bool valid = recorder_service_get_snapshot(&snapshot) == ESP_OK;
    char text[64];
    if (!valid)
    {
        lv_label_set_text(state->duration_label, "--:--");
        app_ui_set_status_text(state->status_label, "录音服务不可用",
                               APP_UI_STATUS_ERROR);
        lv_label_set_text(state->space_label, "可用空间 --");
        recorder_ui_set_visible(state->secondary_row, false);
        _recorder_root_set_indicator(state, APP_UI_COLOR_MUTED,
                                     LV_RADIUS_CIRCLE);
        return;
    }
    recorder_ui_format_mmss(snapshot.duration_ms, text, sizeof(text));
    lv_label_set_text(state->duration_label, text);
    (void)snprintf(text, sizeof(text), "可用空间 %llu MB",
                   (unsigned long long)(snapshot.free_bytes / (1024U * 1024U)));
    lv_label_set_text(state->space_label, text);

    const bool recording = snapshot.state == RECORDER_SERVICE_RECORDING;
    const bool paused = snapshot.state == RECORDER_SERVICE_PAUSED;
    recorder_ui_set_visible(state->secondary_row, recording || paused);
    if (recording || paused)
    {
        app_ui_button_set_text(state->btn_secondary,
                               recording ? "暂停" : "继续");
    }
    if (snapshot.operation_pending)
    {
        app_ui_set_status_text(state->status_label, "处理中",
                               APP_UI_STATUS_WARNING);
    }
    else if (recording)
    {
        app_ui_set_status_text(state->status_label, "录音中",
                               APP_UI_STATUS_ACCENT);
    }
    else if (paused)
    {
        app_ui_set_status_text(state->status_label, "已暂停",
                               APP_UI_STATUS_NEUTRAL);
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        app_ui_set_status_text(state->status_label, "播放中",
                               APP_UI_STATUS_ACCENT);
    }
    else if (snapshot.state == RECORDER_SERVICE_ERROR)
    {
        app_ui_set_status_text(state->status_label, "录音失败",
                               APP_UI_STATUS_ERROR);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "就绪",
                               APP_UI_STATUS_NEUTRAL);
    }
    if (recording)
    {
        _recorder_root_set_indicator(state, APP_UI_COLOR_WARNING, 6);
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        _recorder_root_set_indicator(state, APP_UI_COLOR_RAIN,
                                     LV_RADIUS_CIRCLE);
    }
    else
    {
        _recorder_root_set_indicator(state, APP_UI_COLOR_WARNING,
                                     LV_RADIUS_CIRCLE);
    }

    recorder_service_file_t files[RECORDER_SERVICE_MAX_FILES];
    size_t count = 0U;
    if (recorder_service_list(files, RECORDER_SERVICE_MAX_FILES,
                              &count) == ESP_OK)
    {
        (void)snprintf(text, sizeof(text), "%u 个录音", (unsigned)count);
        lv_label_set_text(state->library_count_label,
                          count == 0U ? "暂无录音" : text);
    }
}

static void _recorder_root_refresh(lv_timer_t *timer)
{
    _recorder_root_render(lv_timer_get_user_data(timer));
}

static void _recorder_primary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    switch (snapshot.state)
    {
    case RECORDER_SERVICE_RECORDING:
        (void)recorder_service_stop();
        break;
    case RECORDER_SERVICE_PAUSED:
        (void)recorder_service_resume();
        break;
    case RECORDER_SERVICE_PLAYING:
        (void)recorder_service_stop_playback();
        break;
    default:
        (void)recorder_service_start();
        break;
    }
    _recorder_root_render(lv_event_get_user_data(event));
}

static void _recorder_secondary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_RECORDING)
    {
        (void)recorder_service_pause();
    }
    else if (snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        (void)recorder_service_resume();
    }
    _recorder_root_render(lv_event_get_user_data(event));
}

static void _recorder_stop_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    (void)recorder_service_stop();
    _recorder_root_render(lv_event_get_user_data(event));
}

static void _recorder_open_files(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_RECORDER,
                                 RECORDER_PAGE_FILES);
    }
}

static void _recorder_root_mount(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "录音", true);
    app_ui_page_set_subtitle(&state->page, "语音备忘");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    state->duration_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->duration_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->duration_label, LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_style_text_color(state->duration_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->duration_label,
                               app_ui_font(APP_THEME_FONT_HUGE), 0);
    lv_label_set_text(state->duration_label, "--:--");

    state->status_label = app_ui_add_body_label(state->page.content, "就绪");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *record_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(record_row);
    lv_obj_set_width(record_row, LV_PCT(100));
    lv_obj_set_height(record_row, 96);
    lv_obj_set_flex_flow(record_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(record_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(record_row, false);

    state->record_button = lv_button_create(record_row);
    app_ui_click_only(state->record_button);
    lv_obj_set_size(state->record_button, 96, 96);
    lv_obj_set_style_radius(state->record_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->record_button,
                              lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(state->record_button,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(state->record_button, 0, 0);
    lv_obj_set_style_border_width(state->record_button, 2, 0);
    lv_obj_set_style_border_color(state->record_button,
                                  lv_color_hex(APP_UI_COLOR_SURFACE_HI), 0);
    lv_obj_add_event_cb(state->record_button, _recorder_primary_event,
                        LV_EVENT_CLICKED, state);
    state->record_indicator = lv_obj_create(state->record_button);
    lv_obj_remove_style_all(state->record_indicator);
    lv_obj_set_size(state->record_indicator, 30, 30);
    lv_obj_set_style_bg_color(state->record_indicator,
                              lv_color_hex(APP_UI_COLOR_WARNING), 0);
    lv_obj_set_style_bg_opa(state->record_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->record_indicator, LV_RADIUS_CIRCLE, 0);
    app_ui_make_passive(state->record_indicator, false);
    lv_obj_center(state->record_indicator);

    state->secondary_row = app_ui_button_row_create(state->page.content, 52);
    state->btn_secondary = app_ui_button_create(state->secondary_row, "暂停",
                           _recorder_secondary_event,
                           state);
    state->btn_stop = app_ui_button_create(state->secondary_row, "停止",
                                           _recorder_stop_event, state);
    recorder_ui_set_visible(state->secondary_row, false);

    state->space_label = app_ui_add_body_label(state->page.content,
                         "可用空间 --");
    lv_obj_set_width(state->space_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->space_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *entry = lv_button_create(state->page.content);
    app_ui_click_only(entry);
    lv_obj_set_width(entry, LV_PCT(100));
    lv_obj_set_height(entry, 56);
    lv_obj_set_style_radius(entry, 8, 0);
    lv_obj_set_style_bg_color(entry, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(entry, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(entry, 0, 0);
    lv_obj_set_style_pad_left(entry, 14, 0);
    lv_obj_set_style_pad_right(entry, 12, 0);
    lv_obj_set_style_pad_column(entry, 10, 0);
    lv_obj_set_flex_flow(entry, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(entry, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(entry, _recorder_open_files, LV_EVENT_CLICKED, NULL);
    lv_obj_t *entry_text = lv_obj_create(entry);
    lv_obj_remove_style_all(entry_text);
    lv_obj_set_width(entry_text, 0);
    lv_obj_set_flex_grow(entry_text, 1);
    lv_obj_set_height(entry_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(entry_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(entry_text, 2, 0);
    app_ui_make_passive(entry_text, false);
    lv_obj_t *entry_title = lv_label_create(entry_text);
    lv_obj_set_style_text_color(entry_title,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(entry_title,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(entry_title, "播放库");
    state->library_count_label = lv_label_create(entry_text);
    lv_obj_set_style_text_color(state->library_count_label,
                                lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(state->library_count_label,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(state->library_count_label, "暂无录音");
    lv_obj_t *chevron = lv_label_create(entry);
    lv_obj_set_style_text_color(chevron, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_font(chevron, LV_FONT_DEFAULT, 0);
    app_ui_make_passive(chevron, false);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);

    state->refresh_timer = lv_timer_create(_recorder_root_refresh, 250U,
                                           state);
    _recorder_root_render(state);
}

static esp_err_t _recorder_root_pause(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _recorder_root_resume(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _recorder_root_render(state);
}

static void _recorder_root_unmount(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->duration_label = NULL;
    state->status_label = NULL;
    state->record_button = NULL;
    state->record_indicator = NULL;
    state->secondary_row = NULL;
    state->btn_secondary = NULL;
    state->btn_stop = NULL;
    state->space_label = NULL;
    state->library_count_label = NULL;
}

static const app_manager_page_ops_t s_recorder_root_ops =
{
    .mount = _recorder_root_mount,
    .resume = _recorder_root_resume,
    .pause = _recorder_root_pause,
    .unmount = _recorder_root_unmount,
};

const app_manager_page_definition_t recorder_root_page_definition =
{
    .ops = &s_recorder_root_ops,
    .memory_size = sizeof(recorder_root_state_t),
};

static const app_manager_page_route_t s_recorder_routes[] =
{
    {
        .page_id = "root",
        .definition = &recorder_root_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = RECORDER_PAGE_FILES,
        .definition = &recorder_files_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(recorder, APP_IMAGE_RECORDER_ICON, "录音",
                            APP_MANAGER_ID_RECORDER, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_recorder_routes,
                            30U, "WAV 语音备忘");
