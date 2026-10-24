/* Host coverage for the file manager pages and the Settings storage page.
 *
 * The fake LVGL model mirrors settings_factory_reset_page_test.c: objects are
 * slots, events are dispatched directly, and timers are stepped by the test.
 * The file service is a scripted stand-in so the page's request/result
 * handshake is exercised without the real worker. */

#include "settings_app_internal.h"

#include "app_manager.h"
#include "app_ui.h"
#include "file_service.h"
#include "files_app_internal.h"
#include "recorder_app_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_OBJECT_CAPACITY 256U
#define TEST_LABEL_BYTES     256U
#define TEST_TIMER_CAPACITY  8U
#define TEST_SESSION_COUNT   4U
#define TEST_SERVICE_PATH    "/sdcard"

struct lv_obj_t
{
    bool live;
    bool button;
    bool label;
    lv_obj_t *parent;
    lv_event_cb_t callback;
    void *callback_user_data;
    lv_event_code_t callback_code;
    void *user_data;
    uint32_t state;
    uint32_t flags;
    char text[TEST_LABEL_BYTES];
};

struct lv_event_t
{
    lv_event_code_t code;
    void *user_data;
    lv_obj_t *target;
};

typedef union test_page_state
{
    max_align_t alignment;
    uint8_t bytes[APP_MANAGER_PAGE_STATE_BYTES];
} test_page_state_t;

static lv_obj_t s_screen;
static lv_obj_t s_objects[TEST_OBJECT_CAPACITY];
/* One instance per fake timer handle created by the LVGL fake header. */
typedef struct test_timer
{
    bool live;
    bool paused;
    void (*callback)(lv_timer_t *timer);
    void *user_data;
    uint32_t period_ms;
} test_timer_t;

static test_timer_t s_timers[TEST_TIMER_CAPACITY];

/* ── App helpers (compiled from files_app.c without the descriptor) ────── */

#include "../../files_app/src/files_app.c"

/* ── Fake file service ─────────────────────────────────────────────────── */

typedef struct fake_session
{
    bool open;
    file_service_result_t result;
    file_service_batch_t batch;
    file_service_detail_t detail;
    bool has_batch;
    bool has_detail;
} fake_session_t;

static fake_session_t s_sessions[TEST_SESSION_COUNT];
static uint32_t s_next_operation;
static file_service_snapshot_t s_snapshot =
{
    .volume = FILE_SERVICE_VOLUME_READY,
    .space = {.valid = true, .total_bytes = 8ULL << 30, .free_bytes = 4ULL << 30},
    .generation = 3U,
    .revision = 7U,
};
static file_service_storage_action_t s_last_storage_action;
static unsigned s_storage_requests;
static uint32_t s_storage_generation;
static file_service_target_t s_delete_target;
static unsigned s_delete_requests;
static file_service_error_reason_t s_delete_reason;

static bool _fake_auto_complete = true;
/* Scripted answer installed by a listing request; session_open clears the
 * session record, so the fixture lives outside it. */
static file_service_batch_t s_scripted_batch;
static bool s_scripted_batch_valid;
static file_service_error_reason_t s_list_reason;

static fake_session_t *_session(uint32_t id)
{
    if (id == 0U || id > TEST_SESSION_COUNT)
    {
        return NULL;
    }
    return &s_sessions[id - 1U];
}

esp_err_t file_service_session_open(uint32_t *session)
{
    for (uint32_t index = 0U; index < TEST_SESSION_COUNT; ++index)
    {
        if (!s_sessions[index].open)
        {
            memset(&s_sessions[index], 0, sizeof(s_sessions[index]));
            s_sessions[index].open = true;
            *session = index + 1U;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t file_service_session_close(uint32_t session)
{
    fake_session_t *record = _session(session);
    if (record == NULL || !record->open)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(record, 0, sizeof(*record));
    return ESP_OK;
}

static esp_err_t _fake_submit(uint32_t session, file_service_result_t *result,
                              file_service_result_state_t terminal,
                              esp_err_t error)
{
    fake_session_t *record = _session(session);
    if (record == NULL || !record->open)
    {
        return ESP_ERR_INVALID_ARG;
    }
    record->result = (file_service_result_t)
    {
        .state = _fake_auto_complete ? terminal : FILE_SERVICE_RESULT_ACCEPTED,
        .operation_id = ++s_next_operation,
        .session = session,
        .generation = s_snapshot.generation,
        .revision = s_snapshot.revision,
        .error = _fake_auto_complete ? error : ESP_OK,
    };
    if (result != NULL)
    {
        *result = record->result;
    }
    return ESP_OK;
}

esp_err_t file_service_session_list(uint32_t session, const char *path,
                                    const file_service_cursor_t *cursor,
                                    file_service_result_t *result)
{
    (void)path;
    (void)cursor;
    if (s_list_reason != FILE_SERVICE_ERROR_NONE ||
            s_snapshot.volume == FILE_SERVICE_VOLUME_UNAVAILABLE ||
            s_snapshot.volume == FILE_SERVICE_VOLUME_MAINTENANCE)
    {
        if (result != NULL)
        {
            *result = (file_service_result_t)
            {
                .state = FILE_SERVICE_RESULT_FAILED,
                .error = ESP_ERR_INVALID_STATE,
                .reason = s_list_reason != FILE_SERVICE_ERROR_NONE ? s_list_reason :
                          s_snapshot.faulted ? FILE_SERVICE_ERROR_IO :
                          s_snapshot.volume == FILE_SERVICE_VOLUME_UNAVAILABLE ?
                          FILE_SERVICE_ERROR_UNAVAILABLE : FILE_SERVICE_ERROR_BUSY,
            };
        }
        return ESP_ERR_INVALID_STATE;
    }
    fake_session_t *record = _session(session);
    if (record != NULL && s_scripted_batch_valid)
    {
        record->batch = s_scripted_batch;
        record->batch.generation = s_snapshot.generation;
        record->batch.revision = s_snapshot.revision;
        record->has_batch = true;
    }
    return _fake_submit(session, result, FILE_SERVICE_RESULT_DONE, ESP_OK);
}

esp_err_t file_service_session_detail(uint32_t session, const file_service_target_t *target,
                                      file_service_result_t *result)
{
    fake_session_t *record = _session(session);
    if (record != NULL)
    {
        record->detail = (file_service_detail_t)
        {
            0
        };
        const char *name = strrchr(target->relative_path, '/');
        name = name == NULL ? target->relative_path : name + 1;
        assert(strlen(name) < sizeof(record->detail.name));
        strcpy(record->detail.name, name);
        (void)snprintf(record->detail.relative_path,
                       sizeof(record->detail.relative_path), "%s", target->relative_path);
        record->detail.generation = target->generation;
        record->detail.revision = target->revision;
        record->has_detail = true;
    }
    return _fake_submit(session, result, FILE_SERVICE_RESULT_DONE, ESP_OK);
}

esp_err_t file_service_session_delete(uint32_t session, const file_service_target_t *target,
                                      file_service_result_t *result)
{
    ++s_delete_requests;
    s_delete_target = *target;
    esp_err_t accepted = _fake_submit(session, result,
                                      s_delete_reason == FILE_SERVICE_ERROR_NONE ?
                                      FILE_SERVICE_RESULT_DONE : FILE_SERVICE_RESULT_FAILED,
                                      s_delete_reason == FILE_SERVICE_ERROR_NONE ? ESP_OK : ESP_FAIL);
    _session(session)->result.reason = s_delete_reason;
    return accepted;
}

esp_err_t file_service_session_cancel(uint32_t session)
{
    return _session(session) != NULL ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t file_service_session_get_result(uint32_t session,
        file_service_result_t *result)
{
    fake_session_t *record = _session(session);
    if (record == NULL || !record->open || result == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *result = record->result;
    return ESP_OK;
}

esp_err_t file_service_session_get_batch(uint32_t session,
        file_service_batch_t *batch)
{
    fake_session_t *record = _session(session);
    if (record == NULL || !record->has_batch)
    {
        return ESP_ERR_NOT_FOUND;
    }
    *batch = record->batch;
    return ESP_OK;
}

esp_err_t file_service_session_get_detail(uint32_t session,
        file_service_detail_t *detail)
{
    fake_session_t *record = _session(session);
    if (record == NULL || !record->has_detail)
    {
        return ESP_ERR_NOT_FOUND;
    }
    *detail = record->detail;
    return ESP_OK;
}

esp_err_t file_service_session_get_cursor(uint32_t session,
        file_service_cursor_t *cursor)
{
    if (_session(session) == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *cursor = (file_service_cursor_t)
    {
        .valid = false,
    };
    return ESP_OK;
}

esp_err_t file_service_get_snapshot(file_service_snapshot_t *snapshot)
{
    *snapshot = s_snapshot;
    return ESP_OK;
}

esp_err_t file_service_request_storage(file_service_storage_action_t action,
                                       uint32_t expected_generation, file_service_result_t *result)
{
    ++s_storage_requests;
    s_last_storage_action = action;
    s_storage_generation = expected_generation;
    s_snapshot.storage = (file_service_result_t)
    {
        .state = FILE_SERVICE_RESULT_ACCEPTED,
        .operation_id = ++s_next_operation,
        .generation = s_snapshot.generation,
        .revision = s_snapshot.revision,
        .error = ESP_OK,
    };
    if (result != NULL)
    {
        *result = s_snapshot.storage;
    }
    return ESP_OK;
}

/* ── Fake LVGL ─────────────────────────────────────────────────────────── */

static lv_obj_t *_test_object_create(lv_obj_t *parent, bool button,
                                     bool label)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (!s_objects[index].live)
        {
            s_objects[index] = (lv_obj_t)
            {
                .live = true,
                .button = button,
                .label = label,
                .flags = label ? 0U : LV_OBJ_FLAG_CLICKABLE,
                .parent = parent,
            };
            return &s_objects[index];
        }
    }
    assert(false);
    return NULL;
}

static lv_obj_t *_test_find_live_label(const char *text)
{
    for (size_t index = TEST_OBJECT_CAPACITY; index > 0U; --index)
    {
        lv_obj_t *object = &s_objects[index - 1U];
        if (!object->live || !object->label || strcmp(object->text, text) != 0)
        {
            continue;
        }
        bool visible = true;
        for (lv_obj_t *ancestor = object; ancestor != NULL; ancestor = ancestor->parent)
        {
            if ((ancestor->flags & LV_OBJ_FLAG_HIDDEN) != 0U)
            {
                visible = false;
            }
        }
        if (visible)
        {
            return object;
        }
    }
    return NULL;
}

static bool _test_has_live_label_containing(const char *text)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].label &&
                strstr(s_objects[index].text, text) != NULL)
        {
            return true;
        }
    }
    return false;
}

