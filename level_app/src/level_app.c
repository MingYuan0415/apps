#define DBG_TAG "level_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "chore_service.h"
#include "imu_service.h"
#include "nv_storage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

typedef struct level_page_state
{
    app_ui_page_t page;
    lv_obj_t *angle_label;
    lv_obj_t *state_label;
    lv_obj_t *temperature_label;
    lv_timer_t *refresh_timer;
    float roll_offset;
    float pitch_offset;
    float filtered_roll;
    float filtered_pitch;
    bool filter_valid;
    chore_service_handle_t calibration_job;
    bool calibration_job_active;
    float pending_roll_offset;
    float pending_pitch_offset;
    atomic_bool calibration_done;
    atomic_int calibration_result;
    bool calibration_error;
} level_page_state_t;

typedef struct level_calibration_blob
{
    uint32_t version;
    float roll_offset;
    float pitch_offset;
} level_calibration_blob_t;

#define LEVEL_CALIBRATION_KEY "level_cal"
#define LEVEL_CALIBRATION_VERSION 1U

_Static_assert(sizeof(level_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Level page state exceeds lifecycle arena slot");

static void _level_render(level_page_state_t *state)
{
    imu_service_snapshot_t snapshot;
    if (imu_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        app_ui_set_status_text(state->state_label, "传感器不可用",
                               APP_UI_STATUS_ERROR);
        return;
    }
    const imu_service_vector_t *acc = &snapshot.sample.acceleration_mps2;
    const float raw_roll = atan2f(acc->y, acc->z) * 57.2957795F;
    const float raw_pitch = atan2f(-acc->x,
                                   sqrtf(acc->y * acc->y + acc->z * acc->z)) *
                            57.2957795F;
    if (!state->filter_valid)
    {
        state->filtered_roll = raw_roll;
        state->filtered_pitch = raw_pitch;
        state->filter_valid = true;
    }
    else
    {
        state->filtered_roll += (raw_roll - state->filtered_roll) * 0.2F;
        state->filtered_pitch += (raw_pitch - state->filtered_pitch) * 0.2F;
    }
    const float roll = state->filtered_roll - state->roll_offset;
    const float pitch = state->filtered_pitch - state->pitch_offset;
    char text[64];
    (void)snprintf(text, sizeof(text), "左右 %+.1f°\n前后 %+.1f°", roll, pitch);
    lv_label_set_text(state->angle_label, text);
    (void)snprintf(text, sizeof(text), "温度 %.1f °C", snapshot.sample.temperature_c);
    lv_label_set_text(state->temperature_label, text);
    const float magnitude = fmaxf(fabsf(roll), fabsf(pitch));
    app_ui_set_status_text(state->state_label,
                           state->calibration_job_active ? "正在保存校准" :
                           (state->calibration_error ? "校准保存失败" :
                            (magnitude < 1.5F ? "基本水平" :
                             (magnitude < 5.0F ? "轻微倾斜" : "明显倾斜"))),
                           state->calibration_job_active ? APP_UI_STATUS_ACCENT :
                           (state->calibration_error ? APP_UI_STATUS_ERROR :
                            (magnitude < 1.5F ? APP_UI_STATUS_SUCCESS :
                             (magnitude < 5.0F ? APP_UI_STATUS_WARNING :
                              APP_UI_STATUS_ERROR))));
}

typedef enum
{
    LEVEL_CALIBRATION_LOAD = 0,
    LEVEL_CALIBRATION_SAVE,
} level_calibration_operation_t;

typedef struct level_calibration_job
{
    level_page_state_t *state;
    level_calibration_operation_t operation;
    float roll_offset;
    float pitch_offset;
} level_calibration_job_t;

static void _level_calibration_job(const chore_service_cancel_token_t *cancel,
                                   void *argument)
{
    level_calibration_job_t *job = argument;
    if (job == NULL || chore_service_cancel_pending(cancel))
    {
        return;
    }
    esp_err_t result = ESP_FAIL;
    if (job->operation == LEVEL_CALIBRATION_LOAD)
    {
        level_calibration_blob_t blob;
        size_t size = sizeof(blob);
        result = nv_storage_get_blob(LEVEL_CALIBRATION_KEY, &blob, &size);
        if (result == ESP_OK && size == sizeof(blob) &&
                blob.version == LEVEL_CALIBRATION_VERSION &&
                isfinite(blob.roll_offset) && isfinite(blob.pitch_offset) &&
                fabsf(blob.roll_offset) <= 180.0F &&
                fabsf(blob.pitch_offset) <= 180.0F)
        {
            job->roll_offset = blob.roll_offset;
            job->pitch_offset = blob.pitch_offset;
        }
        else if (result == ESP_ERR_NOT_FOUND)
        {
            result = ESP_OK;
        }
    }
    else
    {
        const level_calibration_blob_t blob =
        {
            .version = LEVEL_CALIBRATION_VERSION,
            .roll_offset = job->roll_offset,
            .pitch_offset = job->pitch_offset,
        };
        result = nv_storage_set_blob(LEVEL_CALIBRATION_KEY, &blob,
                                     sizeof(blob));
    }
    job->state->pending_roll_offset = job->roll_offset;
    job->state->pending_pitch_offset = job->pitch_offset;
    atomic_store_explicit(&job->state->calibration_result, result,
                          memory_order_relaxed);
    atomic_store_explicit(&job->state->calibration_done, true,
                          memory_order_release);
}

static void _level_calibration_release(void *argument)
{
    free(argument);
}

static void _level_calibration_apply_result(level_page_state_t *state)
{
    if (!atomic_load_explicit(&state->calibration_done, memory_order_acquire))
    {
        return;
    }
    const esp_err_t result = atomic_load_explicit(&state->calibration_result,
                             memory_order_relaxed);
    atomic_store_explicit(&state->calibration_done, false, memory_order_relaxed);
    state->calibration_job_active = false;
    if (result != ESP_OK)
    {
        state->calibration_error = true;
        app_ui_set_status_text(state->state_label, "校准保存失败",
                               APP_UI_STATUS_ERROR);
        return;
    }
    state->roll_offset = state->pending_roll_offset;
    state->pitch_offset = state->pending_pitch_offset;
    state->calibration_error = false;
    state->filter_valid = false;
}

static esp_err_t _level_submit_calibration(level_page_state_t *state,
        level_calibration_operation_t operation,
        float roll_offset,
        float pitch_offset)
{
    level_calibration_job_t *job = calloc(1U, sizeof(*job));
    if (job == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    job->state = state;
    job->operation = operation;
    job->roll_offset = roll_offset;
    job->pitch_offset = pitch_offset;
    atomic_store_explicit(&state->calibration_done, false, memory_order_relaxed);
    const chore_service_job_t chore =
    {
        .run = _level_calibration_job,
        .release = _level_calibration_release,
        .arg = job,
    };
    const esp_err_t result = chore_service_submit(&chore,
                             &state->calibration_job);
    if (result == ESP_OK)
    {
        state->calibration_job_active = true;
    }
    else
    {
        free(job);
    }
    return result;
}

static void _level_refresh(lv_timer_t *timer)
{
    level_page_state_t *state = lv_timer_get_user_data(timer);
    _level_calibration_apply_result(state);
    _level_render(state);
}

static void _level_calibrate(lv_event_t *event)
{
    level_page_state_t *state = lv_event_get_user_data(event);
    imu_service_snapshot_t snapshot;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            imu_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        return;
    }
    const imu_service_vector_t *acc = &snapshot.sample.acceleration_mps2;
    if (state->calibration_job_active)
    {
        return;
    }
    const float roll_offset = atan2f(acc->y, acc->z) * 57.2957795F;
    const float pitch_offset = atan2f(-acc->x,
                                      sqrtf(acc->y * acc->y + acc->z * acc->z)) *
                               57.2957795F;
    const esp_err_t result = _level_submit_calibration(state,
                             LEVEL_CALIBRATION_SAVE,
                             roll_offset,
                             pitch_offset);
    app_ui_set_status_text(state->state_label,
                           result == ESP_OK ? "正在保存校准" : "校准提交失败",
                           result == ESP_OK ? APP_UI_STATUS_ACCENT :
                           APP_UI_STATUS_ERROR);
}

static void _level_mount(const app_manager_page_context_t *context)
{
    level_page_state_t *state = context->state;
    app_ui_page_create(&state->page, "水平仪", false);
    app_ui_page_set_subtitle(&state->page, "倾角与稳定性");
    atomic_init(&state->calibration_done, false);
    atomic_init(&state->calibration_result, ESP_OK);
    if (_level_submit_calibration(state, LEVEL_CALIBRATION_LOAD, 0.0F,
                                  0.0F) != ESP_OK)
    {
        LOG_W("calibration load unavailable");
    }
    state->state_label = app_ui_add_body_label(state->page.content, "读取中");
    state->angle_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->angle_label, LV_PCT(100));
    lv_obj_set_height(state->angle_label, 120);
    lv_obj_set_style_text_align(state->angle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state->angle_label, app_ui_font(APP_THEME_FONT_TITLE), 0);
    state->temperature_label = app_ui_add_body_label(state->page.content, "温度 --");
    lv_obj_t *calibrate = app_ui_add_command(state->page.content, LV_SYMBOL_REFRESH,
                          "校准当前位置", "将当前姿态设为水平",
                          _level_calibrate, state);
    (void)calibrate;
    app_ui_add_body_label(state->page.content, "设备没有磁力计，不能提供指南针方位。");
    state->refresh_timer = lv_timer_create(_level_refresh, 50U, state);
    _level_render(state);
}

static esp_err_t _level_pause(const app_manager_page_context_t *context)
{
    level_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _level_resume(const app_manager_page_context_t *context)
{
    level_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _level_render(state);
}

static void _level_unmount(const app_manager_page_context_t *context)
{
    level_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    if (state->calibration_job_active)
    {
        (void)chore_service_cancel(&state->calibration_job,
                                   CHORE_SERVICE_WAIT_FOREVER);
        state->calibration_job_active = false;
    }
    app_ui_page_destroy(&state->page);
    state->angle_label = NULL;
    state->state_label = NULL;
    state->temperature_label = NULL;
}

static const app_manager_page_ops_t s_level_ops =
{
    .mount = _level_mount,
    .resume = _level_resume,
    .pause = _level_pause,
    .unmount = _level_unmount,
};

static const app_manager_page_definition_t s_level_definition =
{
    .ops = &s_level_ops,
    .memory_size = sizeof(level_page_state_t),
};

static const app_manager_page_route_t s_level_routes[] =
{
    {.page_id = "root", .definition = &s_level_definition, .user_data = NULL},
};

APP_MANAGER_APP_EXPORT_META(level, APP_IMAGE_LEVEL_ICON, "水平仪",
                            APP_MANAGER_ID_LEVEL, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_level_routes, 40U,
                            "倾角与校准");
