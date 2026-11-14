#include "clock_ui_format.h"

#include <stdio.h>

void clock_ui_format_mmss(uint32_t milliseconds, char *output,
                          size_t output_size)
{
    (void)snprintf(output, output_size, "%u:%02u",
                   (unsigned)(milliseconds / 60000U),
                   (unsigned)((milliseconds / 1000U) % 60U));
}

void clock_ui_format_elapsed(uint64_t milliseconds, char *output,
                             size_t output_size)
{
    const uint64_t total_seconds = milliseconds / 1000ULL;
    const uint64_t hours = total_seconds / 3600ULL;
    const uint64_t minutes = (total_seconds / 60ULL) % 60ULL;
    const uint64_t seconds = total_seconds % 60ULL;
    if (hours > 0ULL)
    {
        (void)snprintf(output, output_size, "%llu:%02llu:%02llu",
                       (unsigned long long)hours,
                       (unsigned long long)minutes,
                       (unsigned long long)seconds);
        return;
    }
    (void)snprintf(output, output_size, "%llu:%02llu",
                   (unsigned long long)(total_seconds / 60ULL),
                   (unsigned long long)seconds);
}
