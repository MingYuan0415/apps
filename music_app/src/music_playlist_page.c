#define DBG_TAG "music_playlist"
#include "mt_log.h"

#include "music_app_internal.h"

#include "event_bus.h"

#include <stdio.h>
#include <string.h>

#define MUSIC_PLAYLIST_PAGE_RADIUS 2U
#define MUSIC_PLAYLIST_PAGE_CACHE (MUSIC_PLAYLIST_PAGE_RADIUS * 2U + 1U)
#define MUSIC_PLAYLIST_PAGE_REQUEST_SIZE 10U
#define MUSIC_PLAYLIST_COVER_SIZE MUSIC_UI_HERO_COVER
#define MUSIC_PLAYLIST_COVER_TIMEOUT_TICKS 40U
#define MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS 2U

typedef struct music_playlist_state
{
    app_ui_page_t page;
    lv_obj_t *hero;
    lv_obj_t *details;
    lv_obj_t *title_label;
    lv_obj_t *creator_label;
    lv_obj_t *track_count_label;
    lv_obj_t *status_label;
    lv_obj_t *play_all_button;
    lv_obj_t *song_list;
    lv_timer_t *timer;
    event_bus_sub_handle_t subscription;
    music_playlist_ref_t ref;
    music_playlist_t info;
    music_playlist_snapshot_t *pages[MUSIC_PLAYLIST_PAGE_CACHE];
    uint16_t page_ids[MUSIC_PLAYLIST_PAGE_CACHE];
    uint16_t current_page;
    uint16_t page_count;
    uint16_t pending_page;
    uint16_t request_offset;
    uint16_t loaded_pages;
    uint32_t loaded_rows;
    uint32_t total;
    music_ui_cover_t cover;
    uint16_t cover_wait_ticks;
    uint32_t cover_token;
    uint32_t loading_since_ms;
    uint8_t cover_attempts;
    bool loading;
    bool failed;
    bool request_pending;
    bool cover_requested;
    bool cover_failed;
} music_playlist_state_t;