static lv_obj_t *_test_find_button_for_label(const char *text)
{
    lv_obj_t *label = _test_find_live_label(text);
    if (label == NULL)
    {
        return NULL;
    }
    for (lv_obj_t *object = label->parent; object != NULL;
            object = object->parent)
    {
        if (object->button)
        {
            return object;
        }
    }
    return NULL;
}

static void _test_dispatch(lv_obj_t *object, lv_event_code_t code)
{
    lv_event_t event =
    {
        .code = code,
        .user_data = object->callback_user_data,
        .target = object,
    };
    object->callback(&event);
}

static void _test_click(lv_obj_t *button)
{
    assert(button != NULL);
    assert(button->live);
    assert(button->callback != NULL);
    if (lv_obj_has_state(button, LV_STATE_DISABLED))
    {
        return;
    }
    /* LV_EVENT_ALL handlers must receive the concrete code, exactly as LVGL
     * delivers it; a single-code handler receives its own registration. */
    _test_dispatch(button, button->callback_code == LV_EVENT_ALL ?
                   LV_EVENT_CLICKED : button->callback_code);
}

/* LVGL sends LONG_PRESSED while the finger is down and still sends CLICKED on
 * release (short-click is suppressed). Reproduce that sequence. */
static void _test_long_press(lv_obj_t *object)
{
    assert(object != NULL);
    assert(object->live);
    assert(object->callback != NULL);
    if (lv_obj_has_state(object, LV_STATE_DISABLED))
    {
        return;
    }
    _test_dispatch(object, LV_EVENT_PRESSED);
    _test_dispatch(object, LV_EVENT_LONG_PRESSED);
    _test_dispatch(object, LV_EVENT_CLICKED);
}

static void _test_click_label(const char *text)
{
    lv_obj_t *button = _test_find_button_for_label(text);
    assert(button != NULL);
    _test_click(button);
}

static test_timer_t *_test_live_timer(void)
{
    for (size_t index = 0U; index < TEST_TIMER_CAPACITY; ++index)
    {
        if (s_timers[index].live)
        {
            return &s_timers[index];
        }
    }
    return NULL;
}

static void _test_step_timers(void)
{
    for (size_t index = 0U; index < TEST_TIMER_CAPACITY; ++index)
    {
        if (s_timers[index].live && !s_timers[index].paused)
        {
            s_timers[index].callback((lv_timer_t *)&s_timers[index]);
        }
    }
}

lv_obj_t *lv_bar_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_button_create(lv_obj_t *parent)
{
    return _test_object_create(parent, true, false);
}

lv_obj_t *lv_label_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, true);
}

lv_obj_t *lv_arc_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_switch_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_image_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

void lv_image_set_src(lv_obj_t *object, const lv_image_dsc_t *source)
{
    (void)object;
    (void)source;
}

void lv_arc_set_bg_angles(lv_obj_t *object, int32_t start, int32_t end)
{
    (void)object;
    (void)start;
    (void)end;
}

void lv_arc_set_angles(lv_obj_t *object, int32_t start, int32_t end)
{
    (void)object;
    (void)start;
    (void)end;
}

void lv_arc_set_rotation(lv_obj_t *object, int32_t rotation)
{
    (void)object;
    (void)rotation;
}

void lv_obj_remove_style(lv_obj_t *object, void *style, int selector)
{
    (void)object;
    (void)style;
    (void)selector;
}

lv_obj_t *lv_obj_get_child(lv_obj_t *object, int32_t index)
{
    int32_t seen = 0;
    for (size_t scan = 0U; scan < TEST_OBJECT_CAPACITY; ++scan)
    {
        if (s_objects[scan].live && s_objects[scan].parent == object)
        {
            if (seen == index)
            {
                return &s_objects[scan];
            }
            ++seen;
        }
    }
    return NULL;
}

uint32_t lv_obj_get_child_count(const lv_obj_t *object)
{
    uint32_t count = 0U;
    for (size_t scan = 0U; scan < TEST_OBJECT_CAPACITY; ++scan)
    {
        if (s_objects[scan].live && s_objects[scan].parent == object)
        {
            ++count;
        }
    }
    return count;
}

void lv_obj_delete(lv_obj_t *object)
{
    if (object == NULL)
    {
        return;
    }
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].parent == object)
        {
            lv_obj_delete(&s_objects[index]);
        }
    }
    object->live = false;
}

void lv_obj_clean(lv_obj_t *object)
{
    lv_obj_delete(object);
    object->live = true;
}

void lv_obj_add_event_cb(lv_obj_t *object, lv_event_cb_t callback,
                         lv_event_code_t code, void *user_data)
{
    assert(object != NULL);
    object->callback = callback;
    object->callback_code = code;
    object->callback_user_data = user_data;
}

void lv_obj_add_state(lv_obj_t *object, uint32_t state)
{
    assert(object != NULL);
    object->state |= state;
}

void lv_obj_remove_state(lv_obj_t *object, uint32_t state)
{
    assert(object != NULL);
    object->state &= ~state;
}

bool lv_obj_has_state(const lv_obj_t *object, uint32_t state)
{
    return object != NULL && (object->state & state) != 0U;
}

bool lv_obj_is_valid(const lv_obj_t *object)
{
    return object != NULL && object->live;
}

void lv_obj_add_flag(lv_obj_t *object, uint32_t flag)
{
    assert(object != NULL);
    object->flags |= flag;
}

void lv_obj_remove_flag(lv_obj_t *object, uint32_t flag)
{
    assert(object != NULL);
    object->flags &= ~flag;
}

void lv_obj_set_user_data(lv_obj_t *object, void *user_data)
{
    assert(object != NULL);
    object->user_data = user_data;
}

