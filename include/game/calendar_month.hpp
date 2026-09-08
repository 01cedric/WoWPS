#pragma once

namespace wowee {
namespace game {

/**
 * The month arithmetic the calendar grid is drawn from.
 *
 * `CalendarGetMonth(offset)` answers four values and FrameXML lays out the
 * whole month from them - which day of the week the first falls on decides
 * where the grid starts, and the day count decides where it ends and how many
 * of the next month's days are shown trailing. Every one of them goes straight
 * into arithmetic (`mod((firstWeekday - CALENDAR_FIRST_WEEKDAY - 1) + 7, 7)`),
 * so there is no value here that can be approximated: a February with 28 days
 * in a leap year silently draws the wrong grid rather than failing.
 *
 * Pure, and separate from the bindings, so the leap years and the month
 * rollover can be stated as cases rather than reasoned about.
 */

/// The proleptic Gregorian rule, which is the one the server's clock uses.
constexpr bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/// Days in a month, 1-12. Zero for a month outside that range, so a bad offset
/// draws nothing rather than a grid of garbage.
constexpr int daysInMonth(int month, int year) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
        case 4: case 6: case 9: case 11:                          return 30;
        case 2: return isLeapYear(year) ? 29 : 28;
        default: return 0;
    }
}

/**
 * The weekday of a date, 1 = Sunday through 7 = Saturday.
 *
 * That base is not a choice: `CalendarGetDate` already answers `tm_wday + 1`
 * and the interface indexes CALENDAR_WEEKDAY_NAMES with it, whose first entry
 * is Sunday. Sakamoto's method, which is exact for any Gregorian date rather
 * than only for the range a lookup table happens to cover.
 */
constexpr int weekdayOf(int month, int day, int year) {
    if (month < 1 || month > 12) return 1;
    constexpr int kShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y = (month < 3) ? year - 1 : year;
    return ((y + y / 4 - y / 100 + y / 400 + kShift[month - 1] + day) % 7) + 1;
}

/// What `CalendarGetMonth` answers.
struct CalendarMonthInfo {
    int month = 1;         ///< 1-12
    int year = 2000;
    int numDays = 31;
    int firstWeekday = 1;  ///< 1 = Sunday, of the first of that month
};

/// The month `offset` months from the given one, with December wrapping into
/// January of the next year and the sign of the offset respected - a negative
/// month index is the case a plain modulus gets wrong in C++.
constexpr CalendarMonthInfo calendarMonthAt(int baseMonth, int baseYear,
                                            int offset) {
    int total = (baseYear * 12) + (baseMonth - 1) + offset;
    int year = total / 12;
    int monthIndex = total % 12;
    // Truncation towards zero would put month -1 of year 0 where month 11 of
    // year -1 belongs. Only reachable for dates before year 0, but the wrong
    // answer here is a silently shifted grid rather than an error.
    if (monthIndex < 0) {
        monthIndex += 12;
        year -= 1;
    }
    CalendarMonthInfo info;
    info.month = monthIndex + 1;
    info.year = year;
    info.numDays = daysInMonth(info.month, info.year);
    info.firstWeekday = weekdayOf(info.month, 1, info.year);
    return info;
}

}  // namespace game
}  // namespace wowee
