#ifndef __CLOCK_APP_INTERNAL_H__
#define __CLOCK_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "time_service.h"
#include "timer_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CLOCK_PAGE_COUNTDOWN    "countdown"
#define CLOCK_PAGE_STOPWATCH    "stopwatch"
#define CLOCK_PAGE_FOCUS        "focus"

extern const app_manager_page_definition_t clock_root_page_definition;
extern const app_manager_page_definition_t clock_countdown_page_definition;
extern const app_manager_page_definition_t clock_stopwatch_page_definition;
extern const app_manager_page_definition_t clock_focus_page_definition;

/**
 * @brief Create a contextual control-row button (grows equally per row).
 * @param parent is the control row container.
 * @param text is the button caption.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button.
 */
lv_obj_t *clock_ui_action_button(lv_obj_t *parent, const char *text,
                                 lv_event_cb_t callback, void *user_data);
/**
 * @brief Replace the caption of a clock_ui_action_button.
 * @param button is a clock_ui_action_button.
 * @param text is the new caption.
 */
void clock_ui_button_set_text(lv_obj_t *button, const char *text);
/**
 * @brief Create a preset chip button with selectable accent state.
 * @param parent is the chip row container.
 * @param text is the chip caption.
 * @param callback receives click events.
 * @param user_data is retained as LVGL event user data.
 * @return Created button.
 */
lv_obj_t *clock_ui_chip(lv_obj_t *parent, const char *text,
                        lv_event_cb_t callback, void *user_data);
/**
 * @brief Set chip text color to signal selection.
 * @param chip is a clock_ui_chip button.
 * @param selected uses the accent color when true.
 */
void clock_ui_chip_set_selected(lv_obj_t *chip, bool selected);
/**
 * @brief Format milliseconds as minutes:seconds (minutes may exceed 99).
 * @param milliseconds is the duration to render.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void clock_ui_format_mmss(uint32_t milliseconds, char *output,
                          size_t output_size);
/**
 * @brief Drive a ring sweep from a remaining/total pair.
 * @param ring is an app_ui_ring_create object.
 * @param active hides the indicator when false.
 * @param remaining_ms is the remaining time.
 * @param total_ms is the phase total, or zero for a full-strength ring.
 * @param color is the indicator RGB color.
 */
void clock_ui_ring_update(lv_obj_t *ring, bool active, uint32_t remaining_ms,
                          uint32_t total_ms, uint32_t color);

#endif /* __CLOCK_APP_INTERNAL_H__ */
