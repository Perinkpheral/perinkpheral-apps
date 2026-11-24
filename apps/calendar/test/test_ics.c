/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The calendar's reader, tried on a computer:
 * The catalog CI links the SDK's pure date and text helpers and runs it
 * with address and undefined-behaviour sanitizers.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../ics.c"

static int checks, failures;
#define CHECK(condition)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(condition)) {                                                   \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        }                                                                     \
    } while (0)

static int64_t utc(int year, int month, int day, int hour, int minute)
{
    struct tm parts = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day,
                        .tm_hour = hour, .tm_min = minute };
    return (int64_t)timegm(&parts);
}

static ics_t       ics;
static ics_event_t events[ICS_EVENTS_MAX];

#define MSK (3 * 3600)

static int32_t device_offset;
static int64_t fixed_local(int32_t days, int32_t seconds)
{
    return (int64_t)days * 86400 + seconds - device_offset;
}

/* The file in pieces of `piece` bytes, as the network would give it. */
static int32_t read_file(const char *text, int64_t from, int days, int32_t offset, int piece)
{
    device_offset = offset;
    ics_begin(&ics, events, from, from + (int64_t)days * 86400, fixed_local);
    int32_t length = (int32_t)strlen(text);
    for (int32_t at = 0; at < length; at += piece) {
        ics_feed(&ics, text + at, length - at < piece ? length - at : piece);
    }
    return ics_end(&ics);
}

#define ZONES                                                                              \
    "BEGIN:VTIMEZONE\r\nTZID:Europe/Berlin\r\n"                                            \
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0200\r\nDTSTART:19700329T020000\r\n" \
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\nEND:DAYLIGHT\r\n"                           \
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\nDTSTART:19701025T030000\r\n" \
    "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"         \
    "BEGIN:VTIMEZONE\r\nTZID:Europe/Moscow\r\n"                                            \
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+0300\r\nTZOFFSETTO:+0300\r\nDTSTART:19700101T000000\r\n" \
    "END:STANDARD\r\nEND:VTIMEZONE\r\n"

static void test_single_events(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" ZONES
        "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261113T120000Z\r\nDTEND:20261113T130000Z\r\n"
        "SUMMARY:Lunch\\, with a comma\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;TZID=Europe/Moscow:20261114T100000\r\n"
        "DTEND;TZID=Europe/Moscow:20261114T113000\r\nSUMMARY:A very long title that is fol\r\n"
        " ded over two lines\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Not the title\r\n"
        "TRIGGER:-PT10M\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:c\r\nDTSTART;VALUE=DATE:20261115\r\nDTEND;VALUE=DATE:20261116\r\n"
        "SUMMARY:\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C \xD1\x80\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20251113T120000Z\r\nSUMMARY:Last year\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:e\r\nDTSTART;TZID=Europe/Berlin:20261116T090000\r\n"
        "DURATION:PT45M\r\nSUMMARY:Berlin in winter\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:f\r\nDTSTART:20261117T080000\r\nSUMMARY:No zone at all\r\n"
        "STATUS:CONFIRMED\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:g\r\nDTSTART:20261117T090000Z\r\nSUMMARY:Called off\r\n"
        "STATUS:CANCELLED\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    int64_t now = utc(2026, 11, 13, 9, 0);
    for (int piece = 1; piece <= 97; piece += 32) {
        int32_t count = read_file(FILE_TEXT, now, 14, MSK, piece);
        CHECK(count == 5);
        CHECK(events[0].start == utc(2026, 11, 13, 12, 0) && events[0].end == utc(2026, 11, 13, 13, 0));
        CHECK(strcmp(events[0].title, "Lunch, with a comma") == 0);
        CHECK(events[1].start == utc(2026, 11, 14, 7, 0) && events[1].end == utc(2026, 11, 14, 8, 30));
        CHECK(strcmp(events[1].title, "A very long title that is folded over two lines") == 0);
        CHECK(events[2].is_all_day && events[2].start == utc(2026, 11, 14, 21, 0)); /* midnight in Moscow */
        CHECK(events[2].end - events[2].start == 86400);
        CHECK(strcmp(events[2].title, "\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C \xD1\x80\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F") == 0);
        CHECK(events[3].start == utc(2026, 11, 16, 8, 0) && events[3].end == utc(2026, 11, 16, 8, 45));
        CHECK(events[4].start == utc(2026, 11, 17, 5, 0)); /* 08:00 on the device */
    }
}

