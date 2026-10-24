/** @file App Manager compatibility declarations for application host tests. */
#ifndef __APPS_HOST_APP_MANAGER_H__
#define __APPS_HOST_APP_MANAGER_H__

#include "app_manager_types.h"

/* Mirrors the real descriptor export: host fixtures link one definition per
 * application TU and never read the linker section. */
#define APP_MANAGER_APP_EXPORT_META(n, icon_value, nm, aid, root_id, app_flags, \
                                    routes, order, subtitle) \
    __attribute__((used)) static const app_manager_app_desc_t \
    _app_manager_app_unused_##n = \
    { \
        .id = (aid), \
        .root_page_id = (root_id), \
        .page_routes = (routes), \
        .page_route_count = sizeof(routes) / sizeof((routes)[0]), \
        .display_name = (nm), \
        .launcher_order = (uint16_t)(order), \
        .launcher_subtitle = (subtitle), \
    }

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
/** @brief Queue immediate screen-off in device page fixtures. */
esp_err_t app_manager_pm_request_screen_off(void);
/** @brief Return the fake copied navigation arguments for the current page. */
const app_manager_typed_blob_t *app_manager_this_page_arguments(void);
/** @brief Queue a fake request with stable copied identifiers. */
void app_ui_request_open_page(const char *app_id, const char *page_id);
/** @brief Queue a fake run request with a stable copied identifier. */
void app_ui_request_run(const char *app_id);
/** @brief Return one fake theme font. */
const lv_font_t *app_manager_get_font(app_theme_font_id_t id);
/** @brief Return an image descriptor for a semantic resource id. */
esp_err_t app_manager_get_image(uint32_t semantic_id,
                                const lv_image_dsc_t **image);

#endif /* __APPS_HOST_APP_MANAGER_H__ */
