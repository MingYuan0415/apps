#include "clock_ui_format.h"

#include <assert.h>
#include <string.h>

static void _expect_elapsed(uint64_t milliseconds, const char *expected)
{
    char output[32];
    clock_ui_format_elapsed(milliseconds, output, sizeof(output));
    assert(strcmp(output, expected) == 0);
}

static void _expect_mmss(uint32_t milliseconds, const char *expected)
{
    char output[32];
    clock_ui_format_mmss(milliseconds, output, sizeof(output));
    assert(strcmp(output, expected) == 0);
}

int main(void)
{
    _expect_mmss(0U, "0:00");
    _expect_mmss(10U * 60000U, "10:00");
    _expect_mmss(65U * 60000U, "65:00");

    _expect_elapsed(0ULL, "0:00");
    _expect_elapsed(59999ULL, "0:59");
    _expect_elapsed(60000ULL, "1:00");
    _expect_elapsed(3599999ULL, "59:59");
    _expect_elapsed(3600000ULL, "1:00:00");
    _expect_elapsed(3909000ULL, "1:05:09");
    _expect_elapsed(25ULL * 3600000ULL + 2ULL * 60000ULL + 3000ULL,
                    "25:02:03");
    return 0;
}