_Static_assert(sizeof(music_playlist_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Music playlist state exceeds the page arena");

static void _playlist_build_hero(music_playlist_state_t *state);
static void _playlist_build_page(music_playlist_state_t *state);
static void _playlist_schedule(music_playlist_state_t *state);
static void _playlist_retry_clicked(lv_event_t *event);

static void _playlist_update_cover(music_playlist_state_t *state)
{
    if (state->cover.frame == NULL)
    {
        return;
    }
    const char *url = state->info.cover_url;
    if (url[0] == '\0')
    {
        return;
    }
    if (state->cover_requested)
    {
        if (state->cover_wait_ticks < UINT16_MAX)
        {
            ++state->cover_wait_ticks;
        }
        if (state->cover_wait_ticks <= MUSIC_PLAYLIST_COVER_TIMEOUT_TICKS)
        {
            return;
        }
        state->cover_requested = false;
    }
    if (state->cover.image != NULL || state->cover_failed ||
            state->cover_attempts >= MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS)
    {
        return;
    }
    const music_image_t *image = NULL;
    state->cover_token = music_ui_url_token(url);
    state->cover_requested = true;
    state->cover_wait_ticks = 0U;
    ++state->cover_attempts;
    const esp_err_t result = music_service_image_acquire(
                                 url, MUSIC_PLAYLIST_COVER_SIZE,
                                 MUSIC_PLAYLIST_COVER_SIZE, state->cover_token,
                                 &image);
    if (result == ESP_OK && image != NULL)
    {
        state->cover_requested = false;
        state->cover_attempts = 0U;
        music_ui_cover_set_image(&state->cover, (music_image_t *)image);
    }
    else if (result != ESP_ERR_NOT_FOUND)
    {
        state->cover_requested = false;
        state->cover_failed =
            state->cover_attempts >= MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS;
    }
}

static music_playlist_snapshot_t *_playlist_page_snapshot(
    music_playlist_state_t *state, uint16_t page)
{
    for (size_t slot = 0U; slot < MUSIC_PLAYLIST_PAGE_CACHE; ++slot)
    {
        if (state->pages[slot] != NULL && state->page_ids[slot] == page)
        {
            return state->pages[slot];
        }
    }
    return NULL;
}

static void _playlist_refresh_after_store(music_playlist_state_t *state,
        uint16_t stored_page)
{
    if (stored_page == state->current_page ||
            _playlist_page_snapshot(state, state->current_page) == NULL)
    {
        _playlist_build_page(state);
    }
}

static void _playlist_release_page(music_playlist_state_t *state,
                                   size_t slot)
{
    if (state->pages[slot] != NULL)
    {
        music_service_playlist_release(state->pages[slot]);
        state->pages[slot] = NULL;
        state->page_ids[slot] = UINT16_MAX;
    }
}

static void _playlist_release_all_pages(music_playlist_state_t *state)
{
    for (size_t slot = 0U; slot < MUSIC_PLAYLIST_PAGE_CACHE; ++slot)
    {
        _playlist_release_page(state, slot);
    }
    state->loaded_pages = 0U;
    state->loaded_rows = 0U;
}

static void _playlist_release_outside_window(music_playlist_state_t *state)
{
    if (state->page_count == 0U)
    {
        return;
    }
    const int32_t first = (int32_t)state->current_page -
                          (int32_t)MUSIC_PLAYLIST_PAGE_RADIUS;
    const int32_t last = (int32_t)state->current_page +
                         (int32_t)MUSIC_PLAYLIST_PAGE_RADIUS;
    for (size_t slot = 0U; slot < MUSIC_PLAYLIST_PAGE_CACHE; ++slot)
    {
        if (state->pages[slot] == NULL)
        {
            continue;
        }
        const int32_t page = (int32_t)state->page_ids[slot];
        if (page < first || page > last)
        {
            _playlist_release_page(state, slot);
        }
    }
}

static void _playlist_go_to_page(music_playlist_state_t *state,
                                 uint16_t page)
{
    if (state->page_count == 0U || page >= state->page_count)
    {
        return;
    }
    state->current_page = page;
    _playlist_release_outside_window(state);
    _playlist_build_hero(state);
    _playlist_build_page(state);
    lv_obj_update_layout(state->page.content);
    lv_obj_scroll_to_y(state->page.content, 0, LV_ANIM_OFF);
    _playlist_schedule(state);
}

static void _playlist_prev_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->current_page > 0U)
    {
        _playlist_go_to_page(state, (uint16_t)(state->current_page - 1U));
    }
}

static void _playlist_next_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state != NULL && state->current_page + 1U < state->page_count)
    {
        _playlist_go_to_page(state, (uint16_t)(state->current_page + 1U));
    }
}

static void _playlist_song_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    const music_playlist_snapshot_t *snapshot =
        _playlist_page_snapshot(state, state->current_page);
    if (snapshot == NULL)
    {
        return;
    }
    const uintptr_t encoded = (uintptr_t)lv_obj_get_user_data(
                                  lv_event_get_target_obj(event));
    if (encoded == 0U || encoded > snapshot->count)
    {
        return;
    }
    const esp_err_t result = music_service_play_playlist_snapshot(
                                 snapshot, (uint16_t)(encoded - 1U));
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "播放请求超时，请重试" :
                               "无法开始播放，请稍后重试",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    app_ui_set_status_text(state->status_label, "正在准备播放…",
                           APP_UI_STATUS_ACCENT);
    lv_obj_set_hidden(state->status_label, false);
    (void)music_ui_back_to_player();
}

static void _playlist_play_all_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    const music_playlist_snapshot_t *snapshot = state != NULL ?
        _playlist_page_snapshot(state, state->current_page) : NULL;
    if (snapshot == NULL || snapshot->count == 0U)
    {
        return;
    }
    const esp_err_t result = music_service_play_playlist_snapshot(
                                 snapshot, 0U);
    if (result != ESP_OK)
    {
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "播放请求超时，请重试" :
                               "无法播放歌单，请稍后重试",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        return;
    }
    app_ui_set_status_text(state->status_label, "正在准备播放…",
                           APP_UI_STATUS_ACCENT);
    lv_obj_set_hidden(state->status_label, false);
    (void)music_ui_back_to_player();
}

