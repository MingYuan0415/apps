/* Emissive palette guard: raw hexes live only in app_theme_colors.h, and the
 * language is defined by the contrast floors asserted here. Run on host. */

#include <math.h>
#include <stdio.h>

#include "app_theme_colors.h"

typedef struct
{
    const char *name;
    unsigned fg;
    unsigned bg;
    double floor_ratio;
} contrast_case_t;

static double _channel_luma(unsigned value)
{
    const double scaled = (double)value / 255.0;
    return scaled <= 0.04045 ? scaled / 12.92 :
           pow((scaled + 0.055) / 1.055, 2.4);
}

static double _luma(unsigned rgb)
{
    return 0.2126 * _channel_luma((rgb >> 16) & 0xFFU) +
           0.7152 * _channel_luma((rgb >> 8) & 0xFFU) +
           0.0722 * _channel_luma(rgb & 0xFFU);
}

static double _contrast(unsigned a, unsigned b)
{
    const double la = _luma(a);
    const double lb = _luma(b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

static const contrast_case_t s_cases[] =
{
    { "INK/VOID",           APP_THEME_COLOR_INK,       APP_THEME_COLOR_VOID,       15.0 },
    { "INK/PLUME",          APP_THEME_COLOR_INK,       APP_THEME_COLOR_PLUME,      12.0 },
    { "INK/PLUME_HI",       APP_THEME_COLOR_INK,       APP_THEME_COLOR_PLUME_HI,   10.0 },
    { "INK_SOFT/VOID",      APP_THEME_COLOR_INK_SOFT,  APP_THEME_COLOR_VOID,        7.0 },
    { "INK_SOFT/PLUME",     APP_THEME_COLOR_INK_SOFT,  APP_THEME_COLOR_PLUME,       6.0 },
    { "INK_SOFT/PLUME_HI",  APP_THEME_COLOR_INK_SOFT,  APP_THEME_COLOR_PLUME_HI,    4.5 },
    { "AZURE/VOID",         APP_THEME_COLOR_AZURE,     APP_THEME_COLOR_VOID,        7.0 },
    { "AZURE/PLUME",        APP_THEME_COLOR_AZURE,     APP_THEME_COLOR_PLUME,       4.5 },
    { "AMBER/VOID",         APP_THEME_COLOR_AMBER,     APP_THEME_COLOR_VOID,        7.0 },
    { "AMBER/PLUME",        APP_THEME_COLOR_AMBER,     APP_THEME_COLOR_PLUME,       4.5 },
    { "MINT/VOID",          APP_THEME_COLOR_MINT,      APP_THEME_COLOR_VOID,        7.0 },
    { "MINT/PLUME",         APP_THEME_COLOR_MINT,      APP_THEME_COLOR_PLUME,       4.5 },
    { "CORAL/VOID",         APP_THEME_COLOR_CORAL,     APP_THEME_COLOR_VOID,        4.5 },
    { "CORAL/PLUME",        APP_THEME_COLOR_CORAL,     APP_THEME_COLOR_PLUME,       4.5 },
    { "ON_ACCENT/AZURE",    APP_THEME_COLOR_ON_ACCENT, APP_THEME_COLOR_AZURE,       7.0 },
    { "ON_ACCENT/AMBER",    APP_THEME_COLOR_ON_ACCENT, APP_THEME_COLOR_AMBER,       7.0 },
    { "ON_ACCENT/MINT",     APP_THEME_COLOR_ON_ACCENT, APP_THEME_COLOR_MINT,        7.0 },
};

int main(void)
{
    int failures = 0;

    for (size_t i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); i++)
    {
        const double ratio = _contrast(s_cases[i].fg, s_cases[i].bg);
        if (ratio < s_cases[i].floor_ratio)
        {
            printf("FAIL %-16s %.2f:1 below %.1f:1\n", s_cases[i].name,
                   ratio, s_cases[i].floor_ratio);
            failures++;
        }
    }

    /* Language invariants: void is the canvas, lightness ascends through the
     * two elevation steps, ink is never white (bloom), accents stay bright
     * enough to glow against the void. */
    if (APP_THEME_COLOR_VOID != 0x000000)
    {
        printf("FAIL canvas must be true black on AMOLED\n");
        failures++;
    }
    if (!(_luma(APP_THEME_COLOR_PLUME) < _luma(APP_THEME_COLOR_PLUME_HI) &&
            _luma(APP_THEME_COLOR_PLUME_HI) < _luma(APP_THEME_COLOR_INK_SOFT)))
    {
        printf("FAIL elevation ramp PLUME < PLUME_HI < ink is not monotonic\n");
        failures++;
    }
    if (_luma(APP_THEME_COLOR_INK) > 0.93)
    {
        printf("FAIL primary ink too close to pure white\n");
        failures++;
    }

    if (failures != 0)
    {
        printf("theme_contrast_test: %d failures\n", failures);
        return 1;
    }
    printf("theme_contrast_test: palette OK\n");
    return 0;
}
