#include "weather_app_internal.h"

#include <stdio.h>
#include <time.h>

static uint32_t _weather_ui_image_id(uint16_t code, bool small)
{
    uint32_t offset = APP_IMAGE_WEATHER_UNKNOWN_MAIN -
                      APP_IMAGE_WEATHER_CLEAR_DAY_MAIN;
    if (code == 100U)
    {
        offset = 0U;
    }
    else if (code == 150U)
    {
        offset = 1U;
    }
    else if (code >= 101U && code <= 103U)
    {
        offset = 2U;
    }
    else if (code >= 151U && code <= 153U)
    {
        offset = 3U;
    }
    else if (code == 104U)
    {
        offset = 5U;
    }
    else if (code == 305U || code == 309U)
    {
        offset = 6U;
    }
    else if ((code >= 300U && code <= 301U) || code == 306U ||
             code == 313U || (code >= 314U && code <= 316U) ||
             code == 350U || code == 351U || code == 399U)
    {
        offset = 7U;
    }
    else if (code == 307U || code == 308U ||
             (code >= 310U && code <= 312U) ||
             (code >= 317U && code <= 318U))
    {
        offset = 8U;
    }
    else if (code == 304U)
    {
        offset = 10U;
    }
    else if (code >= 302U && code <= 303U)
    {
        offset = 9U;
    }
    else if (code == 404U || code == 405U || code == 406U)
    {
        offset = 14U;
    }
    else if (code == 400U || code == 407U)
    {
        offset = 12U;
    }
    else if (code >= 401U && code <= 403U)
    {
        offset = 13U;
    }
    else if (code == 500U || code == 501U)
    {
        offset = 15U;
    }
    else if (code >= 502U && code <= 515U)
    {
        offset = 16U;
    }
    else if (code == 900U)
    {
        offset = 17U;
    }
    else if (code == 901U)
    {
        offset = 18U;
    }
    return (small ? APP_IMAGE_WEATHER_CLEAR_DAY_SMALL :
            APP_IMAGE_WEATHER_CLEAR_DAY_MAIN) + offset;
}

lv_obj_t *weather_ui_text_label(lv_obj_t *parent,
                                app_theme_font_id_t font_id)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, app_ui_font(font_id), 0);
    return label;
}

lv_obj_t *weather_ui_symbol_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    return label;
}

