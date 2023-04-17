#include <stdio.h>
#include <math.h>

int leapyear(int year);
int daysInMonth(int month, int year);
int dayOfWeek(int month, int day, int year);
int nthWeekdayOf(int n, int weekday, int month, int year);
void holidize(int *day, int *month, int *year);
void tablePrint(char name[], int month, int day, int year);
void tableHolidizedPrint(char name[], int month, int day, int year);
void printHolidays(int year);

int main() {
    int year;

    while (1) {
        printf("Enter a year between 1980 and 2030: ");
        scanf("%d", &year);

        if (year >= 1980 && year <= 2030)
            break;
        else
            printf("The year must be between 1980 and 2030.\n");
    }
    printHolidays(year);
}

void printHolidays(int year) {
    int tmpyear, tmpmonth, tmpday, i;

    tmpyear = year;
    tmpmonth = 1;
    tmpday = 1;
    holidize(&tmpday, &tmpmonth, &tmpyear);
    if (tmpyear == year)
        tablePrint("New Year's Day", tmpmonth, tmpday, tmpyear);

    tableHolidizedPrint("Birthday of Martin Luther King, Jr.",
        1, nthWeekdayOf(3, 1, 1, year), year);
    tableHolidizedPrint("Washington's Birthday",
        2, nthWeekdayOf(3, 1, 2, year), year);

    // memorial day - last monday in may
    tmpday = -1;
    for (i = 5; i > 2 && tmpday == -1; i--)
        tmpday = nthWeekdayOf(i, 1, 5, year);
    tableHolidizedPrint("Memorial Day", 5, tmpday, year);

    tableHolidizedPrint("Independence Day", 7, 4, year);
    tableHolidizedPrint("Labor Day",
        9, nthWeekdayOf(1, 1, 9, year), year);
    tableHolidizedPrint("Columbus Day",
        10, nthWeekdayOf(2, 1, 10, year), year);
    tableHolidizedPrint("Veteran's Day", 11, 11, year);
    tableHolidizedPrint("Thanksgiving Day",
        11, nthWeekdayOf(4, 4, 11, year), year);
    tableHolidizedPrint("Christmas Day", 12, 25, year);

    tmpday = 1;
    tmpmonth = 1;
    tmpyear = year + 1;
    holidize(&tmpday, &tmpmonth, &tmpyear);
    if (tmpyear == year)
        tablePrint("New Year's Day", tmpmonth, tmpday, tmpyear);
}

// Whether year is a leap year.
int leapyear(int year) {
    return year % 400 == 0 || (year % 100 != 0 && year % 4 == 0);
}

// The number of days for the given month in the given year.
int daysInMonth(int month, int year) {
    if (month == 2)
        return leapyear(year) ? 29 : 28;
    else if (month == 4 || month == 6 || month == 9 || month == 11)
        return 30;
    else
        return 31;
}

// The day of week for the given date, from 0 (Sunday) to 6 (Saturday).
// NB: stupid argument order.
int dayOfWeek(int month, int day, int year) {
    int f;

    month -= 2;
    if (month <= 0) {
        month += 12;
        year--;
    }
    f = day + (13 * month - 1) / 5
            + (year % 100)
            + (year % 100) / 4
            + year / 100 / 4
            - 2 * (year / 100);
    f %= 7;
    while (f < 0)
        f += 7;
    return f;
}

// The `n`th `weekday` of `month` in `year`. Weekdays are 0 (Sunday) to 6.
// For example, nthWeekdayOf(3, 2, 10, 2009) is the third Tuesday of October
// in 2009.
int nthWeekdayOf(int n, int weekday, int month, int year) {
    int day;
    if (weekday < 0 || weekday > 6 || month < 1 || month > 12 || 
            year < 1980 || year > 2030 || n < 1 || n > 5)
        return -1;

    // day 1, plus the number of days after day 1 it is
    day = 1 + (weekday - dayOfWeek(month, 1, year));
    if (day < 1) day += 7;

    day += 7 * (n - 1); // get out to the right week
    if (day > daysInMonth(month, year))
        return -1;
    return day;
}

// Prints out an entry for the holiday name / date.
void tablePrint(char name[], int month, int day, int year) {
    printf("%40s  %2d/%02d/%04d\n", name, month, day, year);
}

// Prints out an entry for the holiday named `name` that actually falls
// on `date`.
void tableHolidizedPrint(char name[], int month, int day, int year) {
    int _day = day, _month = month, _year = year;
    holidize(&_day, &_month, &_year);
    tablePrint(name, _month, _day, _year);
}

// Modifies the passed day/month/year to be the day that the federal government
// would celebrate a holiday on that date. Assumes that the passed date is
// valid.
// Modifying args is lame, but so is making a struct just to return three
// values. Probably should've used a date struct all along.
void holidize(int *day, int *month, int *year) {
    int weekday = dayOfWeek(*month, *day, *year);
    if (weekday == 6) {
        *day = *day - 1;
        if (*day == 0) {
            *month = *month - 1;
            if (*month == 0) {
                *year = *year - 1;
                *month = 12;
            }
            *day = daysInMonth(*month, *year);
        }
    }
    if (weekday == 0) {
        *day = *day + 1;
        if (*day == 7) {
            *month = *month + 1;
            if (*month == 13) {
                *year = *year + 1;
                *month = 1;
            }
            *day = daysInMonth(*month, *year);
        }
    }
}
