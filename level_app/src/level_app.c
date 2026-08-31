#define DBG_TAG "level_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "imu_service.h"

#include <math.h>
#include <stdio.h>
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
} level_page_state_t;

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
    const float roll = atan2f(acc->y, acc->z) * 57.2957795F - state->roll_offset;
    const float pitch = atan2f(-acc->x,
                               sqrtf(acc->y * acc->y + acc->z * acc->z)) *
                        57.2957795F - state->pitch_offset;
    char text[64];
    (void)snprintf(text, sizeof(text), "左右 %+.1f°\n前后 %+.1f°", roll, pitch);
    lv_label_set_text(state->angle_label, text);
    (void)snprintf(text, sizeof(text), "温度 %.1f °C", snapshot.sample.temperature_c);
    lv_label_set_text(state->temperature_label, text);
    const float magnitude = fmaxf(fabsf(roll), fabsf(pitch));
    app_ui_set_status_text(state->state_label,
                           magnitude < 1.5F ? "基本水平" :
                           (magnitude < 5.0F ? "轻微倾斜" : "明显倾斜"),
                           magnitude < 1.5F ? APP_UI_STATUS_SUCCESS :
                           (magnitude < 5.0F ? APP_UI_STATUS_WARNING :
                            APP_UI_STATUS_ERROR));
}

static void _level_refresh(lv_timer_t *timer)
{
    _level_render(lv_timer_get_user_data(timer));
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
    state->roll_offset = atan2f(acc->y, acc->z) * 57.2957795F;
    state->pitch_offset = atan2f(-acc->x,
                                 sqrtf(acc->y * acc->y + acc->z * acc->z)) *
                          57.2957795F;
    _level_render(state);
}

static void _level_start(const app_manager_page_context_t *context)
{
    memset(context->state, 0, sizeof(level_page_state_t));
}

static void _level_mount(const app_manager_page_context_t *context)
{
    level_page_state_t *state = context->state;
    app_ui_page_create(&state->page, "水平仪", false);
    app_ui_page_set_subtitle(&state->page, "倾角与稳定性");
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
    app_ui_page_destroy(&state->page);
}

static const app_manager_page_ops_t s_level_ops =
{
    .start = _level_start,
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
