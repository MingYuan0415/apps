/** @file App Manager compatibility declarations for application host tests. */
#ifndef __APPS_HOST_APP_MANAGER_H__
#define __APPS_HOST_APP_MANAGER_H__

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/** @brief Font roles exposed to application fixtures. */
typedef enum
{
    APP_THEME_FONT_BODY = 0,
    APP_THEME_FONT_SMALL,
    APP_THEME_FONT_HEAD,
    APP_THEME_FONT_BIGL,
    APP_THEME_FONT_HUGE,
    APP_THEME_FONT_TITLE,
    APP_THEME_FONT_MAX,
} app_theme_font_id_t;

/** @brief Record a fake application navigation request. */
esp_err_t app_manager_run(const char *app_id);
/** @brief Record a fake back-navigation request. */
esp_err_t app_manager_goback(void);
/** @brief Queue one callback on the fake UI worker. */
esp_err_t app_manager_ui_post(void (*callback)(void *), void *arg);
/** @brief Return one fake theme font. */
const lv_font_t *app_manager_get_font(app_theme_font_id_t id);

#endif /* __APPS_HOST_APP_MANAGER_H__ */