void *lv_obj_get_user_data(lv_obj_t *object)
{
    return object == NULL ? NULL : object->user_data;
}

lv_event_code_t lv_event_get_code(lv_event_t *event)
{
    assert(event != NULL);
    return event->code;
}

void *lv_event_get_user_data(lv_event_t *event)
{
    assert(event != NULL);
    return event->user_data;
}

lv_obj_t *lv_event_get_target(lv_event_t *event)
{
    assert(event != NULL);
    return event->target;
}

lv_obj_t *lv_event_get_current_target(lv_event_t *event)
{
    return lv_event_get_target(event);
}

lv_result_t lv_obj_send_event(lv_obj_t *object, lv_event_code_t code,
                              void *param)
{
    (void)param;
    assert(object != NULL);
    if (object->callback != NULL &&
            (object->callback_code == LV_EVENT_ALL || object->callback_code == code))
    {
        lv_event_t event =
        {
            .code = code,
            .user_data = object->callback_user_data,
            .target = object,
        };
        object->callback(&event);
    }
    return LV_RESULT_OK;
}

void lv_label_set_text(lv_obj_t *label, const char *text)
{
    assert(label != NULL);
    assert(label->label);
    (void)snprintf(label->text, sizeof(label->text), "%s", text);
}

const char *lv_label_get_text(const lv_obj_t *label)
{
    assert(label != NULL);
    return label->text;
}

void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font,
                                int selector)
{
    (void)object;
    (void)font;
    (void)selector;
}

void lv_obj_update_layout(lv_obj_t *object)
{
    (void)object;
}

int32_t lv_obj_get_content_width(const lv_obj_t *object)
{
    (void)object;
    return 320;
}

int32_t lv_font_get_glyph_width(const lv_font_t *font, uint32_t letter,
                                uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    return 12;
}

const lv_font_t *lv_obj_get_style_text_font(const lv_obj_t *object,
        int selector)
{
    (void)object;
    (void)selector;
    return LV_FONT_DEFAULT;
}

int32_t lv_obj_get_style_text_letter_space(const lv_obj_t *object,
        int selector)
{
    (void)object;
    (void)selector;
    return 0;
}

lv_timer_t *lv_timer_create(void (*callback)(lv_timer_t *timer),
                            uint32_t period_ms, void *user_data)
{
    for (size_t index = 0U; index < TEST_TIMER_CAPACITY; ++index)
    {
        if (!s_timers[index].live)
        {
            s_timers[index].live = true;
            s_timers[index].paused = false;
            s_timers[index].callback = callback;
            s_timers[index].user_data = user_data;
            s_timers[index].period_ms = period_ms;
            return (lv_timer_t *)&s_timers[index];
        }
    }
    assert(false);
    return NULL;
}

void lv_timer_delete(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        ((test_timer_t *)timer)->live = false;
    }
}

void lv_timer_pause(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        ((test_timer_t *)timer)->paused = true;
    }
}

void lv_timer_resume(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        ((test_timer_t *)timer)->paused = false;
    }
}

void lv_timer_step(lv_timer_t *timer)
{
    test_timer_t *record = (test_timer_t *)timer;
    if (record != NULL && record->live && !record->paused)
    {
        record->callback(timer);
    }
}

void *lv_timer_get_user_data(lv_timer_t *timer)
{
    return timer == NULL ? NULL : ((test_timer_t *)timer)->user_data;
}

void lv_timer_reset_all(void)
{
    for (size_t index = 0U; index < TEST_TIMER_CAPACITY; ++index)
    {
        s_timers[index].live = false;
    }
}

lv_obj_t *app_manager_this_page_screen(void)
{
    return &s_screen;
}

const lv_font_t *app_manager_get_font(app_theme_font_id_t id)
{
    (void)id;
    return LV_FONT_DEFAULT;
}

esp_err_t app_manager_get_image(uint32_t semantic_id,
                                const lv_image_dsc_t **image)
{
    (void)semantic_id;
    if (image != NULL)
    {
        *image = NULL;
    }
    return ESP_OK;
}

typedef struct fake_nav
{
    unsigned calls;
    app_manager_nav_request_t last;
} fake_nav_t;

static fake_nav_t s_nav;

