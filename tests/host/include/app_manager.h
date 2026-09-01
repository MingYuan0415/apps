/** @file App Manager compatibility declarations for application host tests. */
#ifndef __APPS_HOST_APP_MANAGER_H__
#define __APPS_HOST_APP_MANAGER_H__

#include "app_manager_types.h"

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

/** @brief Admit and copy one fake asynchronous navigation request. */
esp_err_t app_manager_navigate_async(const app_manager_nav_request_t *request,
                                     app_manager_nav_completion_cb_t completion,
                                     void *context);
/** @brief Return the fake screen owned by the current page callback. */
lv_obj_t *app_manager_this_page_screen(void);
/** @brief Return one fake theme font. */
const lv_font_t *app_manager_get_font(app_theme_font_id_t id);
/** @brief Return an image descriptor for a semantic resource id. */
esp_err_t app_manager_get_image(uint32_t semantic_id,
                                const lv_image_dsc_t **image);

#endif /* __APPS_HOST_APP_MANAGER_H__ */
