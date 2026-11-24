// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define time_from_local device_local
#include "../calendar.c"
#include "../ics.c"
#undef time_from_local
#include "standin.h"
int32_t time_from_local(const host_local_time_t *, int64_t *);
static int conversions;
/* Independent Berlin 2026 transition fixture; the SDK normalizes date parts. */
int32_t device_local(const host_local_time_t *parts, int64_t *at)
{
    int64_t local;
    int32_t result = time_from_local(parts, &local);
    if (result != HOST_OK) { return result; }
    int64_t spring = (int64_t)app_days_from_date(2026, 3, 29) * 86400 + 3 * 3600;
    int64_t autumn = (int64_t)app_days_from_date(2026, 10, 25) * 86400 + 3 * 3600;
    *at = local - (local >= spring && local < autumn ? 7200 : 3600);
    conversions++;
    return HOST_OK;
}
static int64_t utc(int month, int day, int hour, int minute)
{
    return (int64_t)app_days_from_date(2026, month, day) * 86400 + hour * 3600 + minute * 60;
}
static void feed(int month, int day, const char *body)
{
    standin_reset(); standin()->zone_offset = 0;
    standin_clock(utc(month, day, 0, 0));
    standin_set("feed", "https://calendar.invalid/feed");
    app_on_load(); conversions = 0;
    app_on_data(0, (const uint8_t *)body, (int32_t)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(state.status == 200 && conversions > 0);
}
int main(void)
{
    feed(3, 28, "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\nUID:daily\r\nDTSTART:20260328T090000\r\nDTEND:20260328T100000\r\nRRULE:FREQ=DAILY;COUNT=3\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:day\r\nDTSTART;VALUE=DATE:20260329\r\nRRULE:FREQ=DAILY;COUNT=2\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:gap\r\nDTSTART:20260329T023000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    assert(stored.count == 6);
    assert(stored.events[0].start == utc(3, 28, 8, 0));
    assert(stored.events[1].start == utc(3, 28, 23, 0));
    assert(stored.events[1].end == utc(3, 29, 22, 0));
    assert(stored.events[2].start == utc(3, 29, 1, 30));
    assert(stored.events[3].start == utc(3, 29, 7, 0));
    assert(stored.events[4].start == utc(3, 29, 22, 0));
    assert(stored.events[4].end == utc(3, 30, 22, 0));
    assert(stored.events[5].start == utc(3, 30, 7, 0));
    feed(10, 24, "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\nUID:daily\r\nDTSTART:20261024T090000\r\nRRULE:FREQ=DAILY;COUNT=3\r\nEXDATE:20261025T090000\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:day\r\nDTSTART;VALUE=DATE:20261025\r\nDTEND;VALUE=DATE:20261026\r\nRRULE:FREQ=DAILY;COUNT=2\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:fold\r\nDTSTART:20261025T023000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    assert(stored.count == 5);
    assert(stored.events[0].start == utc(10, 24, 7, 0));
    assert(stored.events[1].start == utc(10, 24, 22, 0));
    assert(stored.events[1].end == utc(10, 25, 23, 0));
    assert(stored.events[2].start == utc(10, 25, 0, 30));
    assert(stored.events[3].start == utc(10, 25, 23, 0));
    assert(stored.events[3].end == utc(10, 26, 23, 0));
    assert(stored.events[4].start == utc(10, 26, 8, 0));
    /* A DATE cutoff includes the entire local day, however long it is. */
    feed(3, 27, "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:until\r\n"
        "DTSTART:20260328T003000\r\nRRULE:FREQ=DAILY;UNTIL=20260329\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n");
    assert(stored.count == 2 && stored.events[1].start == utc(3, 28, 23, 30));
    feed(10, 24, "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:until\r\n"
        "DTSTART:20261024T233000\r\nRRULE:FREQ=DAILY;UNTIL=20261025\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n");
    assert(stored.count == 2 && stored.events[1].start == utc(10, 25, 22, 30));
    /* The OS cannot convert an old DTSTART, but its future occurrences and
     * their duration must survive; a distant DATE cutoff must not erase them. */
    feed(3, 28, "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:old\r\n"
        "DTSTART:20250101T090000\r\nDTEND:20250101T100000\r\n"
        "RRULE:FREQ=DAILY;UNTIL=99991231\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    assert(stored.count > 2);
    assert(stored.events[0].start == utc(3, 28, 8, 0));
    assert(stored.events[1].start == utc(3, 29, 7, 0));
    for (int i = 0; i < stored.count; i++) {
        assert(stored.events[i].end - stored.events[i].start == 3600);
    }
    puts("calendar: per-date device conversion across both DST changes, gap, fold, recurrence, EXDATE and whole-day ends passed");
}
