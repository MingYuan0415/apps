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
#define CLOCK_PAGE_DURATION     "duration"

#define CLOCK_ARGUMENT_MINUTES  1U

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
