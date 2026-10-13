/**
 * @brief  Host test for hardware-independent Gregorian calendar math.
 * @note   Compiles only calendar_math.c; no LVGL or ESP-IDF shims are involved.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "calendar_math.h"

static void test_leap_years(void)
{
    assert(calendar_math_is_leap_year(2024));
    assert(calendar_math_is_leap_year(2000));
    assert(calendar_math_is_leap_year(2400));
    assert(!calendar_math_is_leap_year(2026));
    assert(!calendar_math_is_leap_year(1900));
    assert(!calendar_math_is_leap_year(2100));
}

static void test_days_in_month(void)
{
    assert(calendar_math_days_in_month(2024, 2) == 29);
    assert(calendar_math_days_in_month(2000, 2) == 29);
    assert(calendar_math_days_in_month(2026, 2) == 28);
    assert(calendar_math_days_in_month(1900, 2) == 28);
    assert(calendar_math_days_in_month(2100, 2) == 28);
    assert(calendar_math_days_in_month(2026, 4) == 30);
    assert(calendar_math_days_in_month(2026, 12) == 31);
    assert(calendar_math_days_in_month(2026, 1) == 31);
    assert(calendar_math_days_in_month(2026, 0) == 0);
    assert(calendar_math_days_in_month(2026, 13) == 0);
    assert(calendar_math_days_in_month(2026, -1) == 0);
}

static void test_weekday(void)
{
    /* 0 = Sunday. */
    assert(calendar_math_weekday(1970, 1, 1) == 4);  /* Thursday */
    assert(calendar_math_weekday(2000, 1, 1) == 6);  /* Saturday */
    assert(calendar_math_weekday(2026, 8, 5) == 3);  /* Wednesday */
    assert(calendar_math_weekday(2026, 1, 1) == 4);  /* Thursday */
    assert(calendar_math_weekday(2024, 2, 29) == 4); /* Thursday */
    assert(calendar_math_weekday(2026, 2, 28) == 6); /* Saturday */
    assert(calendar_math_weekday(2026, 3, 1) == 0);  /* Sunday */
    assert(calendar_math_weekday(2026, 2, 30) == -1);
    assert(calendar_math_weekday(2026, 13, 1) == -1);
    assert(calendar_math_weekday(2026, 0, 1) == -1);
}

static void test_day_of_year(void)
{
    assert(calendar_math_day_of_year(2026, 1, 1) == 1);
    assert(calendar_math_day_of_year(2026, 8, 5) == 217);
    assert(calendar_math_day_of_year(2026, 12, 31) == 365);
    assert(calendar_math_day_of_year(2024, 12, 31) == 366);
    assert(calendar_math_day_of_year(2024, 3, 1) == 61);
    assert(calendar_math_day_of_year(2026, 3, 1) == 60);
    assert(calendar_math_day_of_year(2026, 2, 29) == 0);
    assert(calendar_math_day_of_year(2026, 13, 1) == 0);
}

static void test_week_of_year(void)
{
    assert(calendar_math_week_of_year(2026, 1, 1) == 1);
    assert(calendar_math_week_of_year(2026, 8, 5) == 32);
    assert(calendar_math_week_of_year(2020, 12, 31) == 53);
    assert(calendar_math_week_of_year(2021, 1, 1) == 53);
    assert(calendar_math_week_of_year(2019, 12, 30) == 1);
    assert(calendar_math_week_of_year(2024, 12, 30) == 1);
    assert(calendar_math_week_of_year(2026, 12, 31) == 53);
    assert(calendar_math_week_of_year(2000, 1, 1) == 52);
    assert(calendar_math_week_of_year(2023, 1, 1) == 52);
    assert(calendar_math_week_of_year(2026, 2, 30) == 0);
}

