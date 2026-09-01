#ifndef __APP_UI_H__
#define __APP_UI_H__

#include <stdbool.h>

#include "app_manager.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Common objects owned by one application page. */
typedef struct app_ui_page
{
    lv_obj_t *root;    /**< Full-screen root object. */
    lv_obj_t *header;  /**< Fixed page header. */
    lv_obj_t *header_text; /**< Passive title/subtitle container. */
    lv_obj_t *content; /**< Scrollable page content. */
    lv_obj_t *title;   /**< Header title label. */
    lv_obj_t *subtitle; /**< Optional header subtitle label. */
} app_ui_page_t;

/** @brief Semantic color applied to dynamic status text. */
typedef enum
{
    APP_UI_STATUS_NEUTRAL = 0, /**< Secondary informational text. */
    APP_UI_STATUS_ACCENT,      /**< Active or in-progress state. */
    APP_UI_STATUS_SUCCESS,     /**< Successful or available state. */
    APP_UI_STATUS_WARNING,     /**< Degraded state requiring attention. */
    APP_UI_STATUS_ERROR,       /**< Failed or unavailable state. */
} app_ui_status_t;

/**
 * @brief Create and style one full-screen application page.
 * @param page receives the created LVGL object handles.
 * @param title is the page header text.
 * @param show_back is retained for source compatibility; navigation uses the
 * App Manager edge gesture and system back path.
 */
void app_ui_page_create(app_ui_page_t *page, const char *title, bool show_back);

/**
 * @brief Create the headerless full-screen Home page layout.
 * @param page receives the created root and content objects.
 */
void app_ui_page_create_home(app_ui_page_t *page);

/**
 * @brief Replace the primary text in a page header.
 * @param page owns the header.
 * @param title is copied by LVGL into the title label.
 */
void app_ui_page_set_title(app_ui_page_t *page, const char *title);

/**
 * @brief Add or replace the secondary text in a page header.
 * @param page owns the header.
 * @param subtitle is optional; NULL or an empty string clears the subtitle.
 */
void app_ui_page_set_subtitle(app_ui_page_t *page, const char *subtitle);
/**
 * @brief Delete a page root and clear all stored object pointers.
 * @param page owns the page objects to delete.
 */
void app_ui_page_destroy(app_ui_page_t *page);

/**
 * @brief Add a section label to a page container.
 * @param parent is the LVGL parent object.
 * @param text is the section label text.
 * @return Created label object.
 */
lv_obj_t *app_ui_add_section(lv_obj_t *parent, const char *text);
/**
 * @brief Add a clickable application action row.
 * @param parent is the LVGL parent object.
 * @param symbol is the optional LVGL symbol text.
 * @param title is the primary row text.
 * @param subtitle is the optional secondary text.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created action object.
 */
lv_obj_t *app_ui_add_action(lv_obj_t *parent, const char *symbol,
                            const char *title, const char *subtitle,
                            lv_event_cb_t callback, void *user_data);
/**
 * @brief Add an immediate command row without a navigation chevron.
 * @param parent is the LVGL parent object.
 * @param symbol is the optional LVGL symbol text.
 * @param title is the primary row text.
 * @param subtitle is the optional secondary text.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created command object.
 */
lv_obj_t *app_ui_add_command(lv_obj_t *parent, const char *symbol,
                             const char *title, const char *subtitle,
                             lv_event_cb_t callback, void *user_data);

/**
 * @brief Add a fixed-size semantic icon button.
 * @param parent is the row or container that owns the button.
 * @param image_id is the semantic image ID, or zero to skip image lookup.
 * @param fallback_symbol is used when the image is unavailable.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button, or NULL when allocation fails.
 */
lv_obj_t *app_ui_add_icon_button(lv_obj_t *parent, uint32_t image_id,
                                 const char *fallback_symbol,
                                 lv_event_cb_t callback, void *user_data);
/**
 * @brief Add a name/value row and optionally return its value label.
 * @param parent is the LVGL parent object.
 * @param name is the row name.
 * @param value is the displayed value.
 * @param value_label optionally receives the created value label.
 * @return Created row object.
 */
lv_obj_t *app_ui_add_value_row(lv_obj_t *parent, const char *name,
                               const char *value, lv_obj_t **value_label);
/**
 * @brief Add a wrapped body-text label.
 * @param parent is the LVGL parent object.
 * @param text is the body text.
 * @return Created label object.
 */
lv_obj_t *app_ui_add_body_label(lv_obj_t *parent, const char *text);
/**
 * @brief Update label text and apply a semantic status color.
 * @param label is the LVGL label to update.
 * @param text is the new label text.
 * @param status selects the semantic text color.
 */
void app_ui_set_status_text(lv_obj_t *label, const char *text,
                            app_ui_status_t status);

/**
 * @brief Return a loaded theme font, falling back to the LVGL default.
 * @param id selects the theme font role.
 * @return Loaded font or LV_FONT_DEFAULT.
 */
const lv_font_t *app_ui_font(app_theme_font_id_t id);
/**
 * @brief Configure a generic object as a passive layout container.
 * @param object is the object to configure.
 * @param scrollable keeps vertical scrolling when true.
 */
void app_ui_make_passive(lv_obj_t *object, bool scrollable);
/** @brief Queue navigation back from an LVGL event callback. */
void app_ui_request_back(void);
/**
 * @brief Queue navigation using an application identifier with stable storage.
 *
 * @note The identifier is copied before this function returns.
 *
 * @param app_id identifies the application to run.
 */
void app_ui_request_run(const char *app_id);
/**
 * @brief Queue navigation to one statically described application page.
 *
 * @note Both identifiers are copied before this function returns.
 *
 * @param app_id identifies the owning application.
 * @param page_id identifies the static page.
 */
void app_ui_request_open_page(const char *app_id, const char *page_id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __APP_UI_H__ */
