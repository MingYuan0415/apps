#define DBG_TAG "diagnostics_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "audio_service.h"
#include "connectivity_manager.h"
#include "device_link_service.h"
#include "imu_service.h"
#include "recorder_service.h"
#include "sd_storage_service.h"
#include "time_service.h"

#include <stdio.h>
#include <string.h>

typedef struct diagnostics_row
{
    lv_obj_t *dot;
    lv_obj_t *text;
} diagnostics_row_t;

typedef struct diagnostics_page_state
{
    app_ui_page_t page;
    lv_obj_t *status;
    diagnostics_row_t rows[6];
    lv_timer_t *refresh_timer;
} diagnostics_page_state_t;

_Static_assert(sizeof(diagnostics_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Diagnostics page state exceeds lifecycle arena slot");

static void _diagnostics_set(diagnostics_row_t *row, uint32_t color,
                             const char *text)
{
    lv_obj_set_style_bg_color(row->dot, lv_color_hex(color), 0);
    lv_label_set_text(row->text, text);
}

static void _diagnostics_render(diagnostics_page_state_t *state)
{
    imu_service_snapshot_t imu;
    const bool imu_ready = imu_service_get_snapshot(&imu) == ESP_OK && imu.valid;
    _diagnostics_set(&state->rows[0], imu_ready ? APP_UI_COLOR_RAIN :
                     APP_UI_COLOR_WARNING,
                     imu_ready ? "IMU：有效采样" : "IMU：不可用");
    _diagnostics_set(&state->rows[1],
                     audio_service_is_available() ?
                     (recorder_service_is_busy() ? APP_UI_COLOR_SUN :
                      APP_UI_COLOR_RAIN) : APP_UI_COLOR_WARNING,
                     audio_service_is_available() ?
                     (recorder_service_is_busy() ? "音频：录音服务占用" : "音频：可用") :
                     "音频：不可用");
    _diagnostics_set(&state->rows[2],
                     sd_storage_service_is_mounted() ? APP_UI_COLOR_RAIN :
                     APP_UI_COLOR_SUN,
                     sd_storage_service_is_mounted() ? "SD：已挂载" : "SD：未挂载");
    const time_service_quality_t quality = time_service_get_quality();
    _diagnostics_set(&state->rows[3],
                     quality == TIME_SERVICE_QUALITY_INVALID ?
                     APP_UI_COLOR_SUN : APP_UI_COLOR_RAIN,
                     quality == TIME_SERVICE_QUALITY_INVALID ?
                     "RTC/NTP：未校准" : "RTC/NTP：有效");
    connectivity_manager_status_snapshot_t wifi;
    if (connectivity_manager_get_status(&wifi) == ESP_OK)
    {
        const bool connected = wifi.state == CONNECTIVITY_MANAGER_STATE_IP_READY;
        char text[48];
        (void)snprintf(text, sizeof(text), "Wi-Fi：%s",
                       connected ? (wifi.ssid[0] != '\0' ? wifi.ssid : "已连接") :
                       (wifi.available ? "未连接" : "不可用"));
        _diagnostics_set(&state->rows[4],
                         connected ? APP_UI_COLOR_RAIN :
                         (wifi.available ? APP_UI_COLOR_MUTED :
                          APP_UI_COLOR_WARNING), text);
    }
    else
    {
        _diagnostics_set(&state->rows[4], APP_UI_COLOR_WARNING,
                         "Wi-Fi：不可用");
    }
    device_link_service_status_t bluetooth;
    if (device_link_service_get_status(&bluetooth) == ESP_OK)
    {
        _diagnostics_set(&state->rows[5],
                         bluetooth.bound ? APP_UI_COLOR_RAIN :
                         (bluetooth.active ? APP_UI_COLOR_SUN :
                          APP_UI_COLOR_MUTED),
                         bluetooth.bound ? "蓝牙：已绑定" :
                         (bluetooth.active ? "蓝牙：绑定窗口" : "蓝牙：未绑定"));
    }
    else
    {
        _diagnostics_set(&state->rows[5], APP_UI_COLOR_WARNING,
                         "蓝牙：不可用");
    }
    app_ui_set_status_text(state->status, "诊断页面仅供维护使用",
                           APP_UI_STATUS_NEUTRAL);
}

static void _diagnostics_refresh(lv_timer_t *timer)
{
    _diagnostics_render(lv_timer_get_user_data(timer));
}

static void _diagnostics_add_row(diagnostics_page_state_t *state,
                                 diagnostics_row_t *row)
{
    lv_obj_t *line = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(line);
    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_height(line, 40);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 10, 0);
    app_ui_make_passive(line, false);

    row->dot = lv_obj_create(line);
    lv_obj_remove_style_all(row->dot);
    lv_obj_set_size(row->dot, 10, 10);
    lv_obj_set_style_radius(row->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(row->dot, lv_color_hex(APP_UI_COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(row->dot, LV_OPA_COVER, 0);
    app_ui_make_passive(row->dot, false);

    row->text = lv_label_create(line);
    lv_obj_set_width(row->text, 0);
    lv_obj_set_flex_grow(row->text, 1);
    /* Diagnostics exists to show exact values: long SSIDs/paths scroll
     * circularly instead of being silently truncated. */
    lv_label_set_long_mode(row->text, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(row->text, lv_color_hex(APP_UI_COLOR_TEXT),
                                0);
    lv_obj_set_style_text_font(row->text,
                               app_ui_font(APP_THEME_FONT_BODY), 0);
    lv_label_set_text(row->text, "");
}

static void _diagnostics_mount(const app_manager_page_context_t *context)
{
    diagnostics_page_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    app_ui_page_create(&state->page, "诊断", false);
    app_ui_page_set_subtitle(&state->page, "工程信息");
    for (size_t index = 0U; index < 6U; ++index)
    {
        _diagnostics_add_row(state, &state->rows[index]);
    }
    state->status = app_ui_add_body_label(state->page.content, "读取中");
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
    state->status = NULL;
}

static const app_manager_page_ops_t s_diagnostics_ops =
{
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
