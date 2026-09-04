#define DBG_TAG "level_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "chore_service.h"
#include "imu_service.h"
#include "level_app_persistence.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#define LEVEL_BOARD_SIZE 160
#define LEVEL_BUBBLE_SIZE 24
#define LEVEL_MAX_DEGREES 15.0F
#define LEVEL_LEVEL_DEGREES 1.5F

typedef struct level_page_state
{
    app_ui_page_t page;
    lv_obj_t *angle_label;
    lv_obj_t *state_label;
    lv_obj_t *info_label;
    lv_obj_t *bubble;
    lv_obj_t *target;
    lv_timer_t *refresh_timer;
    int32_t last_bubble_x;
    int32_t last_bubble_y;
    uint32_t last_bubble_color;
    bool last_bubble_visible;
    char last_angle[48];
    char last_state[24];
    uint32_t last_state_color;
    float roll_offset;
    float pitch_offset;
    float filtered_roll;
    float filtered_pitch;
    bool filter_valid;
    chore_service_handle_t calibration_job;
    bool calibration_job_active;
    bool calibration_op_save;
    float pending_roll_offset;
    float pending_pitch_offset;
    atomic_bool calibration_done;
    atomic_int calibration_result;
    bool calibration_error;
} level_page_state_t;

_Static_assert(sizeof(level_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Level page state exceeds lifecycle arena slot");

static void _level_set_status(level_page_state_t *state, const char *text,
                              app_ui_status_t status, uint32_t accent)
{
    if (strcmp(state->last_state, text) == 0 &&
            state->last_state_color == (uint32_t)status)
    {
        return;
    }
    (void)snprintf(state->last_state, sizeof(state->last_state), "%s", text);
    state->last_state_color = (uint32_t)status;
    app_ui_set_status_text(state->state_label, text, status);
    if (accent != 0U)
    {
        lv_obj_set_style_text_color(state->state_label,
                                    lv_color_hex(accent), 0);
    }
}

static void _level_render(level_page_state_t *state)
{
    imu_service_snapshot_t snapshot;
    if (imu_service_get_snapshot(&snapshot) != ESP_OK || !snapshot.valid)
    {
        _level_set_status(state, "传感器不可用", APP_UI_STATUS_ERROR, 0U);
        lv_label_set_text(state->angle_label, "左右 --\n前后 --");
        state->last_angle[0] = '\0';
        if (state->last_bubble_visible)
        {
            state->last_bubble_visible = false;
            lv_obj_add_flag(state->bubble, LV_OBJ_FLAG_HIDDEN);
        }
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
    char text[48];
    (void)snprintf(text, sizeof(text), "左右 %+.1f°\n前后 %+.1f°", roll, pitch);
    if (strcmp(state->last_angle, text) != 0)
    {
        (void)snprintf(state->last_angle, sizeof(state->last_angle), "%s",
                       text);
        lv_label_set_text(state->angle_label, text);
    }
    char info[64];
    (void)snprintf(info, sizeof(info), "温度 %.1f °C",
                   snapshot.sample.temperature_c);
    lv_label_set_text(state->info_label, info);

    const float magnitude = fmaxf(fabsf(roll), fabsf(pitch));
    const bool level = magnitude < LEVEL_LEVEL_DEGREES;
    if (state->calibration_job_active)
    {
        _level_set_status(state, "正在保存校准", APP_UI_STATUS_ACCENT, 0U);
    }
    else if (state->calibration_error)
    {
        _level_set_status(state, "校准保存失败", APP_UI_STATUS_ERROR, 0U);
    }
    else if (level)
    {
        _level_set_status(state, "已水平", APP_UI_STATUS_NEUTRAL, APP_UI_COLOR_SUN);
    }
    else if (magnitude < 5.0F)
    {
        _level_set_status(state, "轻微倾斜", APP_UI_STATUS_WARNING, 0U);
    }
    else
    {
        _level_set_status(state, "明显倾斜", APP_UI_STATUS_ERROR, 0U);
    }

    const float clamp = LEVEL_MAX_DEGREES;
    const float scale = (LEVEL_BOARD_SIZE - LEVEL_BUBBLE_SIZE - 8) / 2.0F /
                        clamp;
    float bx = roll;
    float by = pitch;
    /* Radial clamp keeps the bubble on the circular board instead of letting
     * a combined tilt park it in a square corner outside the ring. */
    const float tilt = sqrtf(bx * bx + by * by);
    if (tilt > clamp)
    {
        bx *= clamp / tilt;
        by *= clamp / tilt;
    }
    const int32_t center = LEVEL_BOARD_SIZE / 2 - LEVEL_BUBBLE_SIZE / 2;
    const int32_t x = center + (int32_t)(bx * scale);
    const int32_t y = center + (int32_t)(by * scale);
    const uint32_t color = level ? APP_UI_COLOR_SUN : APP_UI_COLOR_RAIN;
    if (!state->last_bubble_visible)
    {
        state->last_bubble_visible = true;
        lv_obj_remove_flag(state->bubble, LV_OBJ_FLAG_HIDDEN);
    }
    if (x != state->last_bubble_x || y != state->last_bubble_y)
    {
        state->last_bubble_x = x;
        state->last_bubble_y = y;
        lv_obj_set_pos(state->bubble, x, y);
    }
    if (color != state->last_bubble_color)
    {
        state->last_bubble_color = color;
        lv_obj_set_style_bg_color(state->bubble, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(state->target, lv_color_hex(
                                          level ? APP_UI_COLOR_SUN : APP_UI_COLOR_SURFACE_HI),
                                      0);
    }
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
        result = level_app_persistence_load(&job->roll_offset,
                                            &job->pitch_offset);
    }
    else
    {
        result = level_app_persistence_save(job->roll_offset,
                                            job->pitch_offset);
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
        if (!state->calibration_op_save)
        {
            return;
        }
        state->calibration_error = true;
        _level_set_status(state, "校准保存失败", APP_UI_STATUS_ERROR, 0U);
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
    state->calibration_op_save = operation == LEVEL_CALIBRATION_SAVE;
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
    _level_set_status(state,
                      result == ESP_OK ? "正在保存校准" : "校准提交失败",
                      result == ESP_OK ? APP_UI_STATUS_ACCENT :
                      APP_UI_STATUS_ERROR, 0U);
}

static lv_obj_t *_level_line(lv_obj_t *parent,
                             const lv_point_precise_t *points,
                             size_t count)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                                0);
    app_ui_make_passive(line, false);
    return line;
}

static void _level_mount(const app_manager_page_context_t *context)
{
    static const lv_point_precise_t horizontal[] = {{ 8, 80 }, { 152, 80 }};
    static const lv_point_precise_t vertical[] = {{ 80, 8 }, { 80, 152 }};
    level_page_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    atomic_init(&state->calibration_done, false);
    atomic_init(&state->calibration_result, ESP_OK);
    state->last_bubble_color = APP_UI_COLOR_RAIN;
    app_ui_page_create(&state->page, "水平仪", false);
    app_ui_page_set_subtitle(&state->page, "倾角与稳定性");
    lv_obj_set_style_pad_row(state->page.content, 8, 0);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    lv_obj_remove_flag(state->page.content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *board_row = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(board_row);
    lv_obj_set_width(board_row, LV_PCT(100));
    lv_obj_set_height(board_row, LEVEL_BOARD_SIZE);
    lv_obj_set_flex_flow(board_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(board_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    app_ui_make_passive(board_row, false);

    lv_obj_t *board = lv_obj_create(board_row);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, LEVEL_BOARD_SIZE, LEVEL_BOARD_SIZE);
    app_ui_make_passive(board, false);
    (void)app_ui_ring_create(board, LEVEL_BOARD_SIZE, 2,
                             APP_UI_COLOR_SURFACE_HI);
    (void)_level_line(board, horizontal, 2);
    (void)_level_line(board, vertical, 2);

    state->target = lv_obj_create(board);
    lv_obj_remove_style_all(state->target);
    lv_obj_set_size(state->target, 44, 44);
    lv_obj_set_style_radius(state->target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(state->target, 2, 0);
    lv_obj_set_style_border_color(state->target,
                                  lv_color_hex(APP_UI_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_bg_opa(state->target, LV_OPA_TRANSP, 0);
    app_ui_make_passive(state->target, false);
    lv_obj_center(state->target);

    state->bubble = lv_obj_create(board);
    lv_obj_remove_style_all(state->bubble);
    lv_obj_set_size(state->bubble, LEVEL_BUBBLE_SIZE, LEVEL_BUBBLE_SIZE);
    lv_obj_set_style_radius(state->bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->bubble, lv_color_hex(APP_UI_COLOR_RAIN),
                              0);
    lv_obj_set_style_bg_opa(state->bubble, LV_OPA_COVER, 0);
    app_ui_make_passive(state->bubble, false);

    if (_level_submit_calibration(state, LEVEL_CALIBRATION_LOAD, 0.0F,
                                  0.0F) != ESP_OK)
    {
        LOG_W("calibration load unavailable");
    }

    state->angle_label = lv_label_create(state->page.content);
    lv_obj_set_width(state->angle_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->angle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(state->angle_label, 2, 0);
    lv_obj_set_style_text_color(state->angle_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(state->angle_label,
                               app_ui_font(APP_THEME_FONT_HEAD), 0);
    lv_label_set_text(state->angle_label, "左右 --\n前后 --");

    state->state_label = app_ui_add_body_label(state->page.content, "读取中");
    lv_obj_set_width(state->state_label, LV_PCT(100));
    lv_obj_set_style_text_align(state->state_label, LV_TEXT_ALIGN_CENTER, 0);

    state->info_label = app_ui_add_body_label(state->page.content,
                        "温度 -- °C");
    lv_obj_set_width(state->info_label, LV_PCT(100));
    lv_label_set_long_mode(state->info_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(state->info_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *calibrate = lv_button_create(state->page.content);
    app_ui_click_only(calibrate);
    lv_obj_set_width(calibrate, LV_PCT(100));
    lv_obj_set_height(calibrate, 52);
    lv_obj_set_style_radius(calibrate, 6, 0);
    lv_obj_set_style_bg_color(calibrate, lv_color_hex(APP_UI_COLOR_SURFACE),
                              0);
    lv_obj_set_style_bg_color(calibrate,
                              lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                              LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(calibrate, 0, 0);
    lv_obj_add_event_cb(calibrate, _level_calibrate, LV_EVENT_CLICKED, state);
    lv_obj_t *calibrate_label = lv_label_create(calibrate);
    lv_obj_set_style_text_font(calibrate_label,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_color(calibrate_label,
                                lv_color_hex(APP_UI_COLOR_TEXT), 0);
    lv_label_set_text(calibrate_label, "校准当前位置");
    lv_obj_center(calibrate_label);

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
    state->info_label = NULL;
    state->bubble = NULL;
    state->target = NULL;
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
