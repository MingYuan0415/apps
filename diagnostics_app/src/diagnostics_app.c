#define DBG_TAG "diagnostics_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "audio_service.h"
#include "connectivity_manager.h"
#include "imu_service.h"
#include "recorder_service.h"
#include "sd_storage_service.h"
#include "time_service.h"

#include <stdio.h>
#include <string.h>

typedef struct diagnostics_page_state
{
    app_ui_page_t page;
    lv_obj_t *status;
    lv_obj_t *imu;
    lv_obj_t *audio;
    lv_obj_t *storage;
    lv_obj_t *time;
    lv_timer_t *refresh_timer;
} diagnostics_page_state_t;

_Static_assert(sizeof(diagnostics_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Diagnostics page state exceeds lifecycle arena slot");

static void _diagnostics_render(diagnostics_page_state_t *state)
{
    imu_service_snapshot_t imu;
    const bool imu_ready = imu_service_get_snapshot(&imu) == ESP_OK && imu.valid;
    lv_label_set_text(state->imu, imu_ready ? "IMU：有效采样" : "IMU：不可用");
    lv_label_set_text(state->audio, audio_service_is_available() ?
                      (recorder_service_is_busy() ? "音频：录音服务占用" : "音频：可用") :
                      "音频：不可用");
    lv_label_set_text(state->storage, sd_storage_service_is_mounted() ?
                      "SD：已挂载" : "SD：未挂载");
    const time_service_quality_t quality = time_service_get_quality();
    lv_label_set_text(state->time, quality == TIME_SERVICE_QUALITY_INVALID ?
                      "RTC/NTP：未校准" : "RTC/NTP：有效");
    app_ui_set_status_text(state->status, "诊断页面仅供维护使用",
                           APP_UI_STATUS_NEUTRAL);
}

static void _diagnostics_refresh(lv_timer_t *timer)
{
    _diagnostics_render(lv_timer_get_user_data(timer));
}

static void _diagnostics_start(const app_manager_page_context_t *context)
{
    memset(context->state, 0, sizeof(diagnostics_page_state_t));
}

static void _diagnostics_mount(const app_manager_page_context_t *context)
{
    diagnostics_page_state_t *state = context->state;
    app_ui_page_create(&state->page, "诊断", false);
    app_ui_page_set_subtitle(&state->page, "工程信息");
    state->status = app_ui_add_body_label(state->page.content, "读取中");
    state->imu = app_ui_add_body_label(state->page.content, "IMU：--");
    state->audio = app_ui_add_body_label(state->page.content, "音频：--");
    state->storage = app_ui_add_body_label(state->page.content, "SD：--");
    state->time = app_ui_add_body_label(state->page.content, "RTC/NTP：--");
    state->refresh_timer = lv_timer_create(_diagnostics_refresh, 1000U, state);
    _diagnostics_render(state);
}

static esp_err_t _diagnostics_pause(const app_manager_page_context_t *context)
{
    diagnostics_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_pause(state->refresh_timer);
    }
    return ESP_OK;
}

static void _diagnostics_resume(const app_manager_page_context_t *context)
{
    diagnostics_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_resume(state->refresh_timer);
    }
    _diagnostics_render(context->state);
}

static void _diagnostics_unmount(const app_manager_page_context_t *context)
{
    diagnostics_page_state_t *state = context->state;
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
}

static const app_manager_page_ops_t s_diagnostics_ops =
{
    .start = _diagnostics_start,
    .mount = _diagnostics_mount,
    .resume = _diagnostics_resume,
    .pause = _diagnostics_pause,
    .unmount = _diagnostics_unmount,
};

static const app_manager_page_definition_t s_diagnostics_definition =
{
    .ops = &s_diagnostics_ops,
    .memory_size = sizeof(diagnostics_page_state_t),
};

static const app_manager_page_route_t s_diagnostics_routes[] =
{
    {.page_id = "root", .definition = &s_diagnostics_definition, .user_data = NULL},
};

APP_MANAGER_APP_EXPORT_META(diagnostics, APP_IMAGE_DIAGNOSTICS_ICON, "诊断",
                            APP_MANAGER_ID_DIAGNOSTICS, "root",
                            APP_MANAGER_APP_FLAG_HIDDEN, s_diagnostics_routes,
                            90U, "维护信息");
