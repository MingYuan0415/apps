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
 * @brief Add a destructive navigation row with a red title.
 * @param parent is the LVGL parent object.
 * @param symbol is the optional LVGL symbol text.
 * @param title is the primary row text.
 * @param subtitle is the optional secondary text.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created row object.
 */
lv_obj_t *app_ui_add_danger_action(lv_obj_t *parent, const char *symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data);
/**
 * @brief Add a two-line icon-less entry row with a live summary label.
 * @param parent is the page content owning the row.
 * @param title is the row title.
 * @param summary_out receives the muted summary label.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created row button.
 */
lv_obj_t *app_ui_add_entry_row(lv_obj_t *parent, const char *title,
                               lv_obj_t **summary_out,
                               lv_event_cb_t callback, void *user_data);
/**
 * @brief Create an equal-width button row (space-between, passive).
 * @param parent is the page content or card owning the row.
 * @param height is the row height in pixels.
 * @return Created passive row container.
 */
lv_obj_t *app_ui_button_row_create(lv_obj_t *parent, int32_t height);
/**
 * @brief Create a grow-width control button inside an app_ui_button_row.
 * @param row is an app_ui_button_row_create container.
 * @param text is the button caption.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button.
 */
lv_obj_t *app_ui_button_create(lv_obj_t *row, const char *text,
                               lv_event_cb_t callback, void *user_data);
/**
 * @brief Replace the caption of an app_ui_button_create button.
 * @param button is an app_ui_button_create button.
 * @param text is the new caption.
 */
void app_ui_button_set_text(lv_obj_t *button, const char *text);
/**
 * @brief Create a 40 px selectable chip row.
 * @param parent is the page content or card owning the row.
 * @return Created passive row container for app_ui_chip_create.
 */
lv_obj_t *app_ui_chip_row_create(lv_obj_t *parent);
/**
 * @brief Create a grow-width selectable chip inside an app_ui_chip_row.
 * @param row is an app_ui_chip_row_create container.
 * @param text is the chip caption.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created chip button.
 */
lv_obj_t *app_ui_chip_create(lv_obj_t *row, const char *text,
                             lv_event_cb_t callback, void *user_data);
/**
 * @brief Toggle the accent selection state of a chip.
 * @param chip is an app_ui_chip_create button.
 * @param selected uses the accent color when true.
 */
void app_ui_chip_set_selected(lv_obj_t *chip, bool selected);

/**
 * @brief Cancel a press once the finger leaves the control.
 *
 * Removes LV_OBJ_FLAG_PRESS_LOCK so LV_EVENT_CLICKED fires only when the
 * release point is still on the control; moving off sends LV_EVENT_PRESS_LOST
 * and the activation is dropped. Apply to every click-activated control.
 *
 * @param obj is a clickable control activated by LV_EVENT_CLICKED.
 */
void app_ui_click_only(lv_obj_t *obj);

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
 * @brief Create a passive full-circle progress ring starting at 12 o'clock.
 * @param parent is the LVGL parent object.
 * @param size is the ring diameter in pixels.
 * @param width is the arc stroke width for both track and indicator.
 * @param track_color is the RGB background track color.
 * @return Created arc whose INDICATOR part carries the progress sweep.
 */
lv_obj_t *app_ui_ring_create(lv_obj_t *parent, int32_t size, int32_t width,
                             uint32_t track_color);
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
 * @param scrollable keeps the object a hit-testable scroll owner
 * (CLICKABLE|SCROLLABLE) when true, otherwise removes both so it never
 * intercepts taps. Scroll-chain bits are always preserved.
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
