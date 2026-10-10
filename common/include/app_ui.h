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
 * @brief Create one shared UI glyph image, or a symbol fallback.
 *
 * Resolves @p image_id through app_manager_get_image(); when the image is
 * unavailable it creates a centered LVGL-symbol label instead, so callers never
 * depend on the resource partition being populated.
 *
 * @param parent is the LVGL parent object.
 * @param image_id is the semantic image ID, or zero to force the fallback.
 * @param fallback_symbol is the LVGL symbol used when the image is missing.
 * @param size is the square glyph size in pixels; zero selects 28.
 * @return Created image or label object, or NULL on allocation failure.
 */
lv_obj_t *app_ui_image(lv_obj_t *parent, uint32_t image_id,
                       const char *fallback_symbol, int32_t size);

/**
 * @brief Apply one color to a glyph created by app_ui_image.
 *
 * Sets the text color for a symbol fallback and the image recolor for a real
 * image, so a single call tints either representation.
 *
 * @param icon is an app_ui_image result, or NULL.
 * @param color is the RGB color.
 */
void app_ui_icon_set_color(lv_obj_t *icon, uint32_t color);

/** @brief Receive a completed whole-page pull-to-refresh gesture. */
typedef void (*app_ui_pull_refresh_cb_t)(void *user_data);

/** @brief Caller-owned whole-page pull-to-refresh state for one page. */
typedef struct app_ui_pull_refresh
{
    lv_obj_t *root;                  /**< Page root owning the occluded hint. */
    lv_obj_t *hint;                  /**< Top hint container, revealed on pull. */
    lv_obj_t *hint_label;            /**< Hint text label. */
    lv_obj_t *header;                /**< Page header translated with the pull. */
    lv_obj_t *content;               /**< Page scroll owner receiving the drag. */
    app_ui_pull_refresh_cb_t on_refresh; /**< Invoked on release past threshold. */
    void *user_data;                 /**< Forwarded to on_refresh. */
    int32_t start_y;                 /**< Press y captured at gesture start. */
    int32_t offset;                  /**< Current whole-page downward offset. */
    bool armed;                      /**< Pull passed the trigger threshold. */
    bool tracking;                   /**< The gesture may still become a pull. */
    bool refreshing;                 /**< A refresh is in progress. */
    void *timeout;                   /**< Bounded refresh fallback timer, or NULL. */
} app_ui_pull_refresh_t;

/**
 * @brief Enable whole-page pull-to-refresh on one page.
 *
 * Adds an occluded hint strip at the top of the page and translates the header
 * and content downward as the user drags over the content scroll owner,
 * revealing the hint. Release past the threshold (about 25 px of 56) fires
 * on_refresh; the page then stays open at the hint until
 * app_ui_pull_refresh_set_refreshing(refresh, false). A drag that starts on a
 * clickable row is left to the row / normal scrolling.
 *
 * @param refresh is caller-owned state that must outlive the page content.
 * @param page supplies the root, header, and content scroll owner.
 * @param on_refresh runs on the UI worker after the gesture commits.
 * @param user_data is forwarded to on_refresh.
 */
void app_ui_pull_refresh_attach(app_ui_pull_refresh_t *refresh,
                                app_ui_page_t *page,
                                app_ui_pull_refresh_cb_t on_refresh,
                                void *user_data);

/**
 * @brief Finish a refresh and animate the page back to rest.
 * @param refresh is the state passed to app_ui_pull_refresh_attach.
 * @param refreshing is false to close the pull; true is a no-op.
 */
void app_ui_pull_refresh_set_refreshing(app_ui_pull_refresh_t *refresh,
                                        bool refreshing);

/**
 * @brief Forget a pull-to-refresh registration before the page is destroyed.
 * @param refresh is the state passed to app_ui_pull_refresh_attach.
 */
void app_ui_pull_refresh_detach(app_ui_pull_refresh_t *refresh);
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

