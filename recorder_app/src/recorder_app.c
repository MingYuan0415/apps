#define DBG_TAG "recorder_app"
#include "mt_log.h"

#include "recorder_app_internal.h"

/* ---------------------------------------------------------------------------
 * Root page: one fixed control stack, no hide/show layout jumps.
 * ------------------------------------------------------------------------- */

/* The receipt lives for a fixed number of 250 ms polls (~5 s) instead of a
 * wall-clock deadline, so it is deterministic and needs no tick source. */
#define RECORDER_ROOT_RECEIPT_POLLS 20U
#define RECORDER_ROOT_SIDE_SLOT 84

typedef struct recorder_root_state
{
    app_ui_page_t page;
    lv_obj_t *duration_label;
    lv_obj_t *status_label;
    lv_obj_t *record_button;
    lv_obj_t *record_indicator;
    lv_obj_t *primary_caption;
    lv_obj_t *secondary_row;
    lv_obj_t *btn_secondary;
    lv_obj_t *meta_label;
    lv_obj_t *library_count_label;
    lv_timer_t *refresh_timer;
    uint32_t last_state;
    esp_err_t local_error; /**< Rejected command the snapshot cannot report. */
    uint32_t last_duration_ms;
    uint32_t receipt_polls;
    bool saved_receipt;
    bool limit_reached;
} recorder_root_state_t;