static void test_summer_time(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\n" ZONES
        "BEGIN:VEVENT\r\nUID:a\r\nDTSTART;TZID=Europe/Berlin:20260601T090000\r\nSUMMARY:S\r\n"
        "RRULE:FREQ=MONTHLY\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    /* The 1st of every month at nine in Berlin: seven in UTC in summer,
     * eight in winter, the last Sunday of October between them. */
    int32_t count = read_file(FILE_TEXT, utc(2026, 9, 15, 0, 0), 120, 0, 50);
    CHECK(count == 4);
    CHECK(events[0].start == utc(2026, 10, 1, 7, 0));
    CHECK(events[1].start == utc(2026, 11, 1, 8, 0));
    CHECK(events[2].start == utc(2026, 12, 1, 8, 0));
    CHECK(events[3].start == utc(2027, 1, 1, 8, 0));
}

static void test_repeating(void)
{
    static const char FILE_TEXT[] =
        "BEGIN:VCALENDAR\r\n" ZONES
        /* every working day since 2019, with one day taken out and one moved */
        "BEGIN:VEVENT\r\nUID:standup\r\nDTSTART;TZID=Europe/Moscow:20190107T103000\r\n"
        "DTEND;TZID=Europe/Moscow:20190107T104500\r\nSUMMARY:Standup\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR\r\n"
        "EXDATE;TZID=Europe/Moscow:20261117T103000,20200101T103000\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:standup\r\nRECURRENCE-ID;TZID=Europe/Moscow:20261118T103000\r\n"
        "DTSTART;TZID=Europe/Moscow:20261118T150000\r\nDTEND;TZID=Europe/Moscow:20261118T151500\r\n"
        "SUMMARY:Standup, moved\r\nEND:VEVENT\r\n"
        /* every second week, three times in all */
        "BEGIN:VEVENT\r\nUID:review\r\nDTSTART:20261110T140000Z\r\nSUMMARY:Review\r\n"
        "RRULE:FREQ=WEEKLY;INTERVAL=2;COUNT=3\r\nEND:VEVENT\r\n"
        /* every day until the 15th */
        "BEGIN:VEVENT\r\nUID:pills\r\nDTSTART:20261101T060000Z\r\nSUMMARY:Pills\r\n"
        "RRULE:FREQ=DAILY;UNTIL=20261115T235959Z\r\nEND:VEVENT\r\n"
        /* the second Tuesday of a month */
        "BEGIN:VEVENT\r\nUID:board\r\nDTSTART:20260113T160000Z\r\nSUMMARY:Board\r\n"
        "RRULE:FREQ=MONTHLY;BYDAY=2TU\r\nEND:VEVENT\r\n"
        /* a birthday */
        "BEGIN:VEVENT\r\nUID:birthday\r\nDTSTART;VALUE=DATE:19900120\r\nSUMMARY:Birthday\r\n"
        "RRULE:FREQ=YEARLY\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    int64_t now = utc(2026, 11, 13, 8, 0); /* a Friday, 11:00 in Moscow */
    int32_t count = read_file(FILE_TEXT, now, 7, MSK, 64);
    /* Fri 13: standup 10:30 is over. Pills 14, 15. Mon 16 standup; Tue 17 taken out;
     * Wed 18 moved to 15:00; Thu 19, Fri 20 standup (20th 07:30 UTC is inside the week). */
    CHECK(count == 6);
    CHECK(strcmp(events[0].title, "Pills") == 0 && events[0].start == utc(2026, 11, 14, 6, 0));
    CHECK(strcmp(events[1].title, "Pills") == 0 && events[1].start == utc(2026, 11, 15, 6, 0));
    CHECK(strcmp(events[2].title, "Standup") == 0 && events[2].start == utc(2026, 11, 16, 7, 30));
    CHECK(strcmp(events[3].title, "Standup, moved") == 0 && events[3].start == utc(2026, 11, 18, 12, 0));
    CHECK(strcmp(events[4].title, "Standup") == 0 && events[4].start == utc(2026, 11, 19, 7, 30));
    CHECK(strcmp(events[5].title, "Standup") == 0 && events[5].start == utc(2026, 11, 20, 7, 30));

    /* Sixty days: the review's second and third time and no fourth, the board, no birthday yet. */
    count = read_file(FILE_TEXT, utc(2026, 11, 23, 0, 0), 60, MSK, 64);
    int reviews = 0, boards = 0, birthdays = 0;
    for (int32_t i = 0; i < count; i++) {
        reviews += strcmp(events[i].title, "Review") == 0;
        boards += strcmp(events[i].title, "Board") == 0;
        birthdays += strcmp(events[i].title, "Birthday") == 0;
    }
    CHECK(count == ICS_EVENTS_MAX); /* the standups fill it: the earliest are kept */
    CHECK(reviews == 2);
    CHECK(boards == 1 && birthdays == 0);

    count = read_file(FILE_TEXT, utc(2027, 1, 19, 0, 0), 2, MSK, 64);
    birthdays = 0;
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(events[i].title, "Birthday") == 0) {
            birthdays++;
            CHECK(events[i].is_all_day && events[i].start == utc(2027, 1, 19, 21, 0));
        }
    }
    CHECK(birthdays == 1);
}