static void _playlist_build_hero(music_playlist_state_t *state)
{
    if (state->hero == NULL)
    {
        return;
    }
    app_ui_label_set_text_if(state->title_label, state->info.title);
    app_ui_label_set_text_if(state->creator_label,
                             state->info.creator[0] == '\0' ?
                             "QQ 音乐" : state->info.creator);
    char count[80];
    if (state->page_count > 1U)
    {
        (void)snprintf(count, sizeof(count), "共 %u 首 · 第 %u / %u 页",
                       (unsigned)state->total,
                       (unsigned)(state->current_page + 1U),
                       (unsigned)state->page_count);
    }
    else
    {
        (void)snprintf(count, sizeof(count), "共 %u 首",
                       (unsigned)state->total);
    }
    app_ui_label_set_text_if(state->track_count_label, count);
    const uint32_t token = music_ui_url_token(state->info.cover_url);
    if (token != state->cover_token)
    {
        music_ui_cover_release(&state->cover);
        state->cover_token = token;
        state->cover_attempts = 0U;
        state->cover_wait_ticks = 0U;
        state->cover_requested = false;
        state->cover_failed = false;
    }
    lv_obj_set_hidden(state->details, false);
    lv_obj_set_hidden(state->status_label, true);
    if (state->total == 0U)
    {
        lv_obj_add_state(state->play_all_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(state->play_all_button, LV_STATE_DISABLED);
    }
}

static uint16_t _playlist_loaded_rows_before(
    const music_playlist_state_t *state, uint16_t page)
{
    uint32_t rows = 0U;
    for (uint16_t index = 0U; index < page; ++index)
    {
        const music_playlist_snapshot_t *snapshot =
            _playlist_page_snapshot((music_playlist_state_t *)state, index);
        if (snapshot == NULL)
        {
            break;
        }
        rows += snapshot->count;
    }
    return rows > UINT16_MAX ? UINT16_MAX : (uint16_t)rows;
}

static uint16_t _playlist_page_offset(const music_playlist_state_t *state,
                                      uint16_t page)
{
    if (page == 0U)
    {
        return 0U;
    }
    const music_playlist_snapshot_t *snapshot =
        _playlist_page_snapshot((music_playlist_state_t *)state, page);
    if (snapshot != NULL)
    {
        return snapshot->offset;
    }
    if (page == state->loaded_pages)
    {
        return state->loaded_rows > UINT16_MAX ? UINT16_MAX :
               (uint16_t)state->loaded_rows;
    }
    return _playlist_loaded_rows_before(state, page);
}

static lv_obj_t *_playlist_add_notice(lv_obj_t *parent, const char *text,
                                      app_ui_status_t status)
{
    lv_obj_t *label = app_ui_add_body_label(parent, text);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(label, text, status);
    return label;
}

static bool _playlist_snapshot_complete(
    const music_playlist_snapshot_t *snapshot, uint16_t offset)
{
    return snapshot != NULL &&
           (snapshot->count > 0U || snapshot->total <= offset);
}

static bool _playlist_store_page(music_playlist_state_t *state,
                                 uint16_t page,
                                 music_playlist_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }
    const bool new_page = _playlist_page_snapshot(state, page) == NULL;
    const size_t slot = (size_t)(page % MUSIC_PLAYLIST_PAGE_CACHE);
    if (state->pages[slot] != NULL && state->page_ids[slot] != page)
    {
        _playlist_release_page(state, slot);
    }
    if (state->pages[slot] != NULL)
    {
        music_service_playlist_release(state->pages[slot]);
    }
    state->pages[slot] = snapshot;
    state->page_ids[slot] = page;
    if (new_page && page == state->loaded_pages)
    {
        state->loaded_rows += snapshot->count;
        ++state->loaded_pages;
    }
    if (page == 0U)
    {
        state->info = snapshot->playlist;
        state->total = snapshot->total != 0U ? snapshot->total :
                       snapshot->count;
    }
    state->page_count = music_ui_playlist_page_count(
                            state->total, state->loaded_rows,
                            state->loaded_pages);
    if (page == 0U)
    {
        _playlist_build_hero(state);
    }
    return true;
}