esp_err_t app_manager_navigate_async(
    const app_manager_nav_request_t *request,
    app_manager_nav_completion_cb_t completion, void *context)
{
    assert(request != NULL);
    ++s_nav.calls;
    s_nav.last = *request;
    if (completion != NULL)
    {
        completion(ESP_OK, context);
    }
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

esp_err_t power_service_get_snapshot(power_service_snapshot_t *snapshot)
{
    *snapshot = (power_service_snapshot_t)
    {
        .valid = false
    };
    return ESP_OK;
}

esp_err_t app_manager_pm_request_screen_off(void)
{
    return ESP_OK;
}

static recorder_service_snapshot_t s_recorder_snapshot;
static recorder_service_file_t s_recorder_row;
/* Extra rows the fake publishes after the primary one, so a batch can span
 * more than a single recording. */
static recorder_service_file_t s_recorder_extra[3];
static size_t s_recorder_extra_count;
static unsigned s_recorder_deleted_names;
static char s_recorder_last_deleted[RECORDER_SERVICE_PATH_BYTES];
static unsigned s_recorder_deletes;
static unsigned s_recorder_stops;
static unsigned s_recorder_plays;
static recorder_service_target_t s_recorder_deleted_target;
static unsigned s_recorder_starts;
static unsigned s_recorder_pauses;
static unsigned s_recorder_resumes;
static esp_err_t s_recorder_start_error;
static unsigned s_recorder_refreshes;

esp_err_t recorder_service_refresh(void)
{
    ++s_recorder_refreshes;
    return ESP_OK;
}

esp_err_t recorder_service_start(void)
{
    ++s_recorder_starts;
    return s_recorder_start_error;
}

esp_err_t recorder_service_pause(void)
{
    ++s_recorder_pauses;
    return ESP_OK;
}

esp_err_t recorder_service_resume(void)
{
    ++s_recorder_resumes;
    return ESP_OK;
}

esp_err_t recorder_service_stop(void)
{
    ++s_recorder_stops;
    return ESP_OK;
}

esp_err_t recorder_service_get_snapshot(recorder_service_snapshot_t *snapshot)
{
    *snapshot = s_recorder_snapshot;
    return ESP_OK;
}

esp_err_t recorder_service_list(recorder_service_file_t *files,
                                size_t capacity, size_t *count)
{
    assert(capacity > 0U);
    size_t total = 0U;
    if (s_recorder_row.target.name[0] != '\0')
    {
        files[total++] = s_recorder_row;
    }
    for (size_t index = 0U; index < s_recorder_extra_count &&
            total < capacity && total < RECORDER_SERVICE_MAX_FILES; ++index)
    {
        files[total++] = s_recorder_extra[index];
    }
    *count = total;
    s_recorder_snapshot.file_count = total;
    return ESP_OK;
}

esp_err_t recorder_service_play(const recorder_service_target_t *target)
{
    assert(target != NULL);
    ++s_recorder_plays;
    return ESP_OK;
}

esp_err_t recorder_service_stop_playback(void)
{
    ++s_recorder_stops;
    s_recorder_snapshot.operation_pending = true;
    ++s_recorder_snapshot.operation_id;
    ++s_recorder_snapshot.generation;
    return ESP_OK;
}

esp_err_t recorder_service_delete(const recorder_service_target_t *target)
{
    ++s_recorder_deletes;
    ++s_recorder_deleted_names;
    (void)snprintf(s_recorder_last_deleted, sizeof(s_recorder_last_deleted),
                   "%s", target->name);
    s_recorder_deleted_target = *target;
    s_recorder_snapshot.operation_pending = true;
    ++s_recorder_snapshot.operation_id;
    ++s_recorder_snapshot.generation;
    return ESP_OK;
}

/* Out-of-tree page definitions live in the app sources; the two static ones
 * under test are declared through their internal header. */
extern const app_manager_page_definition_t files_browse_page_definition;
extern const app_manager_page_definition_t files_detail_page_definition;

static void _test_reset(void)
{
    memset(&s_screen, 0, sizeof(s_screen));
    s_screen.live = true;
    memset(s_objects, 0, sizeof(s_objects));
    memset(s_sessions, 0, sizeof(s_sessions));
    memset(&s_nav, 0, sizeof(s_nav));
    memset(&s_snapshot.storage, 0, sizeof(s_snapshot.storage));

    s_snapshot.volume = FILE_SERVICE_VOLUME_READY;
    s_snapshot.space.valid = true;
    s_snapshot.busy = false;
    s_snapshot.faulted = false;
    s_snapshot.generation = 3U;
    s_snapshot.revision = 7U;
    s_delete_requests = 0U;
    s_delete_reason = FILE_SERVICE_ERROR_NONE;
    s_list_reason = FILE_SERVICE_ERROR_NONE;
    s_scripted_batch_valid = false;
    files_app_clear_selection();
    memset(&s_scripted_batch, 0, sizeof(s_scripted_batch));
    s_last_storage_action = FILE_SERVICE_STORAGE_MOUNT;
    s_storage_requests = 0U;
    _fake_auto_complete = true;
    lv_timer_reset_all();
}

static app_manager_page_context_t _context(const test_page_state_t *state)
{
    return (app_manager_page_context_t)
    {
        .state = (void *)state->bytes,
        .screen = &s_screen,
    };
}

/* ── Tests ─────────────────────────────────────────────────────────────── */

static void _test_helpers(void)
{
    char parent[64] = "a/b/c.txt";
    files_path_parent(parent);
    assert(strcmp(parent, "a/b") == 0);
    strcpy(parent, "top.txt");
    files_path_parent(parent);
    assert(parent[0] == '\0');

    char joined[64];
    assert(files_path_join("", "中文.txt", joined, sizeof(joined)) == ESP_OK);
    assert(strcmp(joined, "中文.txt") == 0);
    assert(files_path_join("相册", "照片.jpg", joined, sizeof(joined)) == ESP_OK);
    assert(strcmp(joined, "相册/照片.jpg") == 0);
    char tiny[4];
    assert(files_path_join("相册", "照片.jpg", tiny, sizeof(tiny)) ==
           ESP_ERR_INVALID_SIZE);

    char text[32];
    files_format_size(44U, text, sizeof(text));
    assert(strcmp(text, "44 B") == 0);
    files_format_size(2048U, text, sizeof(text));
    assert(strcmp(text, "2.0 KB") == 0);
    /* A missing timestamp is reported as unknown, never as a fake date. */
    files_format_time(false, 0U, text, sizeof(text));
    assert(strcmp(text, "未知") == 0);
}

static void _test_browse_mounts_and_lists(void)
{
    _test_reset();
    s_scripted_batch_valid = true;
    s_scripted_batch.count = 3U;
    (void)snprintf(s_scripted_batch.entries[0].name,
                   sizeof(s_scripted_batch.entries[0].name), "相册");
    s_scripted_batch.entries[0].kind = FILE_SERVICE_ENTRY_DIRECTORY;
    (void)snprintf(s_scripted_batch.entries[1].name,
                   sizeof(s_scripted_batch.entries[1].name),
                   "录音 2026.wav");
    s_scripted_batch.entries[1].size_bytes = 4096U;
    (void)snprintf(s_scripted_batch.entries[2].name,
                   sizeof(s_scripted_batch.entries[2].name), "readme.txt");
    s_scripted_batch.entries[2].size_bytes = 12U;

    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _context(&state);
    files_browse_page_definition.ops->mount(&context);

    /* The page submits one request and renders the finished batch. */
    _test_step_timers();
    assert(_test_has_live_label_containing("相册"));
    assert(_test_has_live_label_containing("录音 2026.wav"));
    assert(_test_has_live_label_containing("readme.txt"));
    assert(_test_has_live_label_containing("/") ||
           _test_has_live_label_containing("存储卡"));

    /* No card: the page reports the state instead of an empty tree. */
    files_browse_page_definition.ops->unmount(&context);
    assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
    _test_reset();
    test_page_state_t second;
    memset(&second, 0, sizeof(second));
    const app_manager_page_context_t second_context = _context(&second);
    files_browse_page_definition.ops->mount(&second_context);
    _test_step_timers();
    assert(_test_live_timer() != NULL);
    files_browse_page_definition.ops->unmount(&second_context);
    assert(files_browse_page_definition.ops->stop(&second_context) == ESP_OK);
    assert(_test_live_timer() == NULL);
}

static void _test_browse_row_opens_detail(void)
{
    _test_reset();
    s_scripted_batch_valid = true;
    s_scripted_batch.count = 2U;
    (void)snprintf(s_scripted_batch.entries[0].name,
                   sizeof(s_scripted_batch.entries[0].name), "相册");
    s_scripted_batch.entries[0].kind = FILE_SERVICE_ENTRY_DIRECTORY;
    (void)snprintf(s_scripted_batch.entries[1].name,
                   sizeof(s_scripted_batch.entries[1].name), "a.txt");
    s_scripted_batch.entries[1].kind = FILE_SERVICE_ENTRY_FILE;

    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _context(&state);
    files_browse_page_definition.ops->mount(&context);
    _test_step_timers();

    /* A file opens its detail page; the request carries the child path. */
    lv_obj_t *file_label = _test_find_live_label("a.txt");
    assert(file_label != NULL);
    lv_obj_t *row = file_label->parent;
    assert(row != NULL && row->button);
    _test_click(row);
    assert(s_nav.calls == 1U);
    assert(strcmp(s_nav.last.page_id, "detail") == 0);
    /* The selection is handed over in App scope: a volume path does not fit
     * the 88-byte Typed Blob payload. */
    file_service_target_t *selected = calloc(1U, sizeof(*selected));
    assert(selected != NULL);
    assert(files_app_copy_selection(selected) == ESP_OK);
    assert(strcmp(selected->relative_path, "a.txt") == 0);
    assert(selected->generation == s_snapshot.generation);
    free(selected);
    assert(!s_nav.last.has_arguments);

    /* A directory navigates in place, restarting the enumeration. */
    lv_obj_t *directory_label = _test_find_live_label("相册");
    assert(directory_label != NULL);
    _test_click(directory_label->parent);
    assert(s_nav.calls == 1U);
    files_browse_page_definition.ops->unmount(&context);
    assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
}

static void _test_detail_reads_and_deletes(void)
{
    _test_reset();
    file_service_target_t *target = calloc(1U, sizeof(*target));
    assert(target != NULL);
    strcpy(target->relative_path, "相册/照片一.jpg");
    target->generation = s_snapshot.generation;
    target->revision = s_snapshot.revision;
    assert(files_app_select(target) == ESP_OK);
    s_sessions[0].detail.kind = FILE_SERVICE_ENTRY_FILE;
    s_sessions[0].detail.size_bytes = 2048U;
    s_sessions[0].detail.has_mtime = true;
    s_sessions[0].detail.modified_epoch_s = 1785888100U;

    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _context(&state);
    files_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing("相册/照片一.jpg"));

    /* Delete is confirmed through a sheet, then the service is asked. */
    lv_obj_t *delete_button = _test_find_button_for_label("删除");
    assert(delete_button != NULL);
    _test_click(delete_button);
    assert(_test_has_live_label_containing("删除文件"));
    _test_click_label("删除");
    _test_step_timers();
    assert(s_delete_requests == 1U);
    assert(strcmp(s_delete_target.relative_path, target->relative_path) == 0);
    assert(s_delete_target.generation == target->generation);
    free(target);
    files_detail_page_definition.ops->unmount(&context);
    assert(files_detail_page_definition.ops->stop(&context) == ESP_OK);
}

static void _test_detail_without_arguments(void)
{
    _test_reset();
    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _context(&state);
    files_detail_page_definition.ops->mount(&context);
    /* No navigation payload: an explicit empty state instead of a crash. */
    assert(_test_has_live_label_containing("无法打开该文件"));
    files_detail_page_definition.ops->unmount(&context);
    assert(files_detail_page_definition.ops->stop(&context) == ESP_OK);
}