static void test_monthly_weekdays(void)
{
    static const char text[] = "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:weekly\n"
        "DTSTART:20261102T090000Z\nRRULE:FREQ=MONTHLY;BYDAY=MO\nEND:VEVENT\nEND:VCALENDAR\n";
    CHECK(read_file(text, utc(2026, 11, 20, 0, 0), 14, 0, 1) == 2);
    CHECK(events[0].start == utc(2026, 11, 23, 9, 0));
    CHECK(events[1].start == utc(2026, 11, 30, 9, 0));
    static const char counted[] = "BEGIN:VEVENT\nUID:counted\nDTSTART:20261102T090000Z\n"
        "RRULE:FREQ=MONTHLY;BYDAY=MO,WE;COUNT=4;INTERVAL=2\nEND:VEVENT\n";
    CHECK(read_file(counted, utc(2026, 11, 1, 0, 0), 100, 0, 7) == 4);
    CHECK(events[3].start == utc(2026, 11, 11, 9, 0));
    static const char interval[] = "BEGIN:VEVENT\nDTSTART:20261102T090000Z\n"
        "RRULE:FREQ=MONTHLY;BYDAY=MO;INTERVAL=2;UNTIL=20270112T000000Z\nEND:VEVENT\n";
    CHECK(read_file(interval, utc(2026, 12, 1, 0, 0), 60, 0, 3) == 2);
    CHECK(events[0].start == utc(2027, 1, 4, 9, 0) && events[1].start == utc(2027, 1, 11, 9, 0));
}

/* Unsupported positions must not expand into every matching weekday. */
static void test_monthly_set_position(void)
{
    static const char second[] = "BEGIN:VEVENT\nDTSTART:20261110T090000Z\n"
        "RRULE:FREQ=MONTHLY;BYDAY=TU;BYSETPOS=2\nEND:VEVENT\n";
    static const char first[] = "BEGIN:VEVENT\nDTSTART:20261102T090000Z\n"
        "RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1\nEND:VEVENT\n";
    static const char last[] = "BEGIN:VEVENT\nDTSTART:20261130T090000Z\n"
        "RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\nEND:VEVENT\n";
    CHECK(read_file(second, utc(2026, 11, 10, 0, 0), 28, 0, 1) == 1);
    CHECK(events[0].start == utc(2026, 11, 10, 9, 0));
    CHECK(read_file(first, utc(2026, 11, 1, 0, 0), 14, 0, 3) == 1);
    CHECK(events[0].start == utc(2026, 11, 2, 9, 0));
    CHECK(read_file(last, utc(2026, 11, 1, 0, 0), 31, 0, 7) == 1);
    CHECK(events[0].start == utc(2026, 11, 30, 9, 0));
}