static void _playlist_build_page(music_playlist_state_t *state)
{
    if (state->song_list == NULL)
    {
        return;
    }
    lv_obj_clean(state->song_list);
    const music_playlist_snapshot_t *snapshot =
        _playlist_page_snapshot(state, state->current_page);
    if (snapshot == NULL)
    {
        const uint16_t page_count = state->page_count > 0U ?
                                    state->page_count : 1U;
        if (state->current_page > 0U)
        {
            char subtitle[48];
            (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                           (unsigned)(state->current_page + 1U),
                           (unsigned)page_count);
            (void)app_ui_add_action(state->song_list, LV_SYMBOL_PREV,
                                    "上一页", subtitle,
                                    _playlist_prev_clicked, state);
        }
        if (state->failed)
        {
            (void)_playlist_add_notice(state->song_list, "歌单加载失败",
                                       APP_UI_STATUS_WARNING);
            (void)app_ui_add_action(state->song_list, LV_SYMBOL_REFRESH,
                                    "重新加载", "重试获取这份歌单",
                                    _playlist_retry_clicked, state);
        }
        else
        {
            (void)_playlist_add_notice(state->song_list, "正在加载歌单…",
                                       APP_UI_STATUS_NEUTRAL);
        }
        if (state->current_page + 1U < page_count)
        {
            char subtitle[48];
            (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                           (unsigned)(state->current_page + 2U),
                           (unsigned)page_count);
            (void)app_ui_add_action(state->song_list, LV_SYMBOL_NEXT,
                                    "下一页", subtitle,
                                    _playlist_next_clicked, state);
        }
        return;
    }
    const uint16_t page_count = state->page_count > 0U ?
                                state->page_count : 1U;
    if (state->current_page > 0U)
    {
        char subtitle[48];
        (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                       (unsigned)(state->current_page + 1U),
                       (unsigned)page_count);
        (void)app_ui_add_action(state->song_list, LV_SYMBOL_PREV,
                                "上一页", subtitle,
                                _playlist_prev_clicked, state);
    }
    for (uint16_t index = 0U; index < snapshot->count; ++index)
    {
        const uint32_t global_index = (uint32_t)snapshot->offset + index;
        lv_obj_t *row = music_ui_add_song_row(
                            state->song_list, &snapshot->songs[index],
                            (uint16_t)global_index, _playlist_song_clicked,
                            state);
        if (row != NULL)
        {
            lv_obj_set_user_data(row, (void *)(uintptr_t)(index + 1U));
        }
    }
    if (snapshot->count == 0U)
    {
        (void)_playlist_add_notice(state->song_list,
                                   "这个歌单还没有歌曲",
                                   APP_UI_STATUS_NEUTRAL);
    }
    if (state->current_page + 1U < page_count)
    {
        char subtitle[48];
        (void)snprintf(subtitle, sizeof(subtitle), "第 %u / %u 页",
                       (unsigned)(state->current_page + 2U),
                       (unsigned)page_count);
        (void)app_ui_add_action(state->song_list, LV_SYMBOL_NEXT,
                                "下一页", subtitle,
                                _playlist_next_clicked, state);
    }
    if (snapshot->count > 0U)
    {
        lv_obj_remove_state(state->play_all_button, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->play_all_button, LV_STATE_DISABLED);
    }
}

static void _playlist_retry_clicked(lv_event_t *event)
{
    music_playlist_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    state->failed = false;
    state->loading = false;
    state->loading_since_ms = 0U;
    state->request_pending = false;
    state->pending_page = UINT16_MAX;
    lv_obj_set_hidden(state->status_label, true);
    _playlist_build_page(state);
    _playlist_schedule(state);
}

