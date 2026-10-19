/* Calendar-dependent scene selection, GrAc +16F4..18B0 and +366C..387C, +4624..4800.
 * Arithmetic order and the console's calendar quirks are kept. */
#include "village.h"
#include <melee/gm/gm_1601.h>
#include <melee/lb/lbtime.h>
#include <sysdolphin/baselib/random.h>

void ac_calendar_now(datetime* date)
{
    gm_801692E8(lbTime_GetTimeInSeconds(), date);
}

MU_NO_CONTRACT int ac_random_range(int low, int high)
{
    float span = (float) (high - low);
    float product = span * HSD_Randf();
    return (int) ((float) low + product);
}

int ac_determine_season(void)
{
    unsigned month = ac_date.month;
    if ((u8) (month - 3) > 8) { return 3; }
    if ((u8) (month - 3) <= 2) { return 2; }
    return (u8) (month - 6) > 2;
}

int ac_determine_time(void)
{
    unsigned hour = ac_date.hour;
    if ((u8) (hour - 4) > 14) { return 2; }
    if (hour <= 6) { return 3; }
    return hour > 15;
}

/* +4624: month/year are used directly. This deliberately does not replace the console formula
 * with a different astronomical algorithm or shift January and February to the previous year. */
MU_NO_CONTRACT static float ac_moon_phase(int year, int month, int day)
{
    float century = (float) (year / 100);
    float value = 2.0f - century;
    float quarter = century * 0.25f;
    float years, months;
    value = value + quarter;
    value = value + (float) day;
    years = (float) (int) (((double) year + (double) 4716.0f) * (double) 365.25f);
    value = value + years;
    months = (float) (int) (((double) month + (double) 1.0f) * (double) 30.6001f);
    value = value + months;
    value = value - 1524.5f;
    value = value - 2451549.5f;
    value = value / 29.53f;
    value = value - (float) (int) value;
    return value * 29.53f;
}

static int ac_round(float value)
{
    return (int) (value < 0.0f ? (double) value - 0.5 : (double) value + 0.5);
}

/* +366C: a full moon within one day; the September/October windows and the September 22
 * reference test are the original branching, including its asymmetric September window. */
static bool ac_harvest_moon(int year, int month, int day)
{
    bool full = ac_round(ac_moon_phase(year, month, day)) == 15;
    float reference = ac_moon_phase(year, 9, 22);
    if (!full) { full = ac_round(ac_moon_phase(year, month, day + 1)) == 15; }
    if (!full) { full = ac_round(ac_moon_phase(year, month, day - 1)) == 15; }
    if (reference > 15.0f) { return month == 9 && day <= 21 && full; }
    if (month == 9) { return day > 21 && full; }
    return month == 10 && day <= 20 && full;
}

/* +37F4: the console adds year/100 and subtracts year/400. Keep those signs. */
static int ac_day_of_week(int day, int month, int year)
{
    static const int offsets[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    year -= month < 3;
    return (year + year / 4 + year / 100 - year / 400 + offsets[month - 1] + day) % 7;
}

int ac_determine_event(void)
{
    unsigned date = (unsigned) ac_date.month * 256 + ac_date.day;
    if ((date == 0xC1F && ac_date.hour == 23) ||
        (date == 0x101 && ac_date.hour == 0)) { return 3; }
    if (ac_harvest_moon((s16) ac_date.year, ac_date.month, ac_date.day)) { return 4; }
    if (ac_day_of_week(ac_date.day, ac_date.month, (s16) ac_date.year) == 6) {
        if (ac_time == 3) { return 0; }
        return (u8) (ac_date.hour - 20) < 4 ? 2 : 1;
    }
    return 0;
}