static void test_exception_spares(void)
{
    char text[16000];
    int length = snprintf(text, sizeof text, "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:daily\n"
        "DTSTART:20261101T090000Z\nRRULE:FREQ=DAILY\nEND:VEVENT\n");
    /* The full override budget removes the first 32 of 70 candidates. */
    int64_t first = utc(2026, 11, 1, 9, 0);
    CHECK(length > 0 && length < (int)sizeof text);
    for (int i = 0; i < ICS_OVERRIDES_MAX; i++) {
        int32_t year, month, day;
        app_date_from_days((int32_t)(first / 86400) + i, &year, &month, &day);
        int added = snprintf(text + length, sizeof text - length,
            "BEGIN:VEVENT\nUID:daily\nDTSTART:%04d%02d%02dT090000Z\n"
            "RECURRENCE-ID:%04d%02d%02dT090000Z\nSTATUS:CANCELLED\nEND:VEVENT\n",
            year, month, day, year, month, day);
        CHECK(added > 0 && added < (int)sizeof text - length);
        length += added;
    }
    int added = snprintf(text + length, sizeof text - length, "END:VCALENDAR\n");
    CHECK(added > 0 && added < (int)sizeof text - length);
    CHECK(read_file(text, first - 1, 70, 0, 1) == ICS_EVENTS_MAX);
    CHECK(events[0].start == first + ICS_OVERRIDES_MAX * 86400LL);
    CHECK(events[23].start == first + 55 * 86400LL);
    /* An override before its master must remove the same occurrence. */
    static const char before[] = "BEGIN:VEVENT\nUID:a\nDTSTART:20261102T090000Z\n"
        "RECURRENCE-ID:20261101T090000Z\nEND:VEVENT\n"
        "BEGIN:VEVENT\nUID:a\nDTSTART:20261101T090000Z\nRRULE:FREQ=DAILY\nEND:VEVENT\n";
    CHECK(read_file(before, first - 1, 40, 0, 7) == ICS_EVENTS_MAX);
    CHECK(events[0].start == first + 86400);
}

static void test_transition_hours(void)
{
    static const char spring[] = "BEGIN:VCALENDAR\n" ZONES
        "BEGIN:VEVENT\nDTSTART;TZID=Europe/Berlin:20260329T013000\nEND:VEVENT\n"
        "BEGIN:VEVENT\nDTSTART;TZID=Europe/Berlin:20260329T033000\nEND:VEVENT\nEND:VCALENDAR\n";
    CHECK(read_file(spring, utc(2026, 3, 29, 0, 0), 1, 0, 1) == 2);
    CHECK(events[0].start == utc(2026, 3, 29, 0, 30));
    CHECK(events[1].start == utc(2026, 3, 29, 1, 30));
    static const char autumn[] = "BEGIN:VCALENDAR\n" ZONES
        "BEGIN:VEVENT\nDTSTART;TZID=Europe/Berlin:20261025T023000\nEND:VEVENT\n"
        "BEGIN:VEVENT\nDTSTART;TZID=Europe/Berlin:20261025T033000\nEND:VEVENT\nEND:VCALENDAR\n";
    CHECK(read_file(autumn, utc(2026, 10, 25, 0, 0), 1, 0, 7) == 2);
    CHECK(events[0].start == utc(2026, 10, 25, 0, 30));
    CHECK(events[1].start == utc(2026, 10, 25, 2, 30));
}

static void test_rejects_non_calendar(void)
{
    CHECK(read_file("", 0, 14, 0, 8) == 0);
    CHECK(read_file("<html><body>Sign in</body></html>", 0, 14, 0, 8) == 0);
    CHECK(read_file("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:No start\r\nEND:VEVENT\r\n", 0, 14, 0, 8) == 0);
    CHECK(read_file("BEGIN:VEVENT\nDTSTART:2026\nEND:VEVENT\nBEGIN:VEVENT\nDTSTART:20261113T25\n"
                    "RRULE:FREQ=DAILY;INTERVAL=0;BYDAY=,,XX,9\nEND:VEVENT", utc(2026, 11, 13, 0, 0), 14, 0, 3) == 0);
    /* a line far longer than is kept, and an event that never ends */
    static char huge[5000];
    int at = snprintf(huge, sizeof huge, "BEGIN:VEVENT\nDTSTART:20261114T100000Z\nDESCRIPTION:");
    CHECK(at > 0 && at + 4000 < (int)sizeof huge);
    memset(huge + at, 'x', 4000);
    int added = snprintf(huge + at + 4000, sizeof huge - at - 4000, "\nSUMMARY:After a long line\nEND:VEVENT\n");
    CHECK(added > 0 && added < (int)sizeof huge - at - 4000);
    CHECK(read_file(huge, utc(2026, 11, 13, 0, 0), 14, 0, 100) == 1);
    CHECK(strcmp(events[0].title, "After a long line") == 0);
}

