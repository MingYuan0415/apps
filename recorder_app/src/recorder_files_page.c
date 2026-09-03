#define DBG_TAG "recorder_files"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "recorder_app_internal.h"

typedef struct recorder_files_state
{
    app_ui_page_t page;
    lv_obj_t *list;
    lv_obj_t *bar;
    lv_obj_t *btn_play;
    lv_obj_t *btn_delete;
    lv_timer_t *refresh_timer;
    char selected[64];
    uint32_t last_generation;
    recorder_service_state_t last_state;
} recorder_files_state_t;

_Static_assert(sizeof(recorder_files_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Recorder files state exceeds the lifecycle arena slot");

static void _files_row_event(lv_event_t *event);

static void _files_add_row(recorder_files_state_t *state,
                           const recorder_service_file_t *file,
                           size_t file_index)
{
    const bool selected = state->selected[0] != '\0' &&
                          strcmp(state->selected, file->name) == 0;
    lv_obj_t *row = lv_button_create(state->list);
    lv_obj_set_user_data(row, (void *)(uintptr_t)(file_index + 1U));
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 56);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row,
                              lv_color_hex(selected ? APP_UI_COLOR_SURFACE_HI :
                                           APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, _files_row_event, LV_EVENT_CLICKED, state);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_width(name, 0);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name,
                                lv_color_hex(selected ? APP_UI_COLOR_RAIN :
                                        APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(name, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(name, recorder_ui_display_name(file->name));

    char text[16];
    recorder_ui_format_mmss(file->duration_ms, text, sizeof(text));
    lv_obj_t *duration = lv_label_create(row);
    lv_obj_set_style_text_color(duration, lv_color_hex(APP_UI_COLOR_MUTED),
                                0);
    lv_obj_set_style_text_font(duration, app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(duration, text);
}

static void _files_rebuild(recorder_files_state_t *state)
{
    lv_obj_clean(state->list);
    recorder_service_file_t files[RECORDER_SERVICE_MAX_FILES];
    size_t count = 0U;
    if (recorder_service_list(files, RECORDER_SERVICE_MAX_FILES,
                              &count) != ESP_OK)
    {
        count = 0U;
    }
    bool selected_exists = false;
    for (size_t index = 0U; index < count; ++index)
    {
        if (state->selected[0] != '\0' &&
                strcmp(state->selected, files[index].name) == 0)
        {
            selected_exists = true;
        }
    }
    if (!selected_exists)
    {
        state->selected[0] = '\0';
    }
    if (count == 0U)
    {
        lv_obj_t *empty = app_ui_add_body_label(state->list,
                                                "暂无已完成录音");
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        _files_add_row(state, &files[index], index);
    }
}

static void _files_render(recorder_files_state_t *state)
{
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }
    if (snapshot.generation != state->last_generation ||
            snapshot.state != state->last_state)
    {
        state->last_generation = snapshot.generation;
        state->last_state = snapshot.state;
        _files_rebuild(state);
    }
    const bool playing = snapshot.state == RECORDER_SERVICE_PLAYING;
    app_ui_button_set_text(state->btn_play,
                           playing ? "停止播放" : "播放");
    const bool show_bar = playing && snapshot.playback_duration_ms > 0U;
    recorder_ui_set_visible(state->bar, show_bar);
    if (show_bar)
    {
        lv_bar_set_value(state->bar,
                         (int32_t)(snapshot.playback_position_ms * 100U /
                                   snapshot.playback_duration_ms),
                         LV_ANIM_OFF);
    }
}

static void _files_refresh(lv_timer_t *timer)
{
    _files_render(lv_timer_get_user_data(timer));
}

static void _files_play_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending)
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        (void)recorder_service_stop_playback();
    }
    else if (state->selected[0] != '\0')
    {
        (void)recorder_service_play(state->selected);
    }
    _files_render(state);
}

static void _files_delete_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    recorder_service_snapshot_t snapshot;
    if (recorder_service_get_snapshot(&snapshot) != ESP_OK ||
            snapshot.operation_pending || state->selected[0] == '\0')
    {
        return;
    }
    if (snapshot.state == RECORDER_SERVICE_PLAYING)
    {
        (void)recorder_service_stop_playback();
    }
    (void)recorder_service_delete(state->selected);
    state->selected[0] = '\0';
    _files_rebuild(state);
    _files_render(state);
}

static void _files_row_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    recorder_files_state_t *state = lv_event_get_user_data(event);
    const size_t index = (size_t)(uintptr_t)
                         lv_obj_get_user_data(lv_event_get_target(event)) - 1U;
    recorder_service_file_t files[RECORDER_SERVICE_MAX_FILES];
    size_t count = 0U;
    if (state == NULL ||
            recorder_service_list(files, RECORDER_SERVICE_MAX_FILES,
                                  &count) != ESP_OK || index >= count)
    {
        return;
    }
    (void)snprintf(state->selected, sizeof(state->selected), "%s",
                   files[index].name);
    _files_rebuild(state);
}

static void _files_mount(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->last_generation = UINT32_MAX;
    app_ui_page_create(&state->page, "播放库", true);
    app_ui_page_set_subtitle(&state->page, "本地录音");
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    state->list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->list);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, 0);
    lv_obj_set_flex_grow(state->list, 1);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 8, 0);
    app_ui_make_passive(state->list, true);
    lv_obj_set_scroll_dir(state->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(state->list, LV_SCROLLBAR_MODE_AUTO);

    state->bar = lv_bar_create(state->page.content);
    lv_obj_set_width(state->bar, LV_PCT(100));
    lv_obj_set_height(state->bar, 6);
    lv_bar_set_range(state->bar, 0, 100);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              0);
    lv_obj_set_style_bg_color(state->bar, lv_color_hex(APP_UI_COLOR_RAIN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(state->bar, 3, 0);
    app_ui_make_passive(state->bar, false);
    recorder_ui_set_visible(state->bar, false);

    lv_obj_t *controls = app_ui_button_row_create(state->page.content, 52);
    state->btn_play = app_ui_button_create(controls, "播放",
                                           _files_play_event, state);
    state->btn_delete = app_ui_button_create(controls, "删除",
                        _files_delete_event, state);

    _files_rebuild(state);
    state->refresh_timer = lv_timer_create(_files_refresh, 250U, state);
    _files_render(state);
}

static esp_err_t _files_pause(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _files_resume(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    state->last_generation = UINT32_MAX;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _files_render(state);
}

static void _files_unmount(const app_manager_page_context_t *context)
{
    recorder_files_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->list = NULL;
    state->bar = NULL;
    state->btn_play = NULL;
    state->btn_delete = NULL;
}

static const app_manager_page_ops_t s_recorder_files_ops =
{
    .mount = _files_mount,
    .resume = _files_resume,
    .pause = _files_pause,
    .unmount = _files_unmount,
};

const app_manager_page_definition_t recorder_files_page_definition =
{
    .ops = &s_recorder_files_ops,
    .memory_size = sizeof(recorder_files_state_t),
};