static void _test_storage_page_states(void)
{
    _test_reset();
    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _context(&state);
    settings_storage_page_definition.ops->mount(&context);
    assert(_test_has_live_label_containing("已挂载"));
    assert(_test_has_live_label_containing("剩余空间"));

    /* A failed capacity query is unknown, not zero. */
    s_snapshot.space.valid = false;
    _test_step_timers();
    assert(_test_has_live_label_containing("未知"));

    /* Unmounted offers a plain retry and the explicit recovery path. */
    s_snapshot.volume = FILE_SERVICE_VOLUME_UNAVAILABLE;
    _test_step_timers();
    assert(_test_has_live_label_containing("未挂载"));
    _test_click_label("重新挂载");
    assert(s_storage_requests == 1U);
    assert(s_last_storage_action == FILE_SERVICE_STORAGE_MOUNT);

    /* Formatting needs a second confirmation. */
    s_snapshot.volume = FILE_SERVICE_VOLUME_READY;
    s_snapshot.storage.state = FILE_SERVICE_RESULT_DONE;
    _test_step_timers();
    lv_obj_t *format_button = _test_find_button_for_label("格式化存储卡");
    assert(format_button != NULL);
    _test_click(format_button);
    assert(_test_has_live_label_containing("全部用户内容"));
    assert(s_storage_requests == 1U);
    const uint32_t confirmed_generation = s_snapshot.generation;
    ++s_snapshot.generation;
    _test_step_timers();
    _test_click_label("格式化");
    assert(s_storage_generation == confirmed_generation);
    assert(s_storage_requests == 2U);
    assert(s_last_storage_action == FILE_SERVICE_STORAGE_FORMAT);

    /* A session holding the card reports "in use" and blocks the destructive
     * rows. */
    s_snapshot.volume = FILE_SERVICE_VOLUME_BUSY;
    s_snapshot.storage.state = FILE_SERVICE_RESULT_DONE;
    s_snapshot.busy = false;
    _test_step_timers();
    assert(_test_has_live_label_containing("正在使用"));
    lv_obj_t *busy_format = _test_find_button_for_label("格式化存储卡");
    assert(busy_format != NULL);
    assert(lv_obj_has_state(busy_format, LV_STATE_DISABLED));
    lv_obj_t *busy_unmount = _test_find_button_for_label("安全卸载");
    assert(busy_unmount != NULL);
    assert(lv_obj_has_state(busy_unmount, LV_STATE_DISABLED));
    s_snapshot.volume = FILE_SERVICE_VOLUME_UNAVAILABLE;
    s_snapshot.faulted = true;
    _test_step_timers();
    assert(_test_has_live_label_containing("存储故障"));
    settings_storage_page_definition.ops->unmount(&context);
}

static void _test_retains_detail_across_remount(void)
{
    _test_reset();
    file_service_target_t *target = calloc(1U, sizeof(*target));
    assert(target != NULL);
    strcpy(target->relative_path, "相册/保留.txt");
    target->generation = s_snapshot.generation;
    target->revision = s_snapshot.revision;
    assert(files_app_select(target) == ESP_OK);
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    files_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    files_detail_page_definition.ops->unmount(&context);
    files_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing("相册/保留.txt"));
    files_detail_page_definition.ops->unmount(&context);
    strcpy(target->relative_path, "相册/另一个.txt");
    assert(files_app_select(target) == ESP_OK);
    files_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing("相册/另一个.txt"));
    assert(!_test_has_live_label_containing("相册/保留.txt"));
    files_detail_page_definition.ops->unmount(&context);
    assert(files_detail_page_definition.ops->stop(&context) == ESP_OK);
    free(target);
}

static void _test_rows_ignore_pending_refresh(void)
{
    _test_reset();
    s_scripted_batch_valid = true;
    s_scripted_batch.count = 1U;
    s_scripted_batch.generation = s_snapshot.generation;
    s_scripted_batch.revision = s_snapshot.revision;
    strcpy(s_scripted_batch.entries[0].name, "stable.txt");
    s_scripted_batch.entries[0].kind = FILE_SERVICE_ENTRY_FILE;
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    files_browse_page_definition.ops->mount(&context);
    _test_step_timers();
    lv_obj_t *old_row = _test_find_button_for_label("stable.txt");
    assert(old_row != NULL);
    _fake_auto_complete = false;
    _test_click_label("刷新");
    _test_click(old_row);
    assert(s_nav.calls == 0U);
    files_browse_page_definition.ops->unmount(&context);
    assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
}

static void _test_empty_directory_detail_is_reachable(void)
{
    _test_reset();
    s_scripted_batch_valid = true;
    s_scripted_batch.count = 1U;
    s_scripted_batch.generation = s_snapshot.generation;
    s_scripted_batch.revision = s_snapshot.revision;
    strcpy(s_scripted_batch.entries[0].name, "空目录");
    s_scripted_batch.entries[0].kind = FILE_SERVICE_ENTRY_DIRECTORY;
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    files_browse_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_click_label("空目录");
    s_scripted_batch.count = 0U;
    _test_step_timers();
    _test_click_label("当前目录详情");
    assert(s_nav.calls == 1U);
    file_service_target_t *target = calloc(1U, sizeof(*target));
    assert(target != NULL);
    assert(files_app_copy_selection(target) == ESP_OK);
    assert(strcmp(target->relative_path, "空目录") == 0);
    assert(target->generation == s_snapshot.generation);
    files_browse_page_definition.ops->unmount(&context);
    assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
    free(target);
}

static void _test_busy_delete_is_not_nonempty(void)
{
    _test_reset();
    file_service_target_t *target = calloc(1U, sizeof(*target));
    assert(target != NULL);
    strcpy(target->relative_path, "busy.txt");
    target->generation = s_snapshot.generation;
    target->revision = s_snapshot.revision;
    assert(files_app_select(target) == ESP_OK);
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    files_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    s_delete_reason = FILE_SERVICE_ERROR_BUSY;
    _test_click_label("删除");
    _test_click_label("删除");
    _test_step_timers();
    assert(_test_has_live_label_containing("正在使用"));
    assert(!_test_has_live_label_containing("目录非空"));
    files_detail_page_definition.ops->unmount(&context);
    assert(files_detail_page_definition.ops->stop(&context) == ESP_OK);
    free(target);
}

static void _test_failed_navigation_clears_rows(void)
{
    for (unsigned asynchronous = 0U; asynchronous < 2U; ++asynchronous)
    {
        _test_reset();
        s_scripted_batch_valid = true;
        s_scripted_batch.count = 2U;
        strcpy(s_scripted_batch.entries[0].name, "missing_dir");
        s_scripted_batch.entries[0].kind = FILE_SERVICE_ENTRY_DIRECTORY;
        strcpy(s_scripted_batch.entries[1].name, "old_file.txt");
        test_page_state_t state = {0};
        const app_manager_page_context_t context = _context(&state);
        files_browse_page_definition.ops->mount(&context);
        _test_step_timers();
        assert(_test_find_live_label("old_file.txt") != NULL);
        if (asynchronous)
        {
            _fake_auto_complete = false;
        }
        else
        {
            s_list_reason = FILE_SERVICE_ERROR_NOT_FOUND;
        }
        _test_click_label("missing_dir");
        assert(_test_find_live_label("old_file.txt") != NULL);
        if (asynchronous)
        {
            s_sessions[0].result.state = FILE_SERVICE_RESULT_FAILED;
            s_sessions[0].result.reason = FILE_SERVICE_ERROR_NOT_FOUND;
            s_sessions[0].result.error = ESP_ERR_NOT_FOUND;
        }
        _test_step_timers();
        assert(_test_find_live_label("old_file.txt") == NULL);
        assert(_test_find_live_label("missing_dir") != NULL);
        assert(_test_has_live_label_containing("不存在"));
        files_browse_page_definition.ops->unmount(&context);
        assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
    }
}