static void test_opening_at_every_split(void)
{
    static const char *const VALID[] = {
        "BEGIN:VCALENDAR\r\n", "\xEF\xBB\xBF" "BEGIN:VCALENDAR\r\n",
    };
    for (unsigned i = 0; i < sizeof VALID / sizeof VALID[0]; i++) {
        int32_t length = (int32_t)strlen(VALID[i]);
        for (int32_t split = 0; split <= length; split++) {
            ics_opening_t opening = { 0 };
            CHECK(ics_opening_feed(&opening, (const uint8_t *)VALID[i], split) >= 0);
            CHECK(ics_opening_feed(&opening, (const uint8_t *)VALID[i] + split, length - split) == 1);
        }
        ics_opening_t opening = { 0 };
        for (int32_t at = 0; at < length; at++) {
            CHECK(ics_opening_feed(&opening, (const uint8_t *)VALID[i] + at, 1) >= 0);
        }
        CHECK(ics_opening_feed(&opening, NULL, 0) == 1);
    }
    ics_opening_t opening = { 0 };
    CHECK(ics_opening_feed(&opening, (const uint8_t *)"BEGIN:VCAL", 10) == 0);
    CHECK(ics_opening_feed(&opening, (const uint8_t *)"X", 1) == -1);
    opening = (ics_opening_t){ 0 };
    CHECK(ics_opening_feed(&opening, (const uint8_t *)"\xEF\xBB", 2) == 0);
    CHECK(ics_opening_feed(&opening, (const uint8_t *)"X", 1) == -1);
}

static void test_calendar_names(void)
{
    static const char text[] = "BEGIN:VCALENDAR\r\nX-WR-CALNAME;LANGUAGE=ru:Семья\\, дом\r\n"
        " и друзья\r\nBEGIN:VEVENT\r\nX-WR-CALNAME:Ignore this\r\n"
        "DTSTART:20261118T120000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    CHECK(read_file(text, utc(2026, 11, 18, 0, 0), 1, 0, 1) == 1);
    CHECK(strcmp(ics.name, "Семья, доми друзья") == 0);
    char long_name[512];
    const char *prefix = "BEGIN:VCALENDAR\nX-WR-CALNAME:";
    size_t at = strlen(prefix);
    memcpy(long_name, prefix, at);
    for (int i = 0; i < 100; i++) {
        memcpy(long_name + at, "Я", 2); at += 2;
    }
    strcpy(long_name + at, "\nEND:VCALENDAR\n");
    CHECK(read_file(long_name, 0, 1, 0, 1) == 0);
    CHECK(strlen(ics.name) == 126); /* Never half of a UTF-8 character. */
    CHECK((unsigned char)ics.name[125] == 0xaf);
    CHECK(read_file("BEGIN:VCALENDAR\nEND:VCALENDAR\n", 0, 1, 0, 1) == 0);
    CHECK(ics.name[0] == '\0');
}

