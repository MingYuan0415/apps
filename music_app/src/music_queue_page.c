#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_QUEUE_ROW_HEIGHT 72
#define MUSIC_QUEUE_PAGE_SIZE 20U

typedef struct music_queue_state
{
    app_ui_page_t page;
    lv_obj_t *summary_label;
    lv_obj_t *list;
    lv_obj_t *status_label;
    lv_obj_t *retry_button;
    lv_obj_t *confirm_sheet;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_queue_snapshot_t *queue;
    uint64_t shown_generation;
    uint16_t shown_index;
    uint16_t current_page;
    uint16_t page_count;
    uint16_t total;
    uint16_t pending_remove_index;
    uint16_t request_page;
    uint16_t request_offset;
    bool dirty;
    bool busy;
    bool loading;
    bool failed;
    bool request_pending;
} music_queue_state_t;

_Static_assert(sizeof(music_queue_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music queue state exceeds the page arena");

static void _queue_refresh(music_queue_state_t *state);
static void _queue_dismiss_sheet(music_queue_state_t *state);

static void _queue_release_snapshot(music_queue_state_t *state)
{
    if (state->queue != NULL)
    {
        music_service_queue_release(state->queue);
        state->queue = NULL;
    }
    state->shown_generation = 0U;
    state->shown_index = UINT16_MAX;
}

static void _queue_release(music_queue_state_t *state)
{
    _queue_release_snapshot(state);
    state->total = 0U;
    state->current_page = 0U;
    state->page_count = 0U;
    state->request_pending = false;
    state->request_page = UINT16_MAX;
    state->request_offset = 0U;
    state->loading = false;
    state->failed = false;
}

static lv_obj_t *_queue_label(lv_obj_t *parent, const char *text,
                              app_theme_font_id_t font, uint32_t color,
                              bool one_line)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, one_line ? LV_LABEL_LONG_DOT :
                           LV_LABEL_LONG_WRAP);
    const lv_font_t *font_ptr = app_ui_font(font);
    lv_obj_set_style_text_font(label, font_ptr, 0);
    if (one_line)
    {
        lv_obj_set_height(label, lv_font_get_line_height(font_ptr));
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    app_ui_make_passive(label, false);
    return label;
}

static void _queue_status(music_queue_state_t *state, const char *text,
                          app_ui_status_t status)
{
    if (state->status_label == NULL)
    {
        return;
    }
    app_ui_set_status_text(state->status_label, text, status);
    lv_obj_set_hidden(state->status_label, text == NULL || text[0] == '\0');
}

static lv_obj_t *_queue_equalizer(lv_obj_t *parent, bool active)
{
    lv_obj_t *bars = lv_obj_create(parent);
    if (bars == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(bars);
    lv_obj_set_size(bars, 30, 30);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bars, 3, 0);
    app_ui_make_passive(bars, false);
    static const int32_t heights[] = {10, 18, 14};
    for (size_t index = 0U; index < 3U; ++index)
    {
        lv_obj_t *bar = lv_obj_create(bars);
        if (bar == NULL)
        {
            continue;
        }
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, 4, heights[index]);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(active ?
                APP_UI_COLOR_RAIN :
                APP_UI_COLOR_FAINT), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        app_ui_make_passive(bar, false);
    }
    return bars;
}

static void _queue_delete_cancel(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
        state->pending_remove_index = UINT16_MAX;
    }
}

static void _queue_delete_confirm(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    const uint16_t index = state->pending_remove_index;
    state->pending_remove_index = UINT16_MAX;
    if (state->queue == NULL || index >= state->total)
    {
        return;
    }
    const esp_err_t result = music_service_queue_remove(index);
    if (result != ESP_OK)
    {
        _queue_status(state, "移除失败，请重试", APP_UI_STATUS_WARNING);
        return;
    }
    state->busy = true;
    _queue_status(state, "正在移除…", APP_UI_STATUS_ACCENT);
}

