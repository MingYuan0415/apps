#ifndef __RECORDER_APP_INTERNAL_H__
#define __RECORDER_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "recorder_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORDER_PAGE_FILES     "files"

extern const app_manager_page_definition_t recorder_root_page_definition;
extern const app_manager_page_definition_t recorder_files_page_definition;

/**
 * @brief Create a grow-width control button with a centered caption.
 * @param parent is the flex row owning the button.
 * @param text is the caption.
 * @param height is the button height in pixels.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button.
 */
lv_obj_t *recorder_ui_control(lv_obj_t *parent, const char *text,
                              int32_t height, lv_event_cb_t callback,
                              void *user_data);
/**
 * @brief Replace the caption of a recorder_ui_control button.
 * @param button is a recorder_ui_control button.
 * @param text is the new caption.
 */
void recorder_ui_control_set_text(lv_obj_t *button, const char *text);
/**
 * @brief Show or hide a control without disturbing flex layout.
 * @param control is the object to toggle.
 * @param visible shows the object when true.
 */
void recorder_ui_set_visible(lv_obj_t *control, bool visible);
/**
 * @brief Format milliseconds as minutes:seconds.
 * @param milliseconds is the duration to render.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void recorder_ui_format_mmss(uint32_t milliseconds, char *output,
                             size_t output_size);
/**
 * @brief Strip the directory and extension from a recording name.
 * @param name is the full path stored by the service.
 * @return Pointer into name suitable for display.
 */
const char *recorder_ui_display_name(const char *name);

#endif /* __RECORDER_APP_INTERNAL_H__ */
