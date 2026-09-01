#ifndef __APP_WEATHER_UI_H__
#define __APP_WEATHER_UI_H__

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set one weather image using the shared semantic resource mapping.
 * @param image is the LVGL image object to update.
 * @param condition_code is the normalized provider condition code.
 * @param small selects the 40 px resource instead of the main resource.
 * @return true when a resource was loaded, otherwise false.
 */
bool app_weather_ui_set_image(lv_obj_t *image, uint16_t condition_code,
                              bool small);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WEATHER_UI_H__ */