lv_obj_t *weather_ui_surface(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *surface = lv_obj_create(parent);
    lv_obj_remove_style_all(surface);
    lv_obj_set_width(surface, LV_PCT(100));
    lv_obj_set_height(surface, height);
    lv_obj_set_style_bg_color(surface, lv_color_hex(WEATHER_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(surface, 6, 0);
    lv_obj_set_style_pad_all(surface, 10, 0);
    lv_obj_remove_flag(surface,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return surface;
}

lv_obj_t *weather_ui_container(lv_obj_t *parent, int32_t height,
                               int flex_flow)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, height);
    lv_obj_set_flex_flow(container, flex_flow);
    lv_obj_remove_flag(container,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return container;
}

bool weather_ui_set_image(lv_obj_t *image, uint16_t code, bool small)
{
    const lv_image_dsc_t *descriptor = NULL;
    if (app_manager_get_image(_weather_ui_image_id(code, small),
                              &descriptor) != ESP_OK)
    {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    lv_image_set_src(image, descriptor);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    return true;
}

lv_obj_t *weather_ui_small_icon(lv_obj_t *parent, uint16_t code)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_obj_set_size(image, 40, 40);
    if (weather_ui_set_image(image, code, true))
    {
        return image;
    }
    lv_obj_delete(image);
    lv_obj_t *fallback = weather_ui_symbol_label(parent);
    lv_obj_set_size(fallback, 40, 40);
    lv_obj_set_style_text_align(fallback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(fallback, lv_color_hex(WEATHER_COLOR_RAIN), 0);
    lv_label_set_text(fallback, LV_SYMBOL_IMAGE);
    return fallback;
}

void weather_ui_format_time(const weather_service_time_t *source,
                            const char *format, char *output,
                            size_t output_size)
{
    time_t adjusted = (time_t)(source->epoch_seconds +
                               (int64_t)source->offset_minutes * 60);
    struct tm value;
    if (source->epoch_seconds <= 0 || gmtime_r(&adjusted, &value) == NULL ||
            strftime(output, output_size, format, &value) == 0U)
    {
        if (output_size > 0U)
        {
            output[0] = '\0';
        }
    }
}

void weather_ui_format_dataset_time(
    const weather_service_dataset_meta_t *meta, char *output,
    size_t output_size)
{
    const weather_service_time_t *time = &meta->updated_at;
    if (time->epoch_seconds <= 0)
    {
        time = &meta->fetched_at;
    }
    weather_ui_format_time(time, "%m-%d %H:%M", output, output_size);
}

const char *weather_ui_state_text(weather_service_state_t state)
{
    switch (state)
    {
    case WEATHER_SERVICE_STATE_UNCONFIGURED:
        return "服务未配置";
    case WEATHER_SERVICE_STATE_WAITING_NETWORK:
        return "等待网络";
    case WEATHER_SERVICE_STATE_LOCATING:
        return "正在定位";
    case WEATHER_SERVICE_STATE_UPDATING:
        return "正在更新";
    case WEATHER_SERVICE_STATE_READY:
        return "已更新";
    case WEATHER_SERVICE_STATE_DEGRADED:
        return "使用缓存数据";
    case WEATHER_SERVICE_STATE_AUTH_ERROR:
        return "服务认证失败，显示已有数据";
    case WEATHER_SERVICE_STATE_RATE_LIMITED:
        return "请求受限，显示已有数据";
    case WEATHER_SERVICE_STATE_SUSPENDED:
        return "服务已暂停";
    case WEATHER_SERVICE_STATE_ERROR:
    default:
        return "天气暂不可用";
    }
}

const char *weather_ui_state_short_text(weather_service_state_t state)
{
    switch (state)
    {
    case WEATHER_SERVICE_STATE_UNCONFIGURED:
        return "服务未配置";
    case WEATHER_SERVICE_STATE_WAITING_NETWORK:
        return "等待网络";
    case WEATHER_SERVICE_STATE_LOCATING:
        return "正在定位";
    case WEATHER_SERVICE_STATE_UPDATING:
        return "正在更新";
    case WEATHER_SERVICE_STATE_READY:
        return "";
    case WEATHER_SERVICE_STATE_DEGRADED:
        return "服务降级";
    case WEATHER_SERVICE_STATE_AUTH_ERROR:
        return "认证失败";
    case WEATHER_SERVICE_STATE_RATE_LIMITED:
        return "请求受限";
    case WEATHER_SERVICE_STATE_SUSPENDED:
        return "服务已暂停";
    case WEATHER_SERVICE_STATE_ERROR:
    default:
        return "天气不可用";
    }
}

app_ui_status_t weather_ui_state_color(weather_service_state_t state)
{
    if (state == WEATHER_SERVICE_STATE_READY)
    {
        return APP_UI_STATUS_SUCCESS;
    }
    if (state == WEATHER_SERVICE_STATE_LOCATING ||
            state == WEATHER_SERVICE_STATE_UPDATING ||
            state == WEATHER_SERVICE_STATE_WAITING_NETWORK)
    {
        return APP_UI_STATUS_ACCENT;
    }
    if (state == WEATHER_SERVICE_STATE_DEGRADED ||
            state == WEATHER_SERVICE_STATE_RATE_LIMITED ||
            state == WEATHER_SERVICE_STATE_SUSPENDED)
    {
        return APP_UI_STATUS_WARNING;
    }
    return APP_UI_STATUS_ERROR;
}

const char *weather_ui_dataset_state(
    const weather_service_dataset_meta_t *meta)
{
    if (!meta->available)
    {
        return "不可用";
    }
    if (meta->expired)
    {
        return "已过期";
    }
    if (meta->stale)
    {
        return "缓存数据";
    }
    return "有效";
}

void weather_ui_release_snapshot(
    const weather_service_snapshot_t **snapshot)
{
    if (*snapshot != NULL)
    {
        weather_service_snapshot_release(*snapshot);
        *snapshot = NULL;
    }
}

esp_err_t weather_ui_unsubscribe(event_bus_sub_handle_t *subscription)
{
    if (*subscription == EVENT_BUS_SUB_HANDLE_INVALID)
    {
        return ESP_OK;
    }
    esp_err_t result = event_bus_unsubscribe(*subscription);
    if (result == ESP_OK || result == ESP_ERR_NOT_FOUND)
    {
        *subscription = EVENT_BUS_SUB_HANDLE_INVALID;
        return ESP_OK;
    }
    return result;
}

bool weather_ui_is_snapshot_event(event_bus_msg_id_t msg_id,
                                  uint32_t sub_type, const void *payload,
                                  size_t payload_size)
{
    return msg_id == WEATHER_SERVICE_MSG &&
           sub_type == WEATHER_SERVICE_MSG_SUB_TYPE_SNAPSHOT &&
           payload != NULL && payload_size == sizeof(weather_service_event_t);
}