static void _test_unavailable_and_busy_states(void)
{
    const file_service_error_reason_t reasons[] =
    {
        FILE_SERVICE_ERROR_UNAVAILABLE, FILE_SERVICE_ERROR_BUSY,
    };
    for (size_t index = 0U; index < 2U; ++index)
    {
        _test_reset();
        s_list_reason = reasons[index];
        test_page_state_t state = {0};
        const app_manager_page_context_t context = _context(&state);
        files_browse_page_definition.ops->mount(&context);
        _test_step_timers();
        assert(_test_has_live_label_containing(index == 0U ?
                                               "存储卡未挂载，请前往存储管理重新挂载" : "正在使用，请稍后重试"));
        assert(!_test_has_live_label_containing(index == 0U ?
                                                "正在使用" : "存储卡未挂载"));
        files_browse_page_definition.ops->unmount(&context);
        assert(files_browse_page_definition.ops->stop(&context) == ESP_OK);
    }
    _test_reset();
    s_snapshot.volume = FILE_SERVICE_VOLUME_BUSY;
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    settings_device_page_definition.ops->mount(&context);
    assert(_test_find_live_label("已挂载") != NULL);
    assert(_test_find_live_label("未挂载") == NULL);
    settings_device_page_definition.ops->unmount(&context);
}

/* The row renders a local timestamp, so the expectation must come from the
 * same formatter rather than from a hard-coded time zone. */
static const char *_recorder_time_text(void)
{
    static char text[32];
    recorder_ui_format_time(&s_recorder_row, text, sizeof(text));
    return text;
}

static void _recorder_fixture(void)
{
    _test_reset();
    s_recorder_snapshot = (recorder_service_snapshot_t)
    {
        .generation = 1U, .storage_ready = true,
        .storage_generation = 3U, .content_revision = 7U,
        .index_generation = 3U, .index_revision = 7U, .index_valid = true,
        .file_count = 1U, .max_duration_ms = 1800000U,
        .space_valid = true, .free_bytes = 4096ULL * 1024ULL * 1024ULL,
    };
    s_recorder_row = (recorder_service_file_t)
    {
        .target = {.name = "/sdcard/Recordings/sample.wav",
                   .generation = 3U, .revision = 7U
                  },
        .duration_ms = 2000U, .bytes = 128044U,
        .has_mtime = true, .modified_epoch_s = 1792446000U,
    };
    s_recorder_deletes = 0U;
    s_recorder_stops = 0U;
    s_recorder_plays = 0U;
    s_recorder_starts = 0U;
    s_recorder_pauses = 0U;
    s_recorder_resumes = 0U;
    s_recorder_start_error = ESP_OK;
    s_recorder_refreshes = 0U;
    s_recorder_extra_count = 0U;
    s_recorder_deleted_names = 0U;
    s_recorder_last_deleted[0] = '\0';
}

/* The list is a navigator: a tap hands the row to the detail Page and nothing
 * else. Only a stale identity is refused. */
/* Live fake objects; any change between two frames means the stack rebuilt. */
static size_t _test_live_objects(void)
{
    size_t live = 0U;
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        live += s_objects[index].live ? 1U : 0U;
    }
    return live;
}

/* The root page must answer a rejected start instead of staying on 就绪, and
 * its stack must not gain or lose a slot between states. */
static void _test_recorder_root_reports_failure(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_root_page_definition.ops->mount(&context);
    const size_t baseline = _test_live_objects();
    assert(_test_has_live_label_containing("可用 4096 MB"));
    assert(_test_has_live_label_containing("上限 30:00"));
    assert(_test_has_live_label_containing("就绪"));
    s_recorder_start_error = ESP_ERR_NO_MEM;
    _test_click_label("开始");
    assert(s_recorder_starts == 1U);
    assert(_test_has_live_label_containing(recorder_ui_error_text(ESP_ERR_NO_MEM)));
    assert(!_test_has_live_label_containing("就绪"));

    /* A missing card explains itself and keeps 0 MB off the screen. */
    s_recorder_start_error = ESP_OK;
    s_recorder_snapshot.storage_ready = false;
    s_recorder_snapshot.space_valid = false;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("插入存储卡后可以录音"));
    assert(_test_has_live_label_containing("存储卡未挂载"));
    assert(!_test_has_live_label_containing("空间未知"));
    assert(!_test_has_live_label_containing("可用 0 MB"));
    /* No state creates or destroys a control: the stack keeps its slots. */
    assert(_test_live_objects() == baseline);

    /* A card that changes while the Page is open leaves the published index
     * stale; the count must not keep describing the previous medium. */
    const unsigned refreshes = s_recorder_refreshes;
    ++s_recorder_snapshot.content_revision;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(s_recorder_refreshes > refreshes);
    recorder_root_page_definition.ops->unmount(&context);
}

/* Pausing must not move the controls: the extra slot is always reserved. */
static void _test_recorder_root_pause_and_receipt(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_root_page_definition.ops->mount(&context);
    const size_t baseline = _test_live_objects();
    s_recorder_snapshot.state = RECORDER_SERVICE_RECORDING;
    s_recorder_snapshot.duration_ms = 7000U;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("录音中"));
    assert(_test_has_live_label_containing("剩余 29:53"));
    assert(_test_live_objects() == baseline);

    _test_click_label("暂停");
    assert(s_recorder_pauses == 1U);
    s_recorder_snapshot.state = RECORDER_SERVICE_PAUSED;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("已暂停"));
    assert(_test_find_live_label("继续") != NULL);
    assert(_test_live_objects() == baseline);

    _test_click_label("继续");
    assert(s_recorder_resumes == 1U);
    /* Stopping reports what was kept: the receipt names the saved length. */
    s_recorder_snapshot.state = RECORDER_SERVICE_IDLE;
    s_recorder_snapshot.duration_ms = 7000U;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("已保存 0:07"));
    recorder_root_page_definition.ops->unmount(&context);
}

/* The circle is one control: stop while recording, never pause. */
static void _test_recorder_root_circle_stops(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_root_page_definition.ops->mount(&context);
    assert(_test_find_live_label("开始") != NULL);
    s_recorder_snapshot.state = RECORDER_SERVICE_RECORDING;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_find_live_label("停止") != NULL);
    _test_click_label("停止");
    assert(s_recorder_stops == 1U);
    assert(s_recorder_pauses == 0U);
    recorder_root_page_definition.ops->unmount(&context);
}

static void _test_recorder_row_opens_detail(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(s_recorder_snapshot.file_count == 1U);
    lv_obj_t *row = _test_find_button_for_label(_recorder_time_text());
    assert(row != NULL);
    _test_click(row);
    assert(s_nav.calls == 1U);
    assert(strcmp(s_nav.last.page_id, "detail") == 0);
    recorder_service_file_t selected = {0};
    assert(recorder_app_copy_selection(&selected) == ESP_OK);
    assert(strcmp(selected.target.name, s_recorder_row.target.name) == 0);
    assert(selected.target.generation == 3U && selected.target.revision == 7U);
    recorder_app_clear_selection();
    assert(recorder_app_copy_selection(&selected) == ESP_ERR_NOT_FOUND);
    recorder_files_page_definition.ops->unmount(&context);
}

static void _test_recorder_row_rejects_stale_epoch(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    /* A newer epoch published before the rows were rebuilt leaves the visible
     * row pointing at an identity the service no longer accepts. */
    ++s_recorder_snapshot.content_revision;
    ++s_recorder_snapshot.index_revision;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    lv_obj_t *label = _test_find_button_for_label(_recorder_time_text());
    assert(label != NULL);
    _test_click(label);
    assert(s_nav.calls == 0U);
    recorder_service_file_t selected = {0};
    assert(recorder_app_copy_selection(&selected) == ESP_ERR_NOT_FOUND);
    assert(_test_has_live_label_containing("列表已变化"));
    recorder_files_page_definition.ops->unmount(&context);
}

static void _test_recorder_list_notices(void)
{
    _recorder_fixture();
    s_recorder_snapshot.storage_ready = false;
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing("存储卡未挂载"));
    recorder_files_page_definition.ops->unmount(&context);

    _recorder_fixture();
    s_recorder_snapshot.index_valid = false;
    test_page_state_t state2 = {0};
    const app_manager_page_context_t context2 = _context(&state2);
    recorder_files_page_definition.ops->mount(&context2);
    _test_step_timers();
    assert(_test_has_live_label_containing("正在读取"));
    recorder_files_page_definition.ops->unmount(&context2);
}

