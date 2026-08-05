#define DBG_TAG "motion_demo"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "imu_service.h"
#include "menu_page_definitions.h"

#include <stdio.h>
#include <string.h>

#define MOTION_REFRESH_PERIOD_MS 50U
#define MOTION_TILT_THRESHOLD    2.0F

typedef struct motion_page_state
{
    app_ui_page_t page;
    lv_obj_t *tilt_value;
    lv_obj_t *acceleration_value;
    lv_obj_t *angular_value;
    lv_obj_t *temperature_value;
    lv_obj_t *sample_value;
    lv_timer_t *refresh_timer;
} motion_page_state_t;

_Static_assert(sizeof(motion_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "motion page state exceeds the retained-page slot");

static const char *_motion_service_state_text(imu_service_state_t service_state)
{
    const char *text = "不可用";
    switch (service_state)
    {
    case IMU_SERVICE_STATE_STARTING:
        text = "正在启动";
        break;
    case IMU_SERVICE_STATE_RUNNING:
        text = "采集中";
        break;
    case IMU_SERVICE_STATE_PAUSE_PENDING:
        text = "正在暂停";
        break;
    case IMU_SERVICE_STATE_PAUSED:
        text = "已暂停";
        break;
    case IMU_SERVICE_STATE_RESUME_PENDING:
        text = "正在恢复";
        break;
    case IMU_SERVICE_STATE_STOPPING:
        text = "正在停止";
        break;
    case IMU_SERVICE_STATE_ERROR:
        text = "服务错误";
        break;
    case IMU_SERVICE_STATE_STOPPED:
    default:
        break;
    }
    return text;
}

static void _motion_render_unavailable(motion_page_state_t *state,
                                       const char *status,
                                       app_ui_status_t semantic_status)
{
    lv_label_set_text(state->tilt_value, "--");
    lv_label_set_text(state->acceleration_value, "X --  Y --  Z --");
    lv_label_set_text(state->angular_value, "X --  Y --  Z --");
    lv_label_set_text(state->temperature_value, "--");
    app_ui_set_status_text(state->sample_value, status, semantic_status);
}

static void _motion_tilt_text(const imu_service_vector_t *acceleration,
                              char *text, size_t text_size)
{
    const char *horizontal = "";
    const char *vertical = "";
    if (acceleration->x > MOTION_TILT_THRESHOLD)
    {
        horizontal = "右";
    }
    else if (acceleration->x < -MOTION_TILT_THRESHOLD)
    {
        horizontal = "左";
    }
    if (acceleration->y > MOTION_TILT_THRESHOLD)
    {
        vertical = "前";
    }
    else if (acceleration->y < -MOTION_TILT_THRESHOLD)
    {
        vertical = "后";
    }

    if (horizontal[0] == '\0' && vertical[0] == '\0')
    {
        snprintf(text, text_size, "接近平放");
    }
    else
    {
        snprintf(text, text_size, "%s%s倾斜", horizontal, vertical);
    }
}

static void _motion_render_snapshot(motion_page_state_t *state,
                                    const imu_service_snapshot_t *snapshot)
{
    char text[96];
    _motion_tilt_text(&snapshot->sample.acceleration_mps2,
                      text, sizeof(text));
    lv_label_set_text(state->tilt_value, text);

    snprintf(text, sizeof(text), "X %+.2f  Y %+.2f  Z %+.2f",
             (double)snapshot->sample.acceleration_mps2.x,
             (double)snapshot->sample.acceleration_mps2.y,
             (double)snapshot->sample.acceleration_mps2.z);
    lv_label_set_text(state->acceleration_value, text);
    snprintf(text, sizeof(text), "X %+.1f  Y %+.1f  Z %+.1f",
             (double)snapshot->sample.angular_velocity_dps.x,
             (double)snapshot->sample.angular_velocity_dps.y,
             (double)snapshot->sample.angular_velocity_dps.z);
    lv_label_set_text(state->angular_value, text);
    lv_label_set_text_fmt(state->temperature_value, "%.1f C",
                          (double)snapshot->sample.temperature_c);
    snprintf(text, sizeof(text), "%s / #%lu%s",
             _motion_service_state_text(imu_service_get_state()),
             (unsigned long)snapshot->sequence,
             snapshot->sample.data_ready ? " / DRDY" : "");
    app_ui_set_status_text(state->sample_value, text, APP_UI_STATUS_SUCCESS);
}

static void _motion_refresh(motion_page_state_t *state)
{
    imu_service_snapshot_t snapshot;
    esp_err_t result = imu_service_get_snapshot(&snapshot);
    if (result != ESP_OK)
    {
        _motion_render_unavailable(
            state, _motion_service_state_text(imu_service_get_state()),
            APP_UI_STATUS_ERROR);
        return;
    }
    if (!snapshot.available)
    {
        _motion_render_unavailable(state, "传感器不可用",
                                   APP_UI_STATUS_ERROR);
        return;
    }
    if (!snapshot.valid)
    {
        _motion_render_unavailable(state, "等待有效样本",
                                   APP_UI_STATUS_ACCENT);
        return;
    }
    _motion_render_snapshot(state, &snapshot);
}

static void _motion_refresh_timer(lv_timer_t *timer)
{
    motion_page_state_t *state = lv_timer_get_user_data(timer);
    if (state != NULL && state->page.root != NULL)
    {
        _motion_refresh(state);
    }
}

static void _motion_page_build(motion_page_state_t *state)
{
    app_ui_page_create(&state->page, "运动传感", true);
    app_ui_add_section(state->page.content, "实时状态");
    app_ui_add_value_row(state->page.content, "倾斜提示", "--",
                         &state->tilt_value);
    app_ui_add_value_row(state->page.content, "采样", "等待服务",
                         &state->sample_value);

    app_ui_add_section(state->page.content, "原始传感器数据");
    app_ui_add_value_row(state->page.content, "加速度 m/s2", "--",
                         &state->acceleration_value);
    app_ui_add_value_row(state->page.content, "角速度 deg/s", "--",
                         &state->angular_value);
    app_ui_add_value_row(state->page.content, "温度", "--",
                         &state->temperature_value);
    app_ui_add_body_label(state->page.content,
                          "倾斜提示仅使用原始加速度，不进行姿态融合。");

    lv_obj_set_style_text_font(state->acceleration_value,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    lv_obj_set_style_text_font(state->angular_value,
                               app_ui_font(APP_THEME_FONT_SMALL), 0);
    _motion_refresh(state);
}

static void _motion_page_resume(motion_page_state_t *state)
{
    _motion_refresh(state);
    if (state->refresh_timer == NULL)
    {
        state->refresh_timer = lv_timer_create(
                                   _motion_refresh_timer,
                                   MOTION_REFRESH_PERIOD_MS, state);
        if (state->refresh_timer == NULL)
        {
            app_ui_set_status_text(state->sample_value,
                                   "刷新定时器不可用",
                                   APP_UI_STATUS_ERROR);
            LOG_W("refresh timer unavailable");
        }
    }
}

static void _motion_page_pause(motion_page_state_t *state)
{
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
}

static void _motion_page_unmount(motion_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->tilt_value = NULL;
    state->acceleration_value = NULL;
    state->angular_value = NULL;
    state->temperature_value = NULL;
    state->sample_value = NULL;
}

static void _motion_page_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    motion_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _motion_page_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _motion_page_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        _motion_page_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _motion_page_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        _motion_page_pause(state);
        LOG_I("stopped");
        break;
    default:
        break;
    }
}

const app_manager_page_definition_t menu_motion_page_definition =
{
    .handler = _motion_page_handler,
    .memory_size = sizeof(motion_page_state_t),
};