static bool _playlist_request_page(music_playlist_state_t *state,
                                   uint16_t page)
{
    if (state->request_pending || state->failed)
    {
        return false;
    }
    if (state->page_count > 0U && page >= state->page_count)
    {
        return false;
    }
    if (_playlist_page_snapshot(state, page) != NULL)
    {
        return false;
    }
    const uint16_t offset = _playlist_page_offset(state, page);
    const music_playlist_snapshot_t *snapshot = NULL;
    state->pending_page = page;
    state->request_offset = offset;
    state->request_pending = true;
    state->loading = true;
    state->loading_since_ms = lv_tick_get();
    const esp_err_t result = music_service_playlist_acquire(
                                 &state->ref, offset, MUSIC_PLAYLIST_PAGE_REQUEST_SIZE,
                                 &snapshot);
    if (result == ESP_OK && !_playlist_snapshot_complete(snapshot, offset))
    {
        music_service_playlist_release(snapshot);
        snapshot = NULL;
    }
    if (result == ESP_OK && snapshot != NULL)
    {
        state->request_pending = false;
        state->pending_page = UINT16_MAX;
        state->loading = false;
        state->failed = false;
        _playlist_store_page(state, page,
                             (music_playlist_snapshot_t *)snapshot);
        _playlist_refresh_after_store(state, page);
        return true;
    }
    if (result == ESP_ERR_NOT_FOUND)
    {
        return true;
    }
    state->request_pending = false;
    state->pending_page = UINT16_MAX;
    state->loading = false;
    if (page == state->current_page)
    {
        state->failed = true;
        app_ui_set_status_text(state->status_label,
                               result == ESP_ERR_TIMEOUT ?
                               "加载超时，请重试" : "歌单加载失败",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        _playlist_build_page(state);
    }
    return false;
}

static void _playlist_collect_pending(music_playlist_state_t *state)
{
    if (!state->request_pending || state->pending_page == UINT16_MAX)
    {
        return;
    }
    const uint16_t page = state->pending_page;
    const uint16_t offset = state->request_offset;
    const music_playlist_snapshot_t *snapshot = NULL;
    const esp_err_t result = music_service_playlist_acquire(
                                 &state->ref, offset, MUSIC_PLAYLIST_PAGE_REQUEST_SIZE,
                                 &snapshot);
    if (result == ESP_OK && !_playlist_snapshot_complete(snapshot, offset))
    {
        music_service_playlist_release(snapshot);
        snapshot = NULL;
    }
    if (result == ESP_OK && snapshot != NULL)
    {
        state->request_pending = false;
        state->pending_page = UINT16_MAX;
        state->loading = false;
        state->failed = false;
        _playlist_store_page(state, page,
                             (music_playlist_snapshot_t *)snapshot);
        _playlist_refresh_after_store(state, page);
        return;
    }
    if (result != ESP_ERR_NOT_FOUND)
    {
        state->request_pending = false;
        state->pending_page = UINT16_MAX;
        state->loading = false;
        if (page == state->current_page)
        {
            state->failed = true;
            app_ui_set_status_text(state->status_label, "歌单加载失败",
                                   APP_UI_STATUS_WARNING);
            lv_obj_set_hidden(state->status_label, false);
            _playlist_build_page(state);
        }
    }
}

static void _playlist_schedule(music_playlist_state_t *state)
{
    if (state == NULL || state->request_pending || state->failed)
    {
        return;
    }
    if (state->page_count == 0U)
    {
        if (_playlist_page_snapshot(state, 0U) == NULL)
        {
            (void)_playlist_request_page(state, 0U);
        }
        return;
    }
    static const int8_t deltas[MUSIC_PLAYLIST_PAGE_CACHE] =
    {
        0, -1, 1, -2, 2
    };
    for (size_t index = 0U;
            index < sizeof(deltas) / sizeof(deltas[0]); ++index)
    {
        const int32_t page = (int32_t)state->current_page + deltas[index];
        if (page < 0 || page >= (int32_t)state->page_count)
        {
            continue;
        }
        if (_playlist_page_snapshot(state, (uint16_t)page) != NULL)
        {
            continue;
        }
        if (_playlist_request_page(state, (uint16_t)page))
        {
            return;
        }
        if (state->request_pending || state->failed)
        {
            return;
        }
    }
}

static void _playlist_timer_cb(lv_timer_t *timer)
{
    music_playlist_state_t *state = lv_timer_get_user_data(timer);
    if (state->request_pending &&
            lv_tick_get() - state->loading_since_ms >= 10000U)
    {
        state->request_pending = false;
        state->pending_page = UINT16_MAX;
        state->loading = false;
        state->failed = true;
        app_ui_set_status_text(state->status_label, "加载超时，请重试",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        _playlist_build_page(state);
    }
    _playlist_update_cover(state);
    _playlist_schedule(state);
}

static void _playlist_event_cb(event_bus_msg_id_t msg_id, uint32_t sub_type,
                               const void *payload, size_t payload_size,
                               void *user_data)
{
    (void)msg_id;
    music_playlist_state_t *state = user_data;
    if (state == NULL)
    {
        return;
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_IMAGE &&
            payload != NULL && payload_size == sizeof(music_service_event_t))
    {
        const music_service_event_t *event = payload;
        if (event->token == state->cover_token)
        {
            state->cover_requested = false;
            state->cover_wait_ticks = 0U;
            if (event->failure == MUSIC_SERVICE_FAILURE_NONE)
            {
                state->cover_failed = false;
                state->cover_attempts = 0U;
                (void)music_ui_cover_accept_event(
                    &state->cover, state->cover_token,
                    state->info.cover_url, event);
            }
            else
            {
                state->cover_failed = state->cover_attempts >=
                                      MUSIC_PLAYLIST_COVER_MAX_ATTEMPTS;
            }
        }
    }
    if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_PLAYLIST)
    {
        const music_service_event_t *event =
            payload != NULL && payload_size == sizeof(music_service_event_t) ?
            payload : NULL;
        if (state->request_pending && event != NULL &&
                event->failure != MUSIC_SERVICE_FAILURE_NONE)
        {
            const uint16_t failed_page = state->pending_page;
            state->request_pending = false;
            state->pending_page = UINT16_MAX;
            state->loading = false;
            state->failed = true;
            if (failed_page == state->current_page)
            {
                app_ui_set_status_text(state->status_label, "歌单加载失败",
                                       APP_UI_STATUS_WARNING);
                lv_obj_set_hidden(state->status_label, false);
                _playlist_build_page(state);
            }
        }
        else if (state->request_pending)
        {
            _playlist_collect_pending(state);
        }
        _playlist_schedule(state);
    }
    else if (sub_type == MUSIC_SERVICE_MSG_SUB_TYPE_STATUS &&
             _playlist_page_snapshot(state, 0U) == NULL && !state->failed)
    {
        _playlist_schedule(state);
    }
    _playlist_update_cover(state);
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

static lv_obj_t *_playlist_build_hero_area(music_playlist_state_t *state)
{
    lv_obj_t *hero = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(hero);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, APP_UI_GAP, 0);

    music_ui_cover_create(&state->cover, hero, LV_SYMBOL_AUDIO,
                          MUSIC_PLAYLIST_COVER_SIZE);
    lv_obj_t *details = lv_obj_create(hero);
    lv_obj_remove_style_all(details);
    lv_obj_set_width(details, 0);
    lv_obj_set_flex_grow(details, 1);
    lv_obj_set_height(details, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details, 4, 0);
    app_ui_make_passive(details, false);
    state->details = details;
    state->title_label = app_ui_add_section(details, "歌单");
    lv_obj_set_width(state->title_label, LV_PCT(100));
    lv_obj_set_height(state->title_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(state->title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(state->title_label, 0, 0);
    lv_obj_set_style_text_font(state->title_label,
                               app_ui_font(APP_THEME_FONT_BIGL), 0);
    lv_obj_set_style_text_color(state->title_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    state->creator_label = app_ui_add_body_label(details, "QQ 音乐");
    lv_obj_set_style_text_font(state->creator_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    state->track_count_label = app_ui_add_body_label(details, "共 0 首");
    lv_obj_set_style_text_font(state->track_count_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    app_ui_set_status_text(state->track_count_label, "共 0 首",
                           APP_UI_STATUS_ACCENT);
    state->status_label = app_ui_add_body_label(details, "");
    lv_obj_set_style_text_font(state->status_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_hidden(state->status_label, true);
    lv_obj_set_hidden(details, true);
    return hero;
}

static void _playlist_mount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    state->pending_page = UINT16_MAX;
    for (size_t slot = 0U; slot < MUSIC_PLAYLIST_PAGE_CACHE; ++slot)
    {
        state->page_ids[slot] = UINT16_MAX;
    }
    _playlist_copy_arguments(state, context->arguments);
    app_ui_page_create(&state->page, "歌单", true);
    music_ui_add_header_back(&state->page);
    lv_obj_set_style_pad_top(state->page.content, 12, 0);
    lv_obj_set_style_pad_row(state->page.content, 12, 0);
    state->play_all_button = music_ui_icon_button(
                                 state->page.header, APP_IMAGE_MUSIC_PLAY,
                                 LV_SYMBOL_PLAY, MUSIC_UI_CONTROL_SIZE,
                                 _playlist_play_all_clicked, state);
    if (state->play_all_button != NULL)
    {
        lv_obj_set_ignore_layout(state->play_all_button, true);
        lv_obj_align(state->play_all_button, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
        lv_obj_set_style_bg_color(state->play_all_button,
                                  lv_color_hex(APP_UI_COLOR_RAIN), 0);
        music_ui_set_icon_button(state->play_all_button,
                                 APP_IMAGE_MUSIC_PLAY, LV_SYMBOL_PLAY,
                                 APP_UI_COLOR_BACKGROUND);
        lv_obj_add_state(state->play_all_button, LV_STATE_DISABLED);
    }
    state->hero = _playlist_build_hero_area(state);
    state->song_list = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->song_list);
    lv_obj_set_width(state->song_list, LV_PCT(100));
    lv_obj_set_height(state->song_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state->song_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->song_list, 4, 0);
    lv_obj_set_scroll_dir(state->song_list, LV_DIR_NONE);
    app_ui_make_passive(state->song_list, false);
    _playlist_build_page(state);
}

static void _playlist_resume(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
    (void)event_bus_subscribe(MUSIC_SERVICE_MSG, EVENT_BUS_SUB_TYPE_ANY,
                              _playlist_event_cb, state,
                              EVENT_BUS_DISPATCH_UI, &state->subscription);
    state->timer = lv_timer_create(_playlist_timer_cb, 500U, state);
    if (!music_ui_playlist_ref_valid(&state->ref))
    {
        state->failed = true;
        app_ui_set_status_text(state->status_label, "歌单信息无效",
                               APP_UI_STATUS_WARNING);
        lv_obj_set_hidden(state->status_label, false);
        _playlist_build_page(state);
        return;
    }
    _playlist_update_cover(state);
    _playlist_schedule(state);
}

static void _playlist_new_intent(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    music_playlist_ref_t ref;
    music_playlist_state_t temporary;
    memset(&temporary, 0, sizeof(temporary));
    _playlist_copy_arguments(&temporary, context->arguments);
    ref = temporary.ref;
    if (!music_ui_playlist_ref_valid(&ref))
    {
        return;
    }
    const bool same_ref =
        state->ref.id == ref.id &&
        state->ref.dirid == ref.dirid &&
        state->ref.tid == ref.tid &&
        state->ref.kind == ref.kind;
    if (same_ref)
    {
        return;
    }
    music_ui_cover_release(&state->cover);
    _playlist_release_all_pages(state);
    state->ref = ref;
    state->current_page = 0U;
    state->page_count = 0U;
    state->pending_page = UINT16_MAX;
    state->total = 0U;
    state->loading = false;
    state->failed = false;
    state->request_pending = false;
    state->loading_since_ms = 0U;
    memset(&state->info, 0, sizeof(state->info));
    if (state->page.root != NULL)
    {
        lv_obj_set_hidden(state->details, true);
        _playlist_build_page(state);
        _playlist_schedule(state);
    }
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
    music_ui_cover_release(&state->cover);
    _playlist_release_all_pages(state);
    state->current_page = 0U;
    state->page_count = 0U;
    state->total = 0U;
    memset(&state->info, 0, sizeof(state->info));
    state->failed = false;
    state->loading = false;
    state->loading_since_ms = 0U;
    state->request_pending = false;
    state->pending_page = UINT16_MAX;
    state->cover_requested = false;
    state->cover_wait_ticks = 0U;
    state->cover_attempts = 0U;
    return ESP_OK;
}

static void _playlist_unmount(const app_manager_page_context_t *context)
{
    music_playlist_state_t *state = context->state;
    music_ui_cover_release(&state->cover);
    _playlist_release_all_pages(state);
    app_ui_page_destroy(&state->page);
    memset(state, 0, sizeof(*state));
    state->subscription = EVENT_BUS_SUB_HANDLE_INVALID;
}

static const app_manager_page_ops_t s_playlist_ops =
{
    .mount = _playlist_mount,
    .resume = _playlist_resume,
    .pause = _playlist_pause,
    .unmount = _playlist_unmount,
    .new_intent = _playlist_new_intent,
};

const app_manager_page_definition_t music_playlist_page_definition =
{
    .ops = &s_playlist_ops,
    .memory_size = sizeof(music_playlist_state_t),
};
