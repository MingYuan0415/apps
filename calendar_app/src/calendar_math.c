#include "calendar_math.h"

#include <stddef.h>
#include <stdint.h>

/* Gregorian civil-date conversions follow the proleptic algorithm that counts
 * days since 1970-01-01; it needs no lookup tables and stays valid for every
 * year the device RTC can hold. */
static int64_t _calendar_math_days_from_civil(int year, int month, int day)
{
    const int adjusted_year = year - (month <= 2 ? 1 : 0);
    const int era = (adjusted_year >= 0 ? adjusted_year :
                     adjusted_year - 399) / 400;
    const int year_of_era = adjusted_year - era * 400;
    const int day_of_year =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

static bool _calendar_math_is_valid_date(int year, int month, int day)
{
    return day >= 1 && day <= calendar_math_days_in_month(year, month);
}

bool calendar_math_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int calendar_math_days_in_month(int year, int month)
{
    static const int days[12] =
    {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && calendar_math_is_leap_year(year))
    {
        return 29;
    }
    return days[month - 1];
}

int calendar_math_weekday(int year, int month, int day)
{
    if (!_calendar_math_is_valid_date(year, month, day))
    {
        return -1;
    }
    /* 1970-01-01 was a Thursday, index 4 when Sunday is 0. */
    const int64_t days = _calendar_math_days_from_civil(year, month, day);
    return (int)((days + 4) % 7 + 7) % 7;
}

int calendar_math_day_of_year(int year, int month, int day)
{
    if (!_calendar_math_is_valid_date(year, month, day))
    {
        return 0;
    }
    static const int days_before_month[12] =
    {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    int ordinal = days_before_month[month - 1] + day;
    if (month > 2 && calendar_math_is_leap_year(year))
    {
        ordinal++;
    }
    return ordinal;
}

/* ISO-8601 weeks start on Monday and week 1 contains the year's first
 * Thursday, so a year is 53 weeks long only when it starts on a Thursday, or
 * on a Wednesday in a leap year. */
static int _calendar_math_iso_weeks_in_year(int year)
{
    const int jan1 = (calendar_math_weekday(year, 1, 1) + 6) % 7; /* 0=Monday */
    const bool long_year = jan1 == 3 ||
                           (jan1 == 2 && calendar_math_is_leap_year(year));
    return long_year ? 53 : 52;
}

int calendar_math_week_of_year(int year, int month, int day)
{
    const int weekday = calendar_math_weekday(year, month, day);
    if (weekday < 0)
    {
        return 0;
    }
    const int ordinal = calendar_math_day_of_year(year, month, day);
    const int iso_weekday = (weekday + 6) % 7 + 1; /* 1=Monday .. 7=Sunday */
    int week = (ordinal - iso_weekday + 10) / 7;
    if (week < 1)
    {
        /* The date belongs to the last week of the previous year. */
        week = _calendar_math_iso_weeks_in_year(year - 1);
    }
    else if (week > _calendar_math_iso_weeks_in_year(year))
    {
        /* The date already belongs to week 1 of the following year. */
        week = 1;
    }
    return week;
}

int calendar_math_build_month(int year, int month, int *cells)
{
    const int days = calendar_math_days_in_month(year, month);
    if (days == 0 || cells == NULL)
    {
        return 0;
    }
    const int first_weekday = calendar_math_weekday(year, month, 1);
    const int rows = (first_weekday + days + CALENDAR_MATH_COLUMNS - 1) /
                     CALENDAR_MATH_COLUMNS;
    const int total = rows * CALENDAR_MATH_COLUMNS;
    for (int index = 0; index < total; index++)
    {
        const int day = index - first_weekday + 1;
        cells[index] = day >= 1 && day <= days ? day : 0;
    }
    return total;
}