static void _test_recorder_detail_plays_and_stops(void)
{
    _recorder_fixture();
    recorder_service_file_t throwaway = {0};
    assert(recorder_app_copy_selection(&throwaway) == ESP_ERR_NOT_FOUND);
    recorder_app_select(&s_recorder_row);
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing(_recorder_time_text()));
    assert(_test_has_live_label_containing("0:02"));
    assert(_test_has_live_label_containing("125.0 KB"));
    /* Selection is consumed on mount, so a later mount cannot reopen it. */
    recorder_service_file_t selected = {0};
    assert(recorder_app_copy_selection(&selected) == ESP_ERR_NOT_FOUND);

    _test_click_label("播放");
    assert(s_recorder_plays == 1U);
    s_recorder_snapshot.state = RECORDER_SERVICE_PLAYING;
    s_recorder_snapshot.playback_duration_ms = 2000U;
    s_recorder_snapshot.playback_position_ms = 500U;
    (void)snprintf(s_recorder_snapshot.name,
                   sizeof(s_recorder_snapshot.name), "%s",
                   s_recorder_row.target.name);
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("播放中"));
    assert(_test_has_live_label_containing("0:00 / 0:02") ||
           _test_has_live_label_containing("0:01 / 0:02"));
    _test_click_label("停止");
    assert(s_recorder_stops == 1U);
    recorder_detail_page_definition.ops->unmount(&context);
}

static void _test_recorder_detail_delete_returns(void)
{
    for (unsigned success = 0U; success < 2U; ++success)
    {
        _recorder_fixture();
        recorder_app_select(&s_recorder_row);
        test_page_state_t state = {0};
        const app_manager_page_context_t context = _context(&state);
        recorder_detail_page_definition.ops->mount(&context);
        _test_step_timers();
        _test_click_label("删除");
        assert(_test_has_live_label_containing("删除录音"));
        _test_click_label("删除");
        assert(s_recorder_deletes == 1U);
        assert(s_recorder_deleted_target.generation == 3U);
        assert(s_recorder_deleted_target.revision == 7U);
        const uint32_t operation = s_recorder_snapshot.operation_id;
        s_recorder_snapshot.operation_pending = false;
        s_recorder_snapshot.operation_id = operation;
        s_recorder_snapshot.last_error = success ? ESP_OK : ESP_FAIL;
        ++s_recorder_snapshot.generation;
        _test_step_timers();
        if (success)
        {
            assert(s_nav.calls == 1U);
            assert(s_nav.last.operation == APP_MANAGER_NAV_OP_BACK);
        }
        else
        {
            assert(_test_has_live_label_containing(
                       recorder_ui_error_text(ESP_FAIL)));
            assert(s_nav.calls == 0U);
        }
        recorder_detail_page_definition.ops->unmount(&context);
    }
}

static void _test_recorder_detail_stop_then_delete(void)
{
    _recorder_fixture();
    s_recorder_snapshot.state = RECORDER_SERVICE_PLAYING;
    (void)snprintf(s_recorder_snapshot.name,
                   sizeof(s_recorder_snapshot.name), "%s",
                   s_recorder_row.target.name);
    recorder_app_select(&s_recorder_row);
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_click_label("删除");
    _test_click_label("删除");
    assert(s_recorder_stops == 1U && s_recorder_deletes == 0U);
    s_recorder_snapshot.state = RECORDER_SERVICE_IDLE;
    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(s_recorder_deletes == 1U);
    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_FAIL;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing(
               recorder_ui_error_text(ESP_FAIL)));
    assert(s_nav.calls == 0U);
    recorder_detail_page_definition.ops->unmount(&context);
}

static void _test_recorder_detail_blocks_stale_target(void)
{
    _recorder_fixture();
    recorder_app_select(&s_recorder_row);
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    ++s_recorder_snapshot.content_revision;
    ++s_recorder_snapshot.index_revision;
    ++s_recorder_row.target.revision;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("内容已变化"));
    /* Stale controls are hidden rather than left as silent no-ops. */
    assert(_test_find_live_label("播放") == NULL);
    assert(_test_find_live_label("删除") == NULL);
    assert(s_recorder_plays == 0U);
    assert(s_recorder_deletes == 0U);
    recorder_detail_page_definition.ops->unmount(&context);
}

static void _test_recorder_detail_requires_selection(void)
{
    _recorder_fixture();
    recorder_app_clear_selection();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_detail_page_definition.ops->mount(&context);
    _test_step_timers();
    assert(_test_has_live_label_containing("不可用"));
    _test_click_label("播放");
    assert(s_recorder_plays == 0U);
    assert(s_recorder_deletes == 0U);
    recorder_detail_page_definition.ops->unmount(&context);
}


/* Fill the list with two additional rows and step the page so the rows exist. */
static void _recorder_seed_three_rows(void)
{
    s_recorder_extra_count = 2U;
    s_recorder_extra[0] = s_recorder_row;
    (void)snprintf(s_recorder_extra[0].target.name,
                   sizeof(s_recorder_extra[0].target.name),
                   "/sdcard/Recordings/second.wav");
    s_recorder_extra[0].duration_ms = 3000U;
    s_recorder_extra[1] = s_recorder_row;
    (void)snprintf(s_recorder_extra[1].target.name,
                   sizeof(s_recorder_extra[1].target.name),
                   "/sdcard/Recordings/third.wav");
    s_recorder_extra[1].duration_ms = 4000U;
    s_recorder_snapshot.file_count = 3U;
}

/* A long press enters selection instead of opening the Page, and the release
 * that follows must not do it either. */
static void _test_recorder_long_press_enters_selection(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    const size_t rows = _test_live_objects();
    lv_obj_t *row = _test_find_button_for_label(_recorder_time_text());
    assert(row != NULL);
    _test_long_press(row);
    /* The long press opened selection, not the detail Page. */
    assert(s_nav.calls == 0U);
    recorder_service_file_t selected = {0};
    assert(recorder_app_copy_selection(&selected) == ESP_ERR_NOT_FOUND);
    assert(_test_find_live_label("全选") != NULL);
    assert(_test_find_live_label("取消") != NULL);
    assert(_test_find_live_label("删除 1") != NULL);
    _test_step_timers();
    assert(_test_has_live_label_containing("已选 1 个"));
    assert(_test_live_objects() == rows);
    recorder_files_page_definition.ops->unmount(&context);
}

/* While selecting, a tap toggles rows; leaving the last one exits the mode. */
static void _test_recorder_selection_toggles_and_exits(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    lv_obj_t *first = _test_find_button_for_label(_recorder_time_text());
    assert(first != NULL);
    _test_long_press(first);
    assert(_test_find_live_label("删除 1") != NULL);

    /* Selecting a second row updates the count without navigating. */
    lv_obj_t *second = _test_find_button_for_label("0:03");
    assert(second != NULL);
    _test_click(second);
    assert(s_nav.calls == 0U);
    assert(_test_find_live_label("删除 2") != NULL);
    _test_step_timers();
    assert(_test_has_live_label_containing("已选 2 个"));

    /* Tapping the far-left row again deselects only that row. */
    _test_click(second);
    assert(_test_find_live_label("删除 1") != NULL);

    /* Deselecting the last selected row leaves the mode entirely. */
    _test_click(first);
    assert(_test_find_live_label("全选") == NULL);
    _test_step_timers();
    assert(_test_has_live_label_containing("长按录音可多选删除"));
    recorder_files_page_definition.ops->unmount(&context);
}

/* 全选 covers every row, and pressing it again clears the selection. */
static void _test_recorder_select_all(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    _test_click_label("全选");
    assert(_test_find_live_label("删除 3") != NULL);
    assert(_test_find_live_label("全不选") != NULL);
    _test_click_label("全不选");
    assert(_test_find_live_label("全选") == NULL);
    recorder_files_page_definition.ops->unmount(&context);
}

/* Cancelling selection discards it without touching the library. */
static void _test_recorder_selection_cancel(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    _test_click_label("取消");
    assert(_test_find_live_label("全选") == NULL);
    assert(s_recorder_deletes == 0U);
    recorder_files_page_definition.ops->unmount(&context);
}

/* Every selected recording is removed through one confirmation, one at a time,
 * and each submission rebuilds the epoch from the live snapshot. */
