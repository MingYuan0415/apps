#ifndef __CALENDAR_MATH_H__
#define __CALENDAR_MATH_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Columns in one month grid (Sunday through Saturday). */
#define CALENDAR_MATH_COLUMNS 7
/** @brief Maximum rows a month grid can require. */
#define CALENDAR_MATH_MAX_ROWS 6
/** @brief Capacity of the cell array filled by calendar_math_build_month(). */
#define CALENDAR_MATH_MAX_CELLS \
    (CALENDAR_MATH_COLUMNS * CALENDAR_MATH_MAX_ROWS)

/** @brief Sunday index used by calendar_math_weekday(). */
#define CALENDAR_MATH_SUNDAY 0
/** @brief Saturday index used by calendar_math_weekday(). */
#define CALENDAR_MATH_SATURDAY 6

/**
 * @brief Report whether a Gregorian year is a leap year.
 * @param year is the full Gregorian year.
 * @return true when @p year has 366 days.
 */
bool calendar_math_is_leap_year(int year);

/**
 * @brief Number of days in one Gregorian month.
 * @param year is the full Gregorian year.
 * @param month is the month in the range 1 through 12.
 * @return Days in the month, or 0 when @p month is out of range.
 */
int calendar_math_days_in_month(int year, int month);

/**
 * @brief Day of week for one Gregorian date.
 * @param year is the full Gregorian year.
 * @param month is the month in the range 1 through 12.
 * @param day is the day of month.
 * @return Weekday with 0 = Sunday through 6 = Saturday, or -1 when the date is
 *         not a valid calendar date.
 */
int calendar_math_weekday(int year, int month, int day);

/**
 * @brief One-based day of year for one Gregorian date.
 * @param year is the full Gregorian year.
 * @param month is the month in the range 1 through 12.
 * @param day is the day of month.
 * @return Ordinal in the range 1 through 366, or 0 for an invalid date.
 */
int calendar_math_day_of_year(int year, int month, int day);

/**
 * @brief ISO-8601 week number for one Gregorian date.
 *
 * Weeks start on Monday and week 1 is the week containing the first Thursday
 * of the year, so early January can belong to the previous year's last week.
 *
 * @param year is the full Gregorian year.
 * @param month is the month in the range 1 through 12.
 * @param day is the day of month.
 * @return Week number in the range 1 through 53, or 0 for an invalid date.
 */
int calendar_math_week_of_year(int year, int month, int day);

/**
 * @brief Build the cell sequence of one month grid in reading order.
 *
 * Leading positions before the first of the month and trailing positions after
 * the last of the month are written as 0, so a renderer can skip them.
 *
 * @param year is the full Gregorian year.
 * @param month is the month in the range 1 through 12.
 * @param cells receives at least CALENDAR_MATH_MAX_CELLS integers.
 * @return Number of cells to render (a multiple of CALENDAR_MATH_COLUMNS), or
 *         0 for invalid input.
 */
int calendar_math_build_month(int year, int month, int *cells);

#ifdef __cplusplus
}
#endif

#endif /* __CALENDAR_MATH_H__ */