_Static_assert(sizeof(recorder_root_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder root state exceeds the lifecycle arena slot");

static void _root_set_indicator(recorder_root_state_t *state, uint32_t color,
                                int32_t radius)
{
    lv_obj_set_style_bg_color(state->record_indicator, lv_color_hex(color), 0);
    lv_obj_set_style_radius(state->record_indicator, radius, 0);
}

static void _root_set_caption(recorder_root_state_t *state, const char *text)
{
    if (state->primary_caption != NULL)
    {
        app_ui_label_set_text_if(state->primary_caption, text);
    }
}

static void _root_render(recorder_root_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    char text[96];
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        app_ui_label_set_text_if(state->duration_label, "--:--");
        app_ui_set_status_text(state->status_label, "录音服务不可用",
                               APP_UI_STATUS_ERROR);
        app_ui_label_set_text_if(state->meta_label, "存储卡未挂载");
        _root_set_caption(state, "开始");
        recorder_ui_set_visible(state->btn_secondary, false);
        lv_obj_add_state(state->record_button, LV_STATE_DISABLED);
        _root_set_indicator(state, APP_UI_COLOR_MUTED, LV_RADIUS_CIRCLE);
        return;
    }

    const bool recording = snapshot.state == RECORDER_SERVICE_RECORDING;
    const bool paused = snapshot.state == RECORDER_SERVICE_PAUSED;
    const bool playing = snapshot.state == RECORDER_SERVICE_PLAYING;
    const bool active = recording || paused;

    /* A finished recording turns into a short receipt instead of vanishing:
     * "已保存 0:07" answers the only question the user has after stopping. */
    if (state->last_state != (uint32_t)snapshot.state &&
            !active && !playing &&
            (state->last_state == (uint32_t)RECORDER_SERVICE_RECORDING ||
             state->last_state == (uint32_t)RECORDER_SERVICE_PAUSED) &&
            snapshot.last_error == ESP_OK && state->last_duration_ms > 0U)
    {
        state->saved_receipt = true;
        state->receipt_polls = RECORDER_ROOT_RECEIPT_POLLS;
        state->limit_reached = snapshot.max_duration_ms > 0U &&
                               state->last_duration_ms + 1000U >=
                               snapshot.max_duration_ms;
    }
    if (state->saved_receipt && state->receipt_polls == 0U)
    {
        state->saved_receipt = false;
        state->limit_reached = false;
    }
    if (state->saved_receipt && state->receipt_polls > 0U)
    {
        --state->receipt_polls;
    }
    state->last_state = (uint32_t)snapshot.state;
    if (active || snapshot.duration_ms > 0U)
    {
        state->last_duration_ms = snapshot.duration_ms;
    }

    /* lv_label_set_text compares the pointer, not the contents, so passing a
     * stack buffer reallocated and invalidated the HUGE label on every poll. */
    recorder_ui_format_mmss(snapshot.duration_ms, text, sizeof(text));
    app_ui_label_set_text_if(state->duration_label, text);

    /* One meta line carries capacity, the configured limit and the library
     * size, so no state needs an extra row. */
    const uint32_t library_count = (uint32_t)snapshot.file_count;
    if (!snapshot.storage_ready)
    {
        app_ui_label_set_text_if(state->meta_label, "存储卡未挂载");
    }
    else if (recording)
    {
        /* The countdown to the configured limit belongs next to the limit it
         * counts down to, not in the status line. */
        const uint32_t remaining_ms = snapshot.max_duration_ms >
                                      snapshot.duration_ms ?
                                      snapshot.max_duration_ms - snapshot.duration_ms :
                                      0U;
        char remaining[16];
        recorder_ui_format_mmss(remaining_ms, remaining, sizeof(remaining));
        (void)snprintf(text, sizeof(text), "剩余 %s · 上限 %u:%02u",
                       remaining, (unsigned)(snapshot.max_duration_ms / 60000U),
                       (unsigned)((snapshot.max_duration_ms / 1000U) % 60U));
        app_ui_label_set_text_if(state->meta_label, text);
    }
    else if (!snapshot.space_valid)
    {
        (void)snprintf(text, sizeof(text), "空间未知 · 上限 %u:%02u",
                       (unsigned)(snapshot.max_duration_ms / 60000U),
                       (unsigned)((snapshot.max_duration_ms / 1000U) % 60U));
        app_ui_label_set_text_if(state->meta_label, text);
    }
    else
    {
        (void)snprintf(text, sizeof(text), "可用 %llu MB · 上限 %u:%02u",
                       (unsigned long long)(snapshot.free_bytes /
                                            (1024ULL * 1024ULL)),
                       (unsigned)(snapshot.max_duration_ms / 60000U),
                       (unsigned)((snapshot.max_duration_ms / 1000U) % 60U));
        app_ui_label_set_text_if(state->meta_label, text);
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
    else if (playing)
    {
        app_ui_set_status_text(state->status_label, "播放中",
                               APP_UI_STATUS_ACCENT);
    }
    else if (state->saved_receipt)
    {
        char saved[32];
        recorder_ui_format_mmss(state->last_duration_ms, saved, sizeof(saved));
        /* An automatic stop at the configured limit used to look exactly like
         * a manual one; the receipt now says which of the two happened. */
        (void)snprintf(text, sizeof(text), state->limit_reached ?
                       "已保存 %s · 达到上限" : "已保存 %s", saved);
        app_ui_set_status_text(state->status_label, text,
                               state->limit_reached ? APP_UI_STATUS_WARNING :
                               APP_UI_STATUS_SUCCESS);
    }
    else if (!snapshot.storage_ready)
    {
        /* A missing card outlives any single rejected command. */
        app_ui_set_status_text(state->status_label, "插入存储卡后可以录音",
                               APP_UI_STATUS_WARNING);
    }
    else if (state->local_error != ESP_OK)
    {
        /* A rejected start has no snapshot state of its own; keep the reason
         * visible until the next accepted command. */
        app_ui_set_status_text(state->status_label,
                               recorder_ui_error_text(state->local_error),
                               APP_UI_STATUS_ERROR);
    }
    else if (snapshot.last_error != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               recorder_ui_error_text(snapshot.last_error),
                               APP_UI_STATUS_ERROR);
    }
    else
    {
        app_ui_set_status_text(state->status_label, "就绪",
                               APP_UI_STATUS_NEUTRAL);
    }

    /* The circle is only ever 开始/停止 — including while the library plays,
     * because its click stops playback. Pause lives in its own reserved slot
     * so a paused recording still has an obvious way to end. */
    _root_set_caption(state, (active || playing) ? "停止" : "开始");
    /* The slot occupies its width at every state; only the button inside it
     * appears, so neither the circle nor anything below it moves. */
    recorder_ui_set_visible(state->btn_secondary, active);
    app_ui_button_set_text(state->btn_secondary, recording ? "暂停" : "继续");
    if (state->btn_secondary != NULL)
    {
        if (snapshot.operation_pending)
        {
            lv_obj_add_state(state->btn_secondary, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_remove_state(state->btn_secondary, LV_STATE_DISABLED);
        }
    }
    if (active)
    {
        _root_set_indicator(state, recording ? APP_UI_COLOR_WARNING :
                            APP_UI_COLOR_MUTED,
                            recording ? 6 : LV_RADIUS_CIRCLE);
    }
    else if (playing)
    {
        _root_set_indicator(state, APP_UI_COLOR_RAIN, LV_RADIUS_CIRCLE);
    }
    else
    {
        _root_set_indicator(state, APP_UI_COLOR_WARNING, LV_RADIUS_CIRCLE);
    }
    /* ERROR is a finished operation that failed, not a broken service: the
     * next tap must be able to retry, so it never disables the circle. */
    if (snapshot.operation_pending || !snapshot.storage_ready)
    {
        lv_obj_add_state(state->record_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->record_button, LV_STATE_DISABLED);
    }

    /* Only the counters are read here, so a stale index is refreshed explicitly
     * instead of copying every record per poll. A published index can also be
     * stale without being invalid: the card changed while the Page was open
     * (an app wrote or deleted a recording), and without this request the count
     * would keep describing the previous medium forever. */
    const bool index_current = snapshot.index_valid &&
                               snapshot.index_generation == snapshot.storage_generation &&
                               snapshot.index_revision == snapshot.content_revision;
    if (!index_current)
    {
        (void)recorder_service_refresh();
    }
    if (snapshot.index_valid)
    {
        if (library_count == 0U)
        {
            app_ui_label_set_text_if(state->library_count_label, "暂无录音");
        }
        else
        {
            (void)snprintf(text, sizeof(text), "%u 个录音",
                           (unsigned)library_count);
            app_ui_label_set_text_if(state->library_count_label, text);
        }
    }
    else if (!snapshot.storage_ready)
    {
        app_ui_label_set_text_if(state->library_count_label, "存储卡未挂载");
    }
    else
    {
        app_ui_label_set_text_if(state->library_count_label, "正在读取");
    }
}

static void _root_refresh(lv_timer_t *timer)
{
    _root_render(lv_timer_get_user_data(timer));
}

static void _root_primary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_root_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_RECORDING ||
            snapshot.state == RECORDER_SERVICE_PAUSED)
    {
        state->local_error = recorder_service_stop();
    }
    else if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        state->local_error = recorder_service_stop_playback();
    }
    else
    {
        /* Starting is the one command whose failure used to be invisible. */
        state->local_error = recorder_service_start();
    }
    _root_render(state);
}