static void _test_recorder_batch_delete(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    _test_click_label("全选");
    _test_click_label("删除 3");
    assert(_test_has_live_label_containing("删除 3 个录音"));
    _test_click_label("删除");

    /* One removal is in flight; the batch waits for its completion. */
    assert(s_recorder_deletes == 1U);
    assert(strcmp(s_recorder_last_deleted, "/sdcard/Recordings/sample.wav") == 0);
    _test_step_timers();
    assert(s_recorder_deletes == 1U);

    /* Each publication advances the epoch; the next target follows it. */
    for (unsigned round = 1U; round < 3U; ++round)
    {
        /* Completion keeps the operation id; only a new submit advances it. */
        s_recorder_snapshot.operation_pending = false;
        s_recorder_snapshot.last_error = ESP_OK;
        ++s_recorder_snapshot.content_revision;
        ++s_recorder_snapshot.index_revision;
        ++s_recorder_snapshot.generation;
        _test_step_timers();
        assert(s_recorder_deletes == round + 1U);
        assert(s_recorder_deleted_target.revision ==
               s_recorder_snapshot.content_revision);
    }
    assert(strcmp(s_recorder_last_deleted, "/sdcard/Recordings/third.wav") == 0);

    /* The final completion retires the batch and reports what it removed. */
    ++s_recorder_snapshot.content_revision;
    ++s_recorder_snapshot.index_revision;
    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    s_recorder_row.target.name[0] = '\0';
    s_recorder_extra_count = 0U;
    _test_step_timers();
    assert(s_recorder_deletes == 3U);
    assert(_test_has_live_label_containing("已删除 3 个录音"));
    assert(_test_find_live_label("全选") == NULL);
    assert(_test_has_live_label_containing("暂无已完成录音"));
    recorder_files_page_definition.ops->unmount(&context);
}

/* A failure mid-batch stops it and names the reason instead of continuing. */
static void _test_recorder_batch_delete_stops_on_error(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    _test_click_label("全选");
    _test_click_label("删除 3");
    _test_click_label("删除");
    assert(s_recorder_deletes == 1U);

    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_FAIL;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(s_recorder_deletes == 1U);
    assert(_test_has_live_label_containing(
               recorder_ui_error_text(ESP_FAIL)));
    assert(_test_find_live_label("删除 3") == NULL);
    recorder_files_page_definition.ops->unmount(&context);
}

/* Recording owns the medium: selection cannot start, so no delete is queued. */
static void _test_recorder_batch_blocked_while_recording(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    s_recorder_snapshot.state = RECORDER_SERVICE_RECORDING;
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    assert(_test_find_live_label("全选") == NULL);
    _test_step_timers();
    assert(_test_has_live_label_containing("录音中无法编辑"));
    assert(s_recorder_deletes == 0U);
    recorder_files_page_definition.ops->unmount(&context);
}

/* Playback is stopped once, then the batch proceeds against the idle service. */
static void _test_recorder_batch_stops_playback_first(void)
{
    _recorder_fixture();
    s_recorder_extra_count = 1U;
    s_recorder_extra[0] = s_recorder_row;
    (void)snprintf(s_recorder_extra[0].target.name,
                   sizeof(s_recorder_extra[0].target.name),
                   "/sdcard/Recordings/second.wav");
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    _test_click_label("全选");
    _test_click_label("删除 2");
    s_recorder_snapshot.state = RECORDER_SERVICE_PLAYING;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    _test_click_label("删除");
    assert(s_recorder_stops == 1U);
    assert(s_recorder_deletes == 0U);

    s_recorder_snapshot.state = RECORDER_SERVICE_IDLE;
    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(s_recorder_deletes == 1U);
    recorder_files_page_definition.ops->unmount(&context);
}

/* A batch in flight does not rebuild rows, so the list never blanks between
 * two removals and the scroll position survives. */
static void _test_recorder_batch_keeps_rows_alive(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    lv_obj_t *row = _test_find_button_for_label(_recorder_time_text());
    assert(row != NULL);
    _test_long_press(row);
    _test_click_label("全选");
    _test_click_label("删除 3");
    _test_click_label("删除");
    const size_t live = _test_live_objects();
    /* A stale index (the service's state between two publications) must not
     * empty the list. */
    ++s_recorder_snapshot.content_revision;
    _test_step_timers();
    assert(_test_live_objects() == live);
    assert(row->live);
    recorder_files_page_definition.ops->unmount(&context);
}


/* Retiring a batch must refresh the list even though no new epoch arrived while
 * the rows were frozen: the survivors are what the user came back for. */
static void _test_recorder_batch_reloads_survivors(void)
{
    _recorder_fixture();
    _recorder_seed_three_rows();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_files_page_definition.ops->mount(&context);
    _test_step_timers();
    _test_long_press(_test_find_button_for_label(_recorder_time_text()));
    /* Select only the first row: two recordings must survive the batch. */
    _test_click_label("删除 1");
    _test_click_label("删除");
    assert(s_recorder_deletes == 1U);
    /* The medium now holds two files, but the frozen rows still show three. */
    s_recorder_extra_count = 1U;
    s_recorder_snapshot.operation_pending = false;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("已删除 1 个录音"));
    assert(_test_has_live_label_containing("0:02"));
    assert(_test_has_live_label_containing("0:03"));
    recorder_files_page_definition.ops->unmount(&context);
}


/* Hitting the configured limit is an automatic stop; the receipt must say so
 * instead of looking like the user pressed 停止. */
static void _test_recorder_root_limit_receipt(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_root_page_definition.ops->mount(&context);
    s_recorder_snapshot.state = RECORDER_SERVICE_RECORDING;
    s_recorder_snapshot.duration_ms = 1800000U;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("剩余 0:00"));
    s_recorder_snapshot.state = RECORDER_SERVICE_IDLE;
    s_recorder_snapshot.last_error = ESP_OK;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing("达到上限"));
    recorder_root_page_definition.ops->unmount(&context);
}

/* A service ERROR is retryable from the same Page; only a missing card or an
 * in-flight command disables the circle. */
static void _test_recorder_root_error_is_retryable(void)
{
    _recorder_fixture();
    test_page_state_t state = {0};
    const app_manager_page_context_t context = _context(&state);
    recorder_root_page_definition.ops->mount(&context);
    s_recorder_snapshot.state = RECORDER_SERVICE_ERROR;
    s_recorder_snapshot.last_error = ESP_FAIL;
    ++s_recorder_snapshot.generation;
    _test_step_timers();
    assert(_test_has_live_label_containing(recorder_ui_error_text(ESP_FAIL)));
    lv_obj_t *circle = _test_find_button_for_label("开始");
    assert(circle != NULL);
    assert(!lv_obj_has_state(circle, LV_STATE_DISABLED));
    _test_click(circle);
    assert(s_recorder_starts == 1U);
    recorder_root_page_definition.ops->unmount(&context);
}

int main(void)
{
    _test_helpers();
    _test_browse_mounts_and_lists();
    _test_browse_row_opens_detail();
    _test_detail_reads_and_deletes();
    _test_detail_without_arguments();
    _test_retains_detail_across_remount();
    _test_rows_ignore_pending_refresh();
    _test_empty_directory_detail_is_reachable();
    _test_busy_delete_is_not_nonempty();
    _test_storage_page_states();
    _test_failed_navigation_clears_rows();
    _test_unavailable_and_busy_states();
    _test_recorder_root_reports_failure();
    _test_recorder_root_pause_and_receipt();
    _test_recorder_root_circle_stops();
    _test_recorder_root_limit_receipt();
    _test_recorder_root_error_is_retryable();
    _test_recorder_row_opens_detail();
    _test_recorder_row_rejects_stale_epoch();
    _test_recorder_list_notices();
    _test_recorder_detail_plays_and_stops();
    _test_recorder_detail_delete_returns();
    _test_recorder_detail_stop_then_delete();
    _test_recorder_detail_blocks_stale_target();
    _test_recorder_detail_requires_selection();
    _test_recorder_long_press_enters_selection();
    _test_recorder_selection_toggles_and_exits();
    _test_recorder_select_all();
    _test_recorder_selection_cancel();
    _test_recorder_batch_delete();
    _test_recorder_batch_delete_stops_on_error();
    _test_recorder_batch_blocked_while_recording();
    _test_recorder_batch_stops_playback_first();
    _test_recorder_batch_keeps_rows_alive();
    _test_recorder_batch_reloads_survivors();
    puts("files/settings storage page tests passed");
    return 0;
}
