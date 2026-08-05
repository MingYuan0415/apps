#ifndef __WEATHER_APP_INTERNAL_H__
#define __WEATHER_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_manager_image_ids.h"
#include "app_ui.h"
#include "event_bus.h"
#include "weather_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEATHER_PAGE_SLOT_BYTES 2728U
#define WEATHER_PAGE_FORECAST   "forecast"
#define WEATHER_PAGE_ALERTS     "alerts"
#define WEATHER_PAGE_DETAIL     "alert-detail"

#define WEATHER_COLOR_BACKGROUND 0x090D0F
#define WEATHER_COLOR_SURFACE    0x151B1F
#define WEATHER_COLOR_SURFACE_HI 0x20282D
#define WEATHER_COLOR_TEXT       0xF2F5F6
#define WEATHER_COLOR_MUTED      0x93A0A6
#define WEATHER_COLOR_SUN        0xF5C451
#define WEATHER_COLOR_RAIN       0x4FC4D8
#define WEATHER_COLOR_WARNING    0xFF756C
#define WEATHER_COLOR_WARNING_BG 0x3B2021

extern const app_manager_page_definition_t weather_root_page_definition;
extern const app_manager_page_definition_t weather_forecast_page_definition;
extern const app_manager_page_definition_t weather_alerts_page_definition;
extern const app_manager_page_definition_t weather_alert_detail_page_definition;

lv_obj_t *weather_ui_text_label(lv_obj_t *parent, const char *text,
                                app_theme_font_id_t font_id);
lv_obj_t *weather_ui_symbol_label(lv_obj_t *parent, const char *symbol);
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