static void _root_secondary_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_root_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    const esp_err_t result = snapshot.state == RECORDER_SERVICE_RECORDING ?
                             recorder_service_pause() :
                             snapshot.state == RECORDER_SERVICE_PAUSED ?
                             recorder_service_resume() : ESP_ERR_INVALID_STATE;
    state->local_error = result;
    _root_render(state);
}

static void _root_open_files(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        app_ui_request_open_page(APP_MANAGER_ID_RECORDER, RECORDER_PAGE_FILES);
    }
}

static void _root_mount(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->last_state = UINT32_MAX;
    app_ui_page_create(&state->page, "录音", true);
    /* Every slot below has a fixed height, so the stack never moves between
     * idle, recording and paused. Content still scrolls as a last resort on a
     * shorter panel rather than clipping the library entry. */
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_VER);

    state->duration_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->duration_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->duration_label, LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_style_text_color(state->duration_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->duration_label,
                               app_ui_font(APP_THEME_FONT_HUGE), 0);
    lv_label_set_text(state->duration_label, "0:00");

    state->status_label = app_ui_add_body_label(state->page.content, "就绪");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->status_label, LV_TEXT_ALIGN_CENTER, 0);

    /* Circle and pause share one fixed row. Both side slots are always laid
     * out — one empty, one holding the pause control — so the circle stays
     * centred, pause never costs a separate band, and nothing reflows. */
    lv_obj_t *record_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(record_row);
    lv_obj_set_width(record_row, LV_PCT(100));
    lv_obj_set_height(record_row, 116);
    lv_obj_set_flex_flow(record_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(record_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(record_row, APP_UI_GAP, 0);
    app_ui_make_passive(record_row, false);

    lv_obj_t *leading = lv_obj_create(record_row);
    lv_obj_remove_style_all(leading);
    lv_obj_set_size(leading, RECORDER_ROOT_SIDE_SLOT, APP_UI_BUTTON_HEIGHT);
    app_ui_make_passive(leading, false);

    state->record_button = lv_button_create(record_row);
    app_ui_click_only(state->record_button);
    lv_obj_set_size(state->record_button, 104, 104);
    lv_obj_set_style_radius(state->record_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->record_button,
                              lv_color_hex(APP_UI_COLOR_WARNING_TINT), 0);
    lv_obj_set_style_bg_color(state->record_button,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(state->record_button, 0, 0);
    lv_obj_set_style_border_width(state->record_button, 3, 0);
    lv_obj_set_style_border_color(state->record_button,
                                  lv_color_hex(APP_UI_COLOR_TRACK), 0);
    lv_obj_add_event_cb(state->record_button, _root_primary_event,
                        LV_EVENT_CLICKED, state);
    state->record_indicator = lv_obj_create(state->record_button);
    lv_obj_remove_style_all(state->record_indicator);
    lv_obj_set_size(state->record_indicator, 40, 40);
    lv_obj_set_style_bg_color(state->record_indicator,
                              lv_color_hex(APP_UI_COLOR_WARNING), 0);
    lv_obj_set_style_bg_opa(state->record_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->record_indicator, LV_RADIUS_CIRCLE, 0);
    app_ui_make_passive(state->record_indicator, false);
    lv_obj_align(state->record_indicator, LV_ALIGN_CENTER, 0, -12);

    /* The caption inside the same circle states the current action; it is the
     * only text that changes between 开始 and 停止. */
    state->primary_caption = lv_label_create(state->record_button);
    lv_obj_set_style_text_color(state->primary_caption,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->primary_caption,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_label_set_text(state->primary_caption, "开始");
    lv_obj_align(state->primary_caption, LV_ALIGN_CENTER, 0, 28);
    app_ui_make_passive(state->primary_caption, false);

    state->secondary_row = lv_obj_create(record_row);
    lv_obj_remove_style_all(state->secondary_row);
    lv_obj_set_size(state->secondary_row, RECORDER_ROOT_SIDE_SLOT,
                    APP_UI_BUTTON_HEIGHT);
    lv_obj_set_flex_flow(state->secondary_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state->secondary_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(state->secondary_row, false);
    state->btn_secondary = app_ui_button_create(state->secondary_row, "暂停",
        _root_secondary_event, state);
    app_ui_button_set_kind(state->btn_secondary, APP_UI_BUTTON_SECONDARY);
    recorder_ui_set_visible(state->btn_secondary, false);

    state->meta_label = app_ui_add_body_label(state->page.content, "--");
    lv_obj_set_width(state->meta_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->meta_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->meta_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->meta_label,
                                lv_color_hex(APP_UI_COLOR_FAINT), 0);

    lv_obj_t *library = app_ui_group_create(state->page.content);
    (void)app_ui_group_add_entry(library, "播放库",
                                 &state->library_count_label,
                                 _root_open_files, NULL);
    lv_label_set_text(state->library_count_label, "正在读取");

    state->refresh_timer = lv_timer_create(_root_refresh, 250U, state);
    _root_render(state);
}

static esp_err_t _root_pause(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _root_resume(const app_manager_page_context_t *context)
{
    recorder_root_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _root_render(state);
}

static void _root_unmount(const app_manager_page_context_t *context)
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
    state->primary_caption = NULL;
    state->secondary_row = NULL;
    state->btn_secondary = NULL;
    state->meta_label = NULL;
    state->library_count_label = NULL;
}

static const app_manager_page_ops_t s_recorder_root_ops =
{
    .mount = _root_mount,
    .resume = _root_resume,
    .pause = _root_pause,
    .unmount = _root_unmount,
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
    {
        .page_id = RECORDER_PAGE_DETAIL,
        .definition = &recorder_detail_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT_META(recorder, APP_IMAGE_RECORDER_ICON, "录音",
                            APP_MANAGER_ID_RECORDER, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_recorder_routes,
                            30U, "WAV 语音备忘");