static void _queue_delete_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->busy || state->confirm_sheet != NULL ||
            state->queue == NULL)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || encoded > state->total)
    {
        return;
    }
    const uint16_t index = (uint16_t)(encoded - 1U);
    if (index < state->queue->offset ||
            (uint32_t)index >= (uint32_t)state->queue->offset +
            state->queue->count)
    {
        return;
    }
    const uint16_t local = (uint16_t)(index - state->queue->offset);
    char title[160];
    (void)snprintf(title, sizeof(title), "从播放列表移除《%s》？",
                   state->queue->songs[local].title);
    state->pending_remove_index = index;
    state->confirm_sheet = app_ui_sheet_open(state->page.root, title,
        "移除后不会恢复，是否继续？");
    if (state->confirm_sheet == NULL)
    {
        state->pending_remove_index = UINT16_MAX;
        return;
    }
    (void)app_ui_sheet_add_action(state->confirm_sheet, "移除", true,
                                  _queue_delete_confirm, state);
    (void)app_ui_sheet_add_action(state->confirm_sheet, "取消", false,
                                  _queue_delete_cancel, state);
}

static void _queue_play_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->queue == NULL)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || encoded > state->total)
    {
        return;
    }
    const uint16_t index = (uint16_t)(encoded - 1U);
    music_player_status_t player;
    if (music_service_get_player_status(&player) == ESP_OK &&
            player.queue_index == index &&
            (player.state == MUSIC_PLAYBACK_PLAYING ||
             player.state == MUSIC_PLAYBACK_PAUSED ||
             player.state == MUSIC_PLAYBACK_BUFFERING))
    {
        _queue_status(state, "正在播放这首歌曲", APP_UI_STATUS_ACCENT);
        return;
    }
    const esp_err_t result = music_service_queue_play(index);
    if (result != ESP_OK)
    {
        _queue_status(state, "切换歌曲失败，请重试", APP_UI_STATUS_WARNING);
        return;
    }
    (void)music_ui_back_to_player();
}

static void _queue_update_summary(music_queue_state_t *state)
{
    if (state->summary_label == NULL)
    {
        return;
    }
    if (state->total == 0U)
    {
        app_ui_label_set_text_if(state->summary_label, "共 0 首");
        return;
    }
    char summary[64];
    music_player_status_t player;
    if (music_service_get_player_status(&player) == ESP_OK &&
            player.queue_count == state->total &&
            player.queue_index < player.queue_count)
    {
        (void)snprintf(summary, sizeof(summary), "共 %u 首 · 第 %u / %u 首",
                       (unsigned)state->total,
                       (unsigned)(player.queue_index + 1U),
                       (unsigned)player.queue_count);
    }
    else
    {
        (void)snprintf(summary, sizeof(summary), "共 %u 首",
                       (unsigned)state->total);
    }
    app_ui_label_set_text_if(state->summary_label, summary);
}

static void _queue_prev_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->current_page > 0U)
    {
        state->current_page = (uint16_t)(state->current_page - 1U);
        _queue_release_snapshot(state);
        state->loading = true;
        state->failed = false;
        _queue_refresh(state);
        lv_obj_update_layout(state->page.content);
        lv_obj_scroll_to_y(state->page.content, 0, LV_ANIM_OFF);
    }
}

static void _queue_next_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->current_page + 1U < state->page_count)
    {
        state->current_page = (uint16_t)(state->current_page + 1U);
        _queue_release_snapshot(state);
        state->loading = true;
        state->failed = false;
        _queue_refresh(state);
        lv_obj_update_layout(state->page.content);
        lv_obj_scroll_to_y(state->page.content, 0, LV_ANIM_OFF);
    }
}

