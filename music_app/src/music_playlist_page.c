#include "music_app_internal.h"

#include "event_bus.h"
#include "mt_log.h"

#include <stdio.h>
#include <string.h>

typedef struct music_playlist_state
{
    app_ui_page_t page;
    lv_obj_t *status_label;
    lv_obj_t *list;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_playlist_ref_t ref;
    music_playlist_snapshot_t *snapshot;
    uint16_t requested_offset;
    bool loading;
    bool failed;
} music_playlist_state_t;

_Static_assert(sizeof(music_playlist_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music playlist state exceeds the page arena");

static void _playlist_song_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || lv_event_get_code(event) != LV_EVENT_CLICKED ||
            state->snapshot == NULL)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || encoded > state->snapshot->count)
    {
        return;
    }
    const esp_err_t result = music_service_play_playlist(
                                 &state->ref, (uint16_t)(encoded - 1U));
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               "播放功能暂不可用，请稍后重试",
                               APP_UI_STATUS_WARNING);
        return;
    }
    (void)music_ui_open_player();
}

static void _playlist_build(music_playlist_state_t *state)
{
    lv_obj_clean(state->list);
    if (state->snapshot == NULL)
    {
        (void)app_ui_add_body_label(state->list, "正在加载歌曲…");
        return;
    }
    char summary[192];
    (void)snprintf(summary, sizeof(summary), "%s · 共 %u 首",
                   state->snapshot->playlist.title,
                   (unsigned)state->snapshot->total);
    (void)app_ui_add_section(state->list, summary);
    for (uint16_t index = 0U; index < state->snapshot->count; ++index)
    {
        lv_obj_t *row = music_ui_add_song_row(
                            state->list, &state->snapshot->songs[index], index,
                            _playlist_song_clicked, state);
        if (row != NULL)
        {
            lv_obj_set_user_data(row, (void *)(uintptr_t)(index + 1U));
        }
    }
    if (state->snapshot->count == 0U)
    {
        (void)app_ui_add_body_label(state->list, "这个歌单还没有歌曲");
    }
}

static void _playlist_timer_cb(lv_timer_t *timer)
{
    music_playlist_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    if (state->snapshot != NULL || state->loading || state->failed)
    {
        return;
    }
    const music_playlist_snapshot_t *snapshot = NULL;
    const esp_err_t result = music_service_playlist_acquire(
                                 &state->ref, state->requested_offset,
                                 MUSIC_SERVICE_MAX_SONGS, &snapshot);
    if (result == ESP_OK && snapshot != NULL)
    {
        state->snapshot = (music_playlist_snapshot_t *)snapshot;
        app_ui_set_status_text(state->status_label, "已加载",
                               APP_UI_STATUS_SUCCESS);
        _playlist_build(state);
    }
    else if (result == ESP_ERR_NOT_FOUND)
    {
        state->loading = true;
    }
    else
    {
        state->failed = true;
        app_ui_set_status_text(state->status_label, "歌单加载失败",
                               APP_UI_STATUS_WARNING);
    }
}

static void _playlist_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                               const void *payload, size_t payload_size,
                               void *user_data)
{
    (void)msg_id;
    (void)sub_type;
    (void)payload;
    (void)payload_size;
    music_playlist_state_t *state = user_data;
    if (state != NULL)
    {
        state->loading = false;
        if (state->timer != NULL)
        {
            _playlist_timer_cb(state->timer);
        }
    }
}

static void _playlist_copy_arguments(music_playlist_state_t *state,
                                     const app_manager_typed_blob_t *arguments)
{
    if (arguments == NULL ||
            arguments->version != APP_MANAGER_TYPED_BLOB_VERSION ||
            arguments->type != MUSIC_ARGUMENT_PLAYLIST ||
            arguments->size != sizeof(music_playlist_arguments_t))
    {
        return;
    }
    music_playlist_arguments_t payload;
    memcpy(&payload, arguments->payload, sizeof(payload));
    state->ref = payload.ref;
}

static void _playlist_mount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    memset(&state->ref, 0, sizeof(state->ref));
    state->snapshot = NULL;
    state->loading = false;
    state->failed = false;
    state->requested_offset = 0U;
    _playlist_copy_arguments(state, context->arguments);
    app_ui_page_create(&state->page, "歌单", true);
    state->status_label = app_ui_add_body_label(state->page.content,
        "正在加载…");
    state->list = lv_obj_create(state->page.content);
    lv_obj_set_width(state->list, LV_PCT(100));
    lv_obj_set_height(state->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(state->list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->list, 0, 0);
    lv_obj_set_style_pad_all(state->list, 0, 0);
    lv_obj_set_style_pad_row(state->list, APP_UI_GAP_TIGHT, 0);
    lv_obj_set_scroll_dir(state->list, LV_DIR_NONE);
}

static void _playlist_resume(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _playlist_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_playlist_timer_cb, 500U, state);
    _playlist_timer_cb(state->timer);
}

static esp_err_t _playlist_pause(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
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
    return ESP_OK;
}

static void _playlist_unmount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    if (state->snapshot != NULL)
    {
        music_service_playlist_release(state->snapshot);
        state->snapshot = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->status_label = NULL;
    state->list = NULL;
}

static const app_manager_page_ops_t s_playlist_ops =
{
    .mount = _playlist_mount,
    .resume = _playlist_resume,
    .pause = _playlist_pause,
    .unmount = _playlist_unmount,
};

const app_manager_page_definition_t music_playlist_page_definition =
{
    .ops = &s_playlist_ops,
    .memory_size = sizeof(music_playlist_state_t),
};