static void test_build_month_full_month(void)
{
    int cells[CALENDAR_MATH_MAX_CELLS];
    /* August 2026 starts on a Saturday and has 31 days: the worst case of six
     * rows with six leading blanks. */
    const int total = calendar_math_build_month(2026, 8, cells);
    assert(total == 42);
    assert(cells[0] == 0 && cells[5] == 0);
    assert(cells[6] == 1);
    assert(cells[36] == 31);
    for (int index = 37; index < 42; index++)
    {
        assert(cells[index] == 0);
    }
    for (int day = 1; day <= 31; day++)
    {
        assert(cells[5 + day] == day);
    }
}

static void test_build_month_short_and_leap(void)
{
    int cells[CALENDAR_MATH_MAX_CELLS];
    /* February 2026 starts on Sunday with 28 days: exactly four rows. */
    assert(calendar_math_build_month(2026, 2, cells) == 28);
    assert(cells[0] == 1);
    assert(cells[27] == 28);
    /* February 2024 is a leap February starting on Thursday. */
    assert(calendar_math_build_month(2024, 2, cells) == 35);
    assert(cells[0] == 0 && cells[3] == 0);
    assert(cells[4] == 1);
    assert(cells[32] == 29);
    assert(cells[33] == 0 && cells[34] == 0);
    /* A 30-day month in a five-row arrangement. */
    assert(calendar_math_build_month(2026, 11, cells) == 35);
    assert(cells[0] == 1);
    assert(cells[29] == 30);
    assert(cells[30] == 0 && cells[34] == 0);
    /* Invalid months build nothing. */
    assert(calendar_math_build_month(2026, 0, cells) == 0);
    assert(calendar_math_build_month(2026, 13, cells) == 0);
    assert(calendar_math_build_month(2026, 8, NULL) == 0);
}

/* Every month must produce whole weeks whose declared days are contiguous and
 * bounded, and the leading blanks must equal the weekday of the first. */
static void test_build_month_invariants(void)
{
    int cells[CALENDAR_MATH_MAX_CELLS];
    for (int year = 2024; year <= 2032; year++)
    {
        for (int month = 1; month <= 12; month++)
        {
            const int days = calendar_math_days_in_month(year, month);
            const int total = calendar_math_build_month(year, month, cells);
            assert(days > 0);
            assert(total % CALENDAR_MATH_COLUMNS == 0);
            assert(total >= CALENDAR_MATH_COLUMNS);
            assert(total <= CALENDAR_MATH_MAX_CELLS);
            int seen = 0;
            int first_index = -1;
            int last_index = -1;
            for (int index = 0; index < total; index++)
            {
                const int day = cells[index];
                if (day == 0)
                {
                    continue;
                }
                if (first_index < 0)
                {
                    first_index = index;
                }
                last_index = index;
                seen++;
                assert(day == seen);
            }
            assert(seen == days);
            assert(first_index == calendar_math_weekday(year, month, 1));
            assert(last_index - first_index + 1 == days);
            /* Only the leading and trailing positions stay blank. */
            for (int index = last_index + 1; index < total; index++)
            {
                assert(cells[index] == 0);
            }
        }
    }
}

/* The weekday must advance by exactly one day per calendar day. */
static void test_weekday_advances(void)
{
    int weekday = calendar_math_weekday(2026, 1, 1);
    for (int month = 1; month <= 12; month++)
    {
        const int days = calendar_math_days_in_month(2026, month);
        for (int day = 1; day <= days; day++)
        {
            assert(calendar_math_weekday(2026, month, day) == weekday);
            weekday = (weekday + 1) % CALENDAR_MATH_COLUMNS;
        }
    }
    /* 2026 has 365 days, so the next year starts one weekday later. */
    assert(weekday == calendar_math_weekday(2027, 1, 1));
}

int main(void)
{
    test_leap_years();
    test_days_in_month();
    test_weekday();
    test_day_of_year();
    test_week_of_year();
    test_build_month_full_month();
    test_build_month_short_and_leap();
    test_build_month_invariants();
    test_weekday_advances();
    (void)printf("calendar_math_test: ok\n");
    return 0;
}