static void _queue_build_page(music_queue_state_t *state)
{
    if (state->list == NULL)
    {
        return;
    }
    _queue_update_summary(state);
    lv_obj_clean(state->list);
    const uint16_t page_count = state->page_count > 0U ? state->page_count : 1U;
    if (state->current_page > 0U)
    {
        char subtitle[48];
        (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                       (unsigned)(state->current_page + 1U),
                       (unsigned)page_count);
        (void)app_ui_add_action(state->list, LV_SYMBOL_PREV, "上一页",
                                subtitle, _queue_prev_clicked, state);
    }
    if (state->queue == NULL)
    {
        if (state->failed)
        {
            if (state->retry_button != NULL)
            {
                lv_obj_set_hidden(state->retry_button, false);
            }
        }
        else if (state->loading)
        {
            (void)app_ui_add_body_label(state->list, "正在加载队列…");
        }
        else
        {
            (void)app_ui_add_empty_state(state->list, "队列为空",
                                         "从歌单播放歌曲后可在这里管理");
        }
    }
    else
    {
        music_player_status_t player;
        const bool has_player =
            music_service_get_player_status(&player) == ESP_OK;
        for (uint16_t index = 0U; index < state->queue->count; ++index)
        {
            const uint16_t global_index = (uint16_t)(
                                              state->queue->offset + index);
            const music_song_t *song = &state->queue->songs[index];
            const bool active = has_player &&
                                player.queue_index == global_index;
            lv_obj_t *row = lv_obj_create(state->list);
            if (row == NULL)
            {
                return;
            }
            lv_obj_remove_style_all(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(row, MUSIC_QUEUE_ROW_HEIGHT, 0);
            lv_obj_set_style_radius(row, APP_UI_RADIUS_CONTROL, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_shadow_width(row, 0, 0);
            lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_outline_width(row, 0, 0);
            lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(
                                          active ? APP_UI_COLOR_RAIN_TINT :
                                          APP_UI_COLOR_SURFACE), 0);
            lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER :
                                    LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(row, 8, 0);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            app_ui_make_passive(row, false);

            lv_obj_t *main = lv_button_create(row);
            if (main == NULL)
            {
                continue;
            }
            app_ui_click_only(main);
            lv_obj_set_width(main, 0);
            lv_obj_set_flex_grow(main, 1);
            lv_obj_set_height(main, LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(main, MUSIC_QUEUE_ROW_HEIGHT - 12, 0);
            music_ui_flat_button_reset(main, APP_UI_RADIUS_CONTROL);
            lv_obj_set_style_bg_color(main, lv_color_hex(APP_UI_COLOR_SURFACE),
                                      LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(main, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_pad_column(main, 12, 0);
            lv_obj_set_flex_flow(main, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(main, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_user_data(main, (void *)(uintptr_t)(global_index + 1U));
            lv_obj_add_event_cb(main, _queue_play_clicked, LV_EVENT_CLICKED,
                                state);
            if (active)
            {
                (void)_queue_equalizer(main, true);
            }
            else
            {
                char ordinal[8];
                (void)snprintf(ordinal, sizeof(ordinal), "%u",
                               (unsigned)(global_index + 1U));
                lv_obj_t *number = _queue_label(main, ordinal,
                                                APP_THEME_FONT_SMALL,
                                                APP_UI_COLOR_FAINT, true);
                lv_obj_set_width(number, 30);
                lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
            }
            lv_obj_t *text = lv_obj_create(main);
            lv_obj_remove_style_all(text);
            lv_obj_set_width(text, 0);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_height(text, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(text, 1, 0);
            app_ui_make_passive(text, false);
            _queue_label(text, song->title, APP_THEME_FONT_BODY,
                         active ? APP_UI_COLOR_RAIN : APP_UI_COLOR_TEXT, false);
            _queue_label(text, song->artist[0] == '\0' ? "未知歌手" :
                         song->artist, APP_THEME_FONT_SMALL,
                         APP_UI_COLOR_MUTED, true);

            lv_obj_t *remove = music_ui_plain_icon_button(
                                   row, APP_IMAGE_UI_TRASH, LV_SYMBOL_TRASH,
                                   MUSIC_UI_CONTROL_SIZE, 26,
                                   APP_UI_COLOR_FAINT, _queue_delete_clicked,
                                   state);
            if (remove != NULL)
            {
                lv_obj_set_user_data(remove,
                                     (void *)(uintptr_t)(global_index + 1U));
            }
        }
        if (state->queue->count == 0U)
        {
            (void)app_ui_add_empty_state(state->list, "队列为空",
                                         "从歌单播放歌曲后可在这里管理");
        }
    }
    if (state->current_page + 1U < page_count)
    {
        char subtitle[48];
        (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                       (unsigned)(state->current_page + 2U),
                       (unsigned)page_count);
        (void)app_ui_add_action(state->list, LV_SYMBOL_NEXT, "下一页",
                                subtitle, _queue_next_clicked, state);
    }
}

static void _queue_store_view(music_queue_state_t *state,
                              const music_queue_snapshot_t *view,
                              uint16_t page)
{
    _queue_release_snapshot(state);
    state->queue = (music_queue_snapshot_t *)view;
    state->shown_generation = view->generation;
    state->total = view->total;
    state->page_count = music_ui_queue_page_count(state->total,
        MUSIC_QUEUE_PAGE_SIZE);
    state->current_page = page;
    state->request_pending = false;
    state->loading = false;
    state->failed = false;
    state->dirty = false;
    _queue_build_page(state);
}

static bool _queue_request_page(music_queue_state_t *state, uint16_t page)
{
    if (state->request_pending || state->failed)
    {
        return false;
    }
    if (state->page_count > 0U && page >= state->page_count)
    {
        return false;
    }
    const uint16_t offset = (uint16_t)(page * MUSIC_QUEUE_PAGE_SIZE);
    const music_queue_snapshot_t *view = NULL;
    state->request_page = page;
    state->request_offset = offset;
    state->request_pending = true;
    state->loading = true;
    const esp_err_t result = music_service_queue_view_acquire(
                                 offset, MUSIC_QUEUE_PAGE_SIZE, &view);
    if (result == ESP_OK && view != NULL)
    {
        _queue_store_view(state, view, page);
        return true;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        return true;
    }
    state->request_pending = false;
    state->loading = false;
    if (result == ESP_ERR_INVALID_STATE)
    {
        _queue_release_snapshot(state);
        state->total = 0U;
        state->page_count = 0U;
        state->current_page = 0U;
        _queue_build_page(state);
        return false;
    }
    state->failed = true;
    _queue_status(state, result == ESP_ERR_TIMEOUT ?
                  "队列加载超时，请重试" : "队列加载失败",
                  APP_UI_STATUS_WARNING);
    _queue_build_page(state);
    return false;
}

static void _queue_refresh(music_queue_state_t *state)
{
    if (state->request_pending)
    {
        const music_queue_snapshot_t *view = NULL;
        const esp_err_t result = music_service_queue_view_acquire(
                                     state->request_offset,
                                     MUSIC_QUEUE_PAGE_SIZE, &view);
        if (result == ESP_OK && view != NULL)
        {
            _queue_store_view(state, view, state->request_page);
        }
        else if (result == ESP_ERR_INVALID_STATE)
        {
            state->request_pending = false;
            state->loading = false;
            _queue_release_snapshot(state);
            state->total = 0U;
            state->page_count = 0U;
            _queue_build_page(state);
        }
        else if (result != ESP_ERR_NOT_FOUND)
        {
            state->request_pending = false;
            state->loading = false;
            state->failed = true;
            _queue_build_page(state);
        }
        return;
    }
    if (state->failed || state->total == 0U)
    {
        return;
    }
    if (state->queue == NULL)
    {
        _queue_request_page(state, state->current_page);
    }
    else if (state->dirty)
    {
        state->dirty = false;
        _queue_build_page(state);
    }
}

static void _queue_retry_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    const uint16_t page = state->current_page;
    _queue_release(state);
    state->current_page = page;
    if (state->retry_button != NULL)
    {
        lv_obj_set_hidden(state->retry_button, true);
    }
    _queue_status(state, "", APP_UI_STATUS_NEUTRAL);
    state->dirty = true;
    _queue_request_page(state, page);
}

static void _queue_timer_cb(lv_timer_t *timer)
{
    music_queue_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    music_player_status_t player;
    if (music_service_get_player_status(&player) == ESP_OK)
    {
        if (player.queue_index != state->shown_index)
        {
            state->shown_index = player.queue_index;
            state->dirty = true;
        }
        if (player.queue_count != state->total && player.queue_count != 0U)
        {
            state->total = player.queue_count;
            state->page_count = music_ui_queue_page_count(
                                    state->total, MUSIC_QUEUE_PAGE_SIZE);
            state->dirty = true;
        }
    }
    _queue_refresh(state);
}

static void _queue_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                            const void *payload, size_t payload_size,
                            void *user_data)
{
    (void)msg_id;
    music_queue_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_QUEUE)
    {
        _queue_dismiss_sheet(state);
        state->busy = false;
        state->request_pending = false;
        state->loading = false;
        const music_service_event_t *event = payload;
        if (event != NULL && payload_size >= sizeof(*event) &&
                event->failure != MUSIC_SERVICE_FAILURE_NONE)
        {
            state->failed = true;
            _queue_status(state, "队列加载失败", APP_UI_STATUS_WARNING);
            _queue_build_page(state);
            return;
        }
        _queue_status(state, "", APP_UI_STATUS_NEUTRAL);
        _queue_release_snapshot(state);
        state->dirty = true;
        _queue_refresh(state);
    }
    else if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS)
    {
        _queue_refresh(state);
    }
}

static void _queue_mount(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->shown_index = UINT16_MAX;
    state->pending_remove_index = UINT16_MAX;
    state->request_page = UINT16_MAX;
    state->dirty = true;
    app_ui_page_create(&state->page, "播放列表", true);
    music_ui_add_header_back(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    state->summary_label = app_ui_add_body_label(state->page.content,
        "队列为空");
    lv_obj_set_width(state->summary_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->summary_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(state->summary_label,
                                lv_color_hex(APP_UI_COLOR_FAINT), 0);
    state->status_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_hidden(state->status_label, true);
    state->retry_button = app_ui_add_action(state->page.content,
                                            LV_SYMBOL_REFRESH,
                                            "重新加载队列", NULL,
                                            _queue_retry_clicked, state);
    if (state->retry_button != NULL)
    {
        lv_obj_set_hidden(state->retry_button, true);
    }
    state->list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->list);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 4, 0);
    lv_obj_set_scroll_dir(state->list, LV_DIR_NONE);
    app_ui_make_passive(state->list, false);
    _queue_build_page(state);
}

static void _queue_resume(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _queue_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_queue_timer_cb, 500U, state);
    music_player_status_t player;
    if (music_service_get_player_status(&player) == ESP_OK &&
            player.queue_count > 0U)
    {
        state->total = player.queue_count;
        state->page_count = music_ui_queue_page_count(
                                state->total, MUSIC_QUEUE_PAGE_SIZE);
        state->current_page = music_ui_queue_page_for_index(
                                  player.queue_index, MUSIC_QUEUE_PAGE_SIZE);
    }
    _queue_request_page(state, state->current_page);
}

static void _queue_dismiss_sheet(music_queue_state_t *state)
{
    if (state->confirm_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->confirm_sheet);
        state->confirm_sheet = NULL;
    }
    state->pending_remove_index = UINT16_MAX;
}

static esp_err_t _queue_pause(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    if (state->timer != NULL)
    {
        lv_timer_delete(state->timer);
        state->timer = NULL;
    }
    if (state->subscription != EVENT_BUS_SUB_HANDLE_INVALID)
    {
        (void)event_bus_unsubscribe(state->subscription);
        state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    }
    _queue_dismiss_sheet(state);
    _queue_release(state);
    state->busy = false;
    return ESP_OK;
}

static void _queue_unmount(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    _queue_dismiss_sheet(state);
    _queue_release(state);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_queue_ops =
{
    .mount = _queue_mount,
    .resume = _queue_resume,
    .pause = _queue_pause,
    .unmount = _queue_unmount,
};

const app_manager_page_definition_t music_queue_page_definition =
{
    .ops = &s_queue_ops,
    .memory_size = sizeof(music_queue_state_t),
};
