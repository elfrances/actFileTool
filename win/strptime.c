// Minimalistic version of strptime() which understands only the
// "%Y-%m-%dT%H:%M:%S" format specifier used by gpxFileTool: e.g.
//    2022-04-04T02:32:02Z
// Assumes "tm" has been initialized by the caller.

#include <ctype.h>
#include <string.h>

char *strptime(const char *s, const char *format, struct tm *tm)
{
    int year, mon, mday;
    int hour, min, sec;
    int c;
    char *p;

    // Skip any leading text...
    while (!isdigit((c = *s)))
        s++;

    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d",
               &year, &mon, &mday,
               &hour, &min, &sec) != 6) {
        return NULL;
    }

    //printf("%s: year=%d month=%d date=%d hour=%d min=%d sec=%d\n", __func__, year, mon, mday, hour, min, sec);

    tm->tm_year = year - 1900;  // years since 1900
    tm->tm_mon = mon - 1;       // 0-11
    tm->tm_mday = mday;         // 1-31
    tm->tm_hour = hour;         // 0-23
    tm->tm_min = min;           // 0-59
    tm->tm_sec = sec;           // 0-59

    // If the input string includes an optional millisec value: e.g.
    // 2022-04-04T02:32:02.123Z return a pointer to the '.' char,
    // otherwise return a pointer to the 'Z' char if present, or a
    // pointer to EOL otherwise.
    if ((p = strchr(s, '.')) == NULL) {
        if ((p = strchr(s, 'Z')) == NULL) {
            p = (char *) s + strlen(s);
        }
    }

    return p;
}