/** @brief app_ui_add_action with a shared SVG glyph instead of a symbol. */
lv_obj_t *app_ui_add_action_image(lv_obj_t *parent, uint32_t image_id,
                                  const char *fallback_symbol,
                                  const char *title, const char *subtitle,
                                  lv_event_cb_t callback, void *user_data);
/** @brief app_ui_add_command with a shared SVG glyph instead of a symbol. */
lv_obj_t *app_ui_add_command_image(lv_obj_t *parent, uint32_t image_id,
                                   const char *fallback_symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data);
/** @brief app_ui_add_danger_action with a shared SVG glyph. */
lv_obj_t *app_ui_add_danger_action_image(lv_obj_t *parent, uint32_t image_id,
        const char *fallback_symbol,
        const char *title,
        const char *subtitle,
        lv_event_cb_t callback,
        void *user_data);
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
 * @brief Create a 44 px selectable chip row.
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
 * @brief Add a launcher tile: square icon well plus centered caption.
 *
 * The caller owns the tile size: set an explicit width for a launcher grid
 * cell or leave the default height for a dock cell. The caption is a single
 * DOT line so it never wraps inside the tile.
 *
 * @param parent is the grid or row that owns the tile.
 * @param image_id is the semantic image ID, or zero to use the symbol.
 * @param fallback_symbol is drawn when the image is unavailable.
 * @param label is the caption under the icon.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created tile, or NULL when allocation fails.
 */
lv_obj_t *app_ui_add_icon_tile(lv_obj_t *parent, uint32_t image_id,
                               const char *fallback_symbol, const char *label,
                               lv_event_cb_t callback, void *user_data);

/**
 * @brief Add a centered empty-state block with a title and optional hint.
 * @param parent is the page content owning the state.
 * @param title is the primary line; NULL or empty renders no title.
 * @param subtitle is the optional secondary hint; NULL renders no hint.
 * @return Created container object.
 */
lv_obj_t *app_ui_add_empty_state(lv_obj_t *parent, const char *title,
                                 const char *subtitle);

/**
 * @brief Create a rounded card that groups inset rows.
 *
 * Rows added with the app_ui_group_* helpers are transparent and share the
 * group surface, giving a phone-style grouped list instead of one card per
 * row. A NULL parent returns NULL.
 */
lv_obj_t *app_ui_group_create(lv_obj_t *parent);

/** @brief Append a tappable navigation row (icon, title, subtitle, chevron). */
lv_obj_t *app_ui_group_add_nav(lv_obj_t *group, const char *symbol,
                               const char *title, const char *subtitle,
                               lv_event_cb_t callback, void *user_data);
/** @brief Append a tappable command row without a chevron. */
lv_obj_t *app_ui_group_add_command(lv_obj_t *group, const char *symbol,
                                   const char *title, const char *subtitle,
                                   lv_event_cb_t callback, void *user_data);
/** @brief Append a destructive navigation row with a red title. */
lv_obj_t *app_ui_group_add_danger(lv_obj_t *group, const char *symbol,
                                  const char *title, const char *subtitle,
                                  lv_event_cb_t callback, void *user_data);

/** @brief app_ui_group_add_nav with a shared SVG glyph. */
lv_obj_t *app_ui_group_add_nav_image(lv_obj_t *group, uint32_t image_id,
                                     const char *fallback_symbol,
                                     const char *title, const char *subtitle,
                                     lv_event_cb_t callback, void *user_data);
/** @brief app_ui_group_add_command with a shared SVG glyph. */
lv_obj_t *app_ui_group_add_command_image(lv_obj_t *group, uint32_t image_id,
        const char *fallback_symbol,
        const char *title,
        const char *subtitle,
        lv_event_cb_t callback,
        void *user_data);
/** @brief app_ui_group_add_danger with a shared SVG glyph. */
lv_obj_t *app_ui_group_add_danger_image(lv_obj_t *group, uint32_t image_id,
                                        const char *fallback_symbol,
                                        const char *title,
                                        const char *subtitle,
                                        lv_event_cb_t callback,
                                        void *user_data);
