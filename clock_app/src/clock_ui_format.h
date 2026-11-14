#ifndef __CLOCK_UI_FORMAT_H__
#define __CLOCK_UI_FORMAT_H__

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Format milliseconds as minutes:seconds (minutes may exceed 99).
 * @param milliseconds is the duration to render.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void clock_ui_format_mmss(uint32_t milliseconds, char *output,
                          size_t output_size);

/**
 * @brief Format elapsed stopwatch time without wrapping at one hour.
 * @param milliseconds is the cumulative elapsed duration.
 * @param output receives M:SS below one hour and H:MM:SS otherwise.
 * @param output_size is the output buffer size.
 */
void clock_ui_format_elapsed(uint64_t milliseconds, char *output,
                             size_t output_size);

#endif /* __CLOCK_UI_FORMAT_H__ */