/* Untrusted numeric fields must be bounded before arithmetic or narrowing. */
static void test_numeric_bounds(void)
{
    static const struct { const char *rule; int count; } rules[] = {
        { "FREQ=DAILY;COUNT=4444444444", 2 }, /* verified UBSan reproducer */
        { "FREQ=DAILY;COUNT=2147483647", 2 },
        { "FREQ=DAILY;COUNT=99999999999999999999999999999999999999", 2 },
        { "FREQ=DAILY;COUNT=-4444444444", 1 },
        { "FREQ=DAILY;COUNT=0", 1 },
        { "FREQ=DAILY;INTERVAL=0", 1 },
        { "FREQ=DAILY;COUNT=1", 1 },
        { "FREQ=DAILY;COUNT=2", 2 },
        { "FREQ=DAILY;INTERVAL=2147483647", 1 },
        { "FREQ=WEEKLY;INTERVAL=4444444444", 1 },
        { "FREQ=MONTHLY;INTERVAL=4444444444", 1 },
        { "FREQ=YEARLY;INTERVAL=4444444444", 1 },
        { "FREQ=DAILY;INTERVAL=-4444444444", 1 },
        { "FREQ=MONTHLY;BYMONTHDAY=4444444444", 1 },
        { "FREQ=MONTHLY;BYDAY=-4444444444WE", 1 },
    };
    char text[512];
    for (unsigned i = 0; i < sizeof rules / sizeof rules[0]; i++) {
        snprintf(text, sizeof text, "BEGIN:VCALENDAR\nBEGIN:VEVENT\n"
                 "DTSTART:20261118T120000Z\nRRULE:%s\nEND:VEVENT\nEND:VCALENDAR\n", rules[i].rule);
        CHECK(read_file(text, utc(2026, 11, 18, 0, 0), 2, 0, 1) == rules[i].count);
    }
    CHECK(ics.nth_weekday == 0);
    /* Leap dates and the leap second are valid; every invalid field is refused. */
    static const char *const invalid[] = {
        "", "2", "2026111", "202611180", "20261118T12", "20261118X120000",
        "20261318", "20260018", "20261100", "20261131", "20260229",
        "20261118T240000Z", "20261118T126000Z", "20261118T120061Z",
        "20261118T1200xxZ", "44444444444444444444444444444444",
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        ics_moment_t moment;
        moment_of(&moment, invalid[i], "");
        CHECK(!moment.is_set);
        snprintf(text, sizeof text, "BEGIN:VCALENDAR\nBEGIN:VTIMEZONE\nTZID:bad\n"
                 "BEGIN:STANDARD\nDTSTART:%s\nEND:STANDARD\nEND:VTIMEZONE\n"
                 "BEGIN:VEVENT\nDTSTART:%s\nEND:VEVENT\nEND:VCALENDAR\n", invalid[i], invalid[i]);
        CHECK(read_file(text, utc(2026, 11, 18, 0, 0), 2, 0, 1) == 0);
    }
    ics_moment_t moment;
    moment_of(&moment, "20240229T235960Z", "");
    CHECK(moment.is_set && moment.seconds == 86400);
    moment_of(&moment, "99991231", "");
    CHECK(moment.is_set);
    CHECK(offset_of("+235959") == 86399 && offset_of("-0530") == -19800);
    static const char *const offsets[] = { "", "+", "+03", "+2400", "-0060", "+000060", "+9999999999999999" };
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) { CHECK(offset_of(offsets[i]) == 0); }
    CHECK(duration_of("PT2147483647S") == INT32_MAX);
    CHECK(duration_of("PT0002147483647S") == INT32_MAX);
    static const char *const oversized_seconds[] = {
        "PT2147483648S", "PT4444444444S", "PT9999999999999999999999999999S",
    };
    for (unsigned i = 0; i < sizeof oversized_seconds / sizeof oversized_seconds[0]; i++) {
        CHECK(duration_of(oversized_seconds[i]) == 0);
        snprintf(text, sizeof text, "BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:20261118T120000Z\n"
                 "DURATION:%s\nEND:VEVENT\nEND:VCALENDAR\n", oversized_seconds[i]);
        CHECK(read_file(text, utc(2026, 11, 18, 0, 0), 2, 0, 1) == 1);
        CHECK(ics.duration_s == 0 && events[0].end == events[0].start);
    }
    CHECK(duration_of("P24855DT11647S") == INT32_MAX);
    CHECK(duration_of("P24855DT11648S") == 0);
    CHECK(duration_of("P4444444444W") == 0);
    CHECK(duration_of("PT9999999999999999999999999999H") == 0);
    snprintf(text, sizeof text, "BEGIN:VCALENDAR\nBEGIN:VTIMEZONE\nTZID:large\nBEGIN:DAYLIGHT\n"
             "DTSTART:20260329T020000\nTZOFFSETTO:+999999\n"
             "RRULE:FREQ=YEARLY;BYMONTH=4444444444;BYDAY=-4444444444SU\n"
             "END:DAYLIGHT\nEND:VTIMEZONE\nBEGIN:VEVENT\nDTSTART:20261118T120000Z\n"
             "DURATION:P4444444444W\nRRULE:FREQ=DAILY;COUNT=4444444444\nEND:VEVENT\nEND:VCALENDAR\n");
    CHECK(read_file(text, utc(2026, 11, 18, 0, 0), 2, 0, 7) == 2);
    CHECK(ics.rule_count == STEPS_MAX * DAYS_PER_WEEK && ics.duration_s == 0);
    CHECK(ics.zones[0].daylight.month == 3 && ics.zones[0].daylight.nth == 0);
    CHECK(read_file("BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:99991231T120000Z\n"
                    "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR,SA,SU\nEND:VEVENT\nEND:VCALENDAR\n",
                    utc(9999, 12, 31, 0, 0), 2, 0, 1) == 1);
    /* A nonmatching weekday used to run every iteration, even past year 9999. */
    CHECK(read_file("BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:99991231T120000Z\n"
                    "RRULE:FREQ=DAILY;INTERVAL=2147483647;BYDAY=MO\nEND:VEVENT\nEND:VCALENDAR\n",
                    utc(9999, 12, 31, 0, 0), 2, 0, 1) == 0);
}

int main(void)
{
    test_numeric_bounds();
    test_calendar_names();
    test_monthly_weekdays();
    test_monthly_set_position();
    test_exception_spares();
    test_transition_hours();
    test_opening_at_every_split();
    test_single_events();
    test_summer_time();
    test_repeating();
    test_rejects_non_calendar();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
