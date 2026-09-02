#ifndef __WEATHER_APP_INTERNAL_H__
#define __WEATHER_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "event_bus.h"
#include "weather_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEATHER_PAGE_FORECAST   "forecast"
#define WEATHER_PAGE_ALERTS     "alerts"
#define WEATHER_PAGE_DETAIL     "alert-detail"
#define WEATHER_ARGUMENT_ALERT_KEY 1U

#define WEATHER_COLOR_BACKGROUND APP_UI_COLOR_BACKGROUND
#define WEATHER_COLOR_SURFACE    APP_UI_COLOR_SURFACE
#define WEATHER_COLOR_SURFACE_HI APP_UI_COLOR_SURFACE_HI
#define WEATHER_COLOR_TEXT       APP_UI_COLOR_TEXT
#define WEATHER_COLOR_MUTED      APP_UI_COLOR_MUTED
#define WEATHER_COLOR_SUN        APP_UI_COLOR_SUN
#define WEATHER_COLOR_RAIN       APP_UI_COLOR_RAIN
#define WEATHER_COLOR_WARNING    APP_UI_COLOR_WARNING
#define WEATHER_COLOR_WARNING_BG APP_UI_COLOR_WARNING_BG

typedef struct weather_alert_arguments
{
    uint64_t alert_key;
} weather_alert_arguments_t;

_Static_assert(sizeof(weather_alert_arguments_t) <=
               APP_MANAGER_TYPED_BLOB_PAYLOAD_BYTES,
               "Weather alert arguments exceed the Typed Blob payload");

extern const app_manager_page_definition_t weather_root_page_definition;
extern const app_manager_page_definition_t weather_forecast_page_definition;
extern const app_manager_page_definition_t weather_alerts_page_definition;
extern const app_manager_page_definition_t weather_alert_detail_page_definition;

lv_obj_t *weather_ui_text_label(lv_obj_t *parent,
                                app_theme_font_id_t font_id);
lv_obj_t *weather_ui_symbol_label(lv_obj_t *parent);
lv_obj_t *weather_ui_surface(lv_obj_t *parent, int32_t height);
lv_obj_t *weather_ui_container(lv_obj_t *parent, int32_t height,
                               int flex_flow);
lv_obj_t *weather_ui_small_icon(lv_obj_t *parent, uint16_t code);
bool weather_ui_set_image(lv_obj_t *image, uint16_t code, bool small);
void weather_ui_format_time(const weather_service_time_t *source,
                            const char *format, char *output,
                            size_t output_size);
void weather_ui_format_dataset_time(
    const weather_service_dataset_meta_t *meta, char *output,
    size_t output_size);
const char *weather_ui_state_text(weather_service_state_t state);
const char *weather_ui_state_short_text(weather_service_state_t state);
app_ui_status_t weather_ui_state_color(weather_service_state_t state);
const char *weather_ui_dataset_state(
    const weather_service_dataset_meta_t *meta);
void weather_ui_release_snapshot(
    const weather_service_snapshot_t **snapshot);
esp_err_t weather_ui_unsubscribe(event_bus_sub_handle_t *subscription);
bool weather_ui_is_snapshot_event(event_bus_msg_id_t msg_id,
                                  uint32_t sub_type, const void *payload,
                                  size_t payload_size);

#endif /* __WEATHER_APP_INTERNAL_H__ */
