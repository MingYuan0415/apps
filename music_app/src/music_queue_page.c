#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_QUEUE_ROW_HEIGHT 72

typedef struct music_queue_state
{
    app_ui_page_t page;
    lv_obj_t *list;
    lv_obj_t *status_label;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_queue_snapshot_t *queue;
    uint64_t shown_generation;
    uint16_t shown_index;
    bool dirty;
    bool busy;
} music_queue_state_t;

_Static_assert(sizeof(music_queue_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music queue state exceeds the page arena");

static void _queue_release(music_queue_state_t *state)
{
    if (state->queue != NULL)
    {
        music_service_queue_release(state->queue);
        state->queue = NULL;
    }
    state->shown_generation = 0U;
    state->shown_index = UINT16_MAX;
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

static void _queue_delete_clicked(lv_event_t *event)
{
    music_queue_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->busy)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || state->queue == NULL ||
            encoded > state->queue->count)
    {
        return;
    }
    const esp_err_t result = music_service_queue_remove(
                                 (uint16_t)(encoded - 1U));
    if (result != ESP_OK)
    {
        _queue_status(state, "移除失败，请重试", APP_UI_STATUS_WARNING);
        return;
    }
    state->busy = true;
    _queue_status(state, "正在移除…", APP_UI_STATUS_ACCENT);
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
    if (encoded == 0U || encoded > state->queue->count)
    {
        return;
    }
    const esp_err_t result = music_service_queue_play(
                                 (uint16_t)(encoded - 1U));
    if (result != ESP_OK)
    {
        _queue_status(state, "切换歌曲失败，请重试", APP_UI_STATUS_WARNING);
        return;
    }
    (void)music_ui_back_to_player();
}

static void _queue_rebuild(music_queue_state_t *state)
{
    if (state->list == NULL)
    {
        return;
    }
    lv_obj_clean(state->list);
    if (state->queue == NULL || state->queue->count == 0U)
    {
        (void)app_ui_add_empty_state(state->list, "队列为空",
                                     "从歌单播放歌曲后可在这里管理");
        return;
    }
    music_player_status_t player;
    const bool has_player =
        music_service_get_player_status(&player) == ESP_OK;
    const uint16_t current = has_player ? player.queue_index : UINT16_MAX;
    for (uint16_t index = 0U; index < state->queue->count; ++index)
    {
        const music_song_t *song = &state->queue->songs[index];
        const bool active = index == current;
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
        lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
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
        lv_obj_set_user_data(main, (void *)(uintptr_t)(index + 1U));
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
                           (unsigned)(index + 1U));
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
                     active ? APP_UI_COLOR_RAIN : APP_UI_COLOR_TEXT, true);
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
            lv_obj_set_user_data(remove, (void *)(uintptr_t)(index + 1U));
        }
    }
}

static void _queue_refresh(music_queue_state_t *state)
{
    const music_queue_snapshot_t *current = NULL;
    const esp_err_t result = music_service_queue_acquire(&current);
    if (result == ESP_OK && current != NULL)
    {
        const bool changed = state->queue == NULL ||
                             current->generation != state->shown_generation ||
                             state->dirty;
        if (state->queue != NULL)
        {
            music_service_queue_release(state->queue);
        }
        state->queue = (music_queue_snapshot_t *)current;
        state->shown_generation = current->generation;
        if (changed)
        {
            _queue_rebuild(state);
        }
        state->dirty = false;
        return;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        if (state->queue != NULL || state->shown_generation != 0U)
        {
            _queue_release(state);
            _queue_rebuild(state);
        }
        return;
    }
    _queue_status(state, "队列加载失败", APP_UI_STATUS_WARNING);
}

static void _queue_timer_cb(lv_timer_t *timer)
{
    music_queue_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    music_player_status_t player;
    if (music_service_get_player_status(&player) == ESP_OK &&
            player.queue_index != state->shown_index)
    {
        state->shown_index = player.queue_index;
        state->dirty = true;
    }
    _queue_refresh(state);
}

static void _queue_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                            const void *payload, size_t payload_size,
                            void *user_data)
{
    (void)msg_id;
    (void)payload;
    (void)payload_size;
    music_queue_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_QUEUE ||
            sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS)
    {
        state->busy = false;
        _queue_status(state, "", APP_UI_STATUS_NEUTRAL);
        state->dirty = true;
        _queue_refresh(state);
    }
}

static void _queue_mount(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->shown_index = UINT16_MAX;
    state->dirty = true;
    app_ui_page_create(&state->page, "播放列表", true);
    music_ui_add_header_back(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    state->status_label = app_ui_add_body_label(state->page.content, "");
    lv_obj_set_width(state->status_label, LV_PCT(100));
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_hidden(state->status_label, true);
    state->list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->list);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->list, 4, 0);
    lv_obj_set_scroll_dir(state->list, LV_DIR_NONE);
    app_ui_make_passive(state->list, false);
    _queue_rebuild(state);
}

static void _queue_resume(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _queue_event_cb, state, EVENT_BUS_DISPATCH_UI,
                              &state->subscription);
    state->timer = lv_timer_create(_queue_timer_cb, 500U, state);
    _queue_refresh(state);
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
    _queue_release(state);
    state->busy = false;
    return ESP_OK;
}

static void _queue_unmount(const app_manager_page_context_t *context)
{
    music_queue_state_t *state = context->state;
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