/**
 * @brief Append a title + live-summary navigation row.
 * @param summary_out receives the muted summary label for later updates.
 */
lv_obj_t *app_ui_group_add_entry(lv_obj_t *group, const char *title,
                                 lv_obj_t **summary_out,
                                 lv_event_cb_t callback, void *user_data);
/**
 * @brief Append a name/value row.
 * @param value_out optionally receives the value label.
 */
lv_obj_t *app_ui_group_add_value(lv_obj_t *group, const char *name,
                                 const char *value, lv_obj_t **value_out);
/**
 * @brief Append a title/subtitle row with a trailing switch.
 * @param switch_out optionally receives the switch.
 */
lv_obj_t *app_ui_group_add_switch(lv_obj_t *group, const char *title,
                                  const char *subtitle,
                                  lv_event_cb_t callback, void *user_data,
                                  lv_obj_t **switch_out);

/**
 * @brief Add or replace the header trailing action button.
 * @param page owns the header.
 * @param image_id is the semantic image ID, or zero for the symbol.
 * @param fallback_symbol is drawn when the image is unavailable.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button, or NULL when the page is headerless.
 */
lv_obj_t *app_ui_page_set_action(app_ui_page_t *page, uint32_t image_id,
                                 const char *fallback_symbol,
                                 lv_event_cb_t callback, void *user_data);

/**
 * @brief Open a modal bottom sheet over a page root.
 *
 * Creates a dimming scrim plus a bottom-anchored panel carrying a title and an
 * optional message. The caller adds action rows with app_ui_sheet_add_action
 * and closes the sheet with app_ui_sheet_dismiss. The scrim swallows touches so
 * the covered page receives none while the sheet is open; the panel keeps the
 * page's full-bleed opaque root intact for snapshot transitions.
 *
 * @param parent is the page root that owns the sheet.
 * @param title is the sheet heading.
 * @param message is optional supporting copy; NULL renders none.
 * @return Sheet handle (the scrim), or NULL on allocation failure.
 */
lv_obj_t *app_ui_sheet_open(lv_obj_t *parent, const char *title,
                            const char *message);

/**
 * @brief Append a full-width action row to a sheet.
 * @param sheet is an app_ui_sheet_open handle.
 * @param text is the action caption.
 * @param danger renders the caption in the destructive color when true.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button, or NULL on failure.
 */
lv_obj_t *app_ui_sheet_add_action(lv_obj_t *sheet, const char *text,
                                  bool danger, lv_event_cb_t callback,
                                  void *user_data);

/**
 * @brief Close and release a sheet after its action completes.
 *
 * Hides the sheet immediately and frees it on the next UI cycle so the caller
 * never destroys an object inside its own CLICKED callback.
 *
 * @param sheet is an app_ui_sheet_open handle, or NULL.
 */
void app_ui_sheet_dismiss(lv_obj_t *sheet);

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
 * @brief Add a title/subtitle row with a right-aligned on/off switch.
 * @param parent is the page content owning the row.
 * @param title is the primary row text.
 * @param subtitle is the optional secondary text (NULL hides it).
 * @param callback receives the switch LV_EVENT_VALUE_CHANGED.
 * @param user_data is retained as LVGL event user data.
 * @param switch_out optionally receives the created switch for state updates.
 * @return Created row object; the whole row is clickable and toggles the
 *         switch (which then fires @p callback).
 */
lv_obj_t *app_ui_add_switch_row(lv_obj_t *parent, const char *title,
                                const char *subtitle, lv_event_cb_t callback,
                                void *user_data, lv_obj_t **switch_out);
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
 * @brief Update a label's text only when it differs from the current one.
 * @param label is the LVGL label to update.
 * @param text is the desired label text.
 */
void app_ui_label_set_text_if(lv_obj_t *label, const char *text);

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
