#ifndef __CLOCK_APP_INTERNAL_H__
#define __CLOCK_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "clock_ui_format.h"
#include "time_service.h"
#include "timer_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CLOCK_PAGE_COUNTDOWN    "countdown"
#define CLOCK_PAGE_STOPWATCH    "stopwatch"
#define CLOCK_PAGE_FOCUS        "focus"
#define CLOCK_PAGE_DURATION     "duration"

#define CLOCK_ARGUMENT_MINUTES  1U
#define CLOCK_UI_PULSE_MS       120U

typedef struct clock_duration_arguments
{
    uint32_t minutes;
} clock_duration_arguments_t;

_Static_assert(sizeof(clock_duration_arguments_t) <=
               APP_MANAGER_TYPED_BLOB_PAYLOAD_BYTES,
               "Clock duration arguments exceed the Typed Blob payload");

extern const app_manager_page_definition_t clock_root_page_definition;
extern const app_manager_page_definition_t clock_countdown_page_definition;
extern const app_manager_page_definition_t clock_stopwatch_page_definition;
extern const app_manager_page_definition_t clock_focus_page_definition;
extern const app_manager_page_definition_t clock_duration_page_definition;

/** @brief Static App-owned provider for the system control-center card. */
extern const app_manager_control_center_card_ops_t
clock_control_center_card_ops;

/**
 * @brief Open a clock page carrying the selected countdown minutes.
 * @param page_id is the target route inside the clock app.
 * @param minutes is the duration payload.
 */
void clock_ui_open_page_with_minutes(const char *page_id, uint32_t minutes);
/**
 * @brief Read the app-wide countdown duration in minutes.
 * @return Current duration.
 */
uint32_t clock_ui_minutes_get(void);
/**
 * @brief Set the app-wide countdown duration in minutes for this session.
 * @param minutes is clamped to 1..779. RAM-only; reboots return to the
 *        default duration.
 */
void clock_ui_minutes_set(uint32_t minutes);
/**
 * @brief Read a Typed Blob minutes payload if the page was opened with one.
 * @param minutes receives the payload when the argument type matches.
 * @return true when a valid payload was consumed.
 */
bool clock_ui_take_minutes_argument(uint32_t *minutes);

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

/**
 * @brief Pulse one clock object once as acknowledgement of a state change.
 * @param object receives a short opacity breath; NULL is ignored.
 */
void clock_ui_pulse(lv_obj_t *object);

/**
 * @brief Create a tonal clock card with the shared radius and inset.
 * @param parent is the page content.
 * @return Created passive card.
 */
lv_obj_t *clock_ui_card_create(lv_obj_t *parent);

/**
 * @brief Create a centered wrapping clock label.
 * @param parent is the card or row owning the label.
 * @param text is the initial caption.
 * @param font selects the theme role.
 * @param color is the semantic RGB color.
 * @return Created passive label.
 */
lv_obj_t *clock_ui_label_create(lv_obj_t *parent, const char *text,
                                app_theme_font_id_t font, uint32_t color);

#endif /* __CLOCK_APP_INTERNAL_H__ */
