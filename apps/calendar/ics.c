/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ics.h"

#include <stddef.h>

#include "app_days.h"
#include "app_text.h"

#define SECONDS_PER_DAY 86400
#define DAYS_PER_WEEK   7
/* The interpreter needs about four bytes for every byte of code that runs:
 * what is called from many places is kept one function, not a copy in each. */
#define KEPT_ONE __attribute__((noinline))
#define NOT_AN_ORIGINAL INT64_MIN /* an occurrence that was moved is nobody's original */

enum { FREQ_NONE, FREQ_DAILY, FREQ_WEEKLY, FREQ_MONTHLY, FREQ_YEARLY };

/* ---- dates ---- */

/* Monday is 0. */
KEPT_ONE static int32_t weekday_of(int32_t days)
{
    int32_t weekday = (days + 3) % DAYS_PER_WEEK;
    return weekday < 0 ? weekday + DAYS_PER_WEEK : weekday;
}

KEPT_ONE static int32_t days_in_month(int32_t year, int32_t month)
{
    return app_days_from_date(month == 12 ? year + 1 : year, month == 12 ? 1 : month + 1, 1) -
           app_days_from_date(year, month, 1);
}

/* The `nth` such day of the week in a month - 1 the first, -1 the last -
 * as days since 1970, or -1 when the month has no such day. */
KEPT_ONE static int32_t nth_weekday_of(int32_t year, int32_t month, int32_t nth, int32_t weekday)
{
    int32_t first = app_days_from_date(year, month, 1), length = days_in_month(year, month);
    if (nth > 0) {
        int32_t day = first + (weekday - weekday_of(first) + DAYS_PER_WEEK) % DAYS_PER_WEEK +
                      (nth - 1) * DAYS_PER_WEEK;
        return day < first + length ? day : -1;
    }
    int32_t last = first + length - 1;
    int32_t day = last - (weekday_of(last) - weekday + DAYS_PER_WEEK) % DAYS_PER_WEEK +
                  (nth + 1) * DAYS_PER_WEEK;
    return day >= first ? day : -1;
}

/* ---- text ---- */

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

KEPT_ONE uint32_t ics_hash(uint32_t hash, const char *text, int32_t length)
{
    while (length-- > 0) {
        hash = (hash ^ (uint8_t)*text++) * 16777619u;
    }
    return hash;
}

KEPT_ONE static uint32_t number_of(const char *text, int32_t length)
{
    uint32_t hash = ics_hash(2166136261u, text, length);
    return hash != 0 ? hash : 1;
}

KEPT_ONE static int32_t digits(const char *text, int32_t count)
{
    int32_t value = 0;
    for (int32_t i = 0; i < count; i++) {
        if (!is_digit(text[i])) {
            return -1;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

/* A number with its sign, and where it ends. */
KEPT_ONE static int32_t signed_number(const char **text)
{
    const char *at = *text;
    bool        is_negative = *at == '-';
    if (*at == '-' || *at == '+') {
        at++;
    }
    int32_t value = 0;
    while (is_digit(*at)) {
        value = value * 10 + (*at++ - '0');
    }
    *text = at;
    return is_negative ? -value : value;
}

/* The value of `name=` among parameters or rule parts separated by ';',
 * up to the next ';' or the text's end; NULL when it is not there. */
static const char *part_named(const char *text, const char *name, int32_t *length)
{
    while (*text != '\0') {
        const char *end = text;
        bool        in_quotes = false;
        while (*end != '\0' && (*end != ';' || in_quotes)) {
            in_quotes ^= *end == '"';
            end++;
        }
        if (app_text_starts(text, name)) {
            const char *value = text + app_text_length(name);
            if (*value == '"') {
                value++;
                *length = (int32_t)(end - value) - (end[-1] == '"' ? 1 : 0);
            } else {
                *length = (int32_t)(end - value);
            }
            return value;
        }
        text = *end == ';' ? end + 1 : end;
    }
    return NULL;
}

/* Packed name tables keep one lookup loop instead of separate call sites. */
KEPT_ONE int32_t ics_name_index(const char *text, const char *names, bool prefix)
{
    if (text == NULL) { return -1; }
    int32_t index = 0;
    while (*names != '\0') {
        const char *at = text, *name = names;
        while (*name != '\0' && *name == *at) { name++; at++; }
        if (*name == '\0' && (prefix || *at == '\0')) { return index; }
        while (*names++ != '\0') { }
        index++;
    }
    return -1;
}

/* Weekday prefixes share the property/frequency lookup. */
static int32_t weekday_named(const char *two)
{
    static const char NAMES[] = "MO\0TU\0WE\0TH\0FR\0SA\0SU\0";
    return ics_name_index(two, NAMES, true);
}

/* Classify each property once; event and zone readers share the names. */
enum {
    PROP_BEGIN, PROP_END, PROP_DTSTART, PROP_DTEND, PROP_DURATION, PROP_SUMMARY,
    PROP_UID, PROP_RRULE, PROP_RECURRENCE_ID, PROP_STATUS, PROP_EXDATE,
    PROP_TZID, PROP_TZOFFSETTO, PROP_CALNAME
};

static int32_t property_of(const char *name)
{
    static const char NAMES[] =
        "BEGIN\0END\0DTSTART\0DTEND\0DURATION\0SUMMARY\0"
        "UID\0RRULE\0RECURRENCE-ID\0STATUS\0EXDATE\0TZID\0TZOFFSETTO\0X-WR-CALNAME\0";
    return ics_name_index(name, NAMES, false);
}

/* ---- moments ---- */

/* "20261113", "20261113T150000" or "20261113T150000Z". */
KEPT_ONE static void moment_of(ics_moment_t *moment, const char *value, const char *parameters)
{
    *moment = (ics_moment_t){ 0 };
    int32_t      year = digits(value, 4), month = digits(value + 4, 2), day = digits(value + 6, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        return;
    }
    moment->days = app_days_from_date(year, month, day);
    moment->is_date = value[8] != 'T';
    if (!moment->is_date) {
        int32_t hour = digits(value + 9, 2), minute = digits(value + 11, 2);
        int32_t second = digits(value + 13, 2);
        if (hour < 0 || minute < 0) {
            return;
        }
        moment->seconds = hour * 3600 + minute * 60 + (second > 0 ? second : 0);
        moment->is_utc = value[15] == 'Z';
    }
    int32_t     length;
    const char *zone = part_named(parameters, "TZID=", &length);
    if (zone != NULL && !moment->is_utc) {
        moment->zone = number_of(zone, length);
    }
    moment->is_set = true;
}

KEPT_ONE static int32_t transition_day(const ics_transition_t *transition, int32_t year)
{
    return transition->nth != 0
               ? nth_weekday_of(year, transition->month, transition->nth, transition->weekday)
               : app_days_from_date(year, transition->month, transition->weekday);
}

/* Compare local clock times so the transition does not begin at midnight. */
static bool is_in_daylight(const ics_zone_t *zone, int32_t days, int32_t seconds)
{
    if (!zone->has_daylight || zone->daylight.month == 0 || zone->standard.month == 0) {
        return false;
    }
    int32_t year, month, day;
    app_date_from_days(days, &year, &month, &day);
    /* With no rule the day a zone last changed stands in for every year. */
    int32_t begins_at = transition_day(&zone->daylight, year);
    int32_t ends_at = transition_day(&zone->standard, year);
    int32_t begin_seconds = zone->daylight.seconds;
    /* A skipped clock time uses the offset before the gap. */
    if (zone->daylight.offset_s > zone->standard.offset_s) {
        begin_seconds += zone->daylight.offset_s - zone->standard.offset_s;
    }
    bool after_begin = days > begins_at || (days == begins_at && seconds >= begin_seconds);
    bool before_end = days < ends_at || (days == ends_at && seconds < zone->standard.seconds);
    return begins_at < ends_at ? after_begin && before_end : after_begin || before_end;
}

KEPT_ONE static int64_t unix_of(const ics_t *ics, const ics_moment_t *moment)
{
    int64_t local = (int64_t)moment->days * SECONDS_PER_DAY + moment->seconds;
    if (moment->is_utc) {
        return local;
    }
    if (moment->zone != 0 && !moment->is_date) {
        for (int32_t i = 0; i < ics->zone_count; i++) {
            const ics_zone_t *zone = &ics->zones[i];
            if (zone->name == moment->zone) {
                return local -
                       (is_in_daylight(zone, moment->days, moment->seconds) ? zone->daylight.offset_s : zone->standard.offset_s);
            }
        }
    }
    /* No zone, a zone the file does not describe, or a whole day: the
     * device's own. */
    return local - ics->device_offset_s;
}

/* ---- the events kept ---- */

KEPT_ONE int32_t ics_insert(ics_event_t *events, int32_t count, int32_t capacity, const ics_event_t *event, uint8_t source)
{
    uint32_t length = (uint32_t)count, at = 0;
    while (at < length && events[at].start <= event->start) { at++; }
    if (at >= (uint32_t)capacity) { return count; }
    if (length < (uint32_t)capacity) { length++; }
    for (uint32_t i = length - 1; i > at; i--) { events[i] = events[i - 1]; }
    events[at] = *event;
    events[at].source = source;
    return (int32_t)length;
}

static void keep(ics_t *ics, int64_t start, int64_t end, int64_t original, bool is_all_day)
{
    if (end <= ics->from || start >= ics->until) { return; }
    ics_event_t event = { .start = start, .end = end, .original = original,
                          .uid = ics->uid, .is_all_day = is_all_day };
    app_text_copy(event.title, sizeof event.title, ics->title);
    ics->count = ics_insert(ics->candidates, ics->count, ICS_CANDIDATES_MAX, &event, 0);
}

static bool is_taken_out(const ics_t *ics, int64_t start)
{
    for (int32_t i = 0; i < ics->exdate_count; i++) {
        if (unix_of(ics, &ics->exdates[i]) == start) {
            return true;
        }
    }
    return false;
}

/* ---- an event that repeats ---- */

#define STEPS_MAX 40000 /* a rule that makes more than this before the window is somebody's mistake */

typedef struct {
    ics_t  *ics;
    int64_t duration;
    int64_t until;  /* of the rule; INT64_MAX: none */
    int32_t made;   /* occurrences so far, for COUNT */
    bool    is_over;
} run_t;

/* One occurrence, on the day `days` of the event's own calendar. */
static void occur(run_t *run, int32_t days)
{
    ics_t *ics = run->ics;
    if (run->is_over || days < ics->start.days) {
        return;
    }
    ics_moment_t moment = ics->start;
    moment.days = days;
    int64_t start = unix_of(ics, &moment);
    if (start > run->until || (ics->rule_count > 0 && run->made >= ics->rule_count) ||
        start >= ics->until) {
        run->is_over = true;
        return;
    }
    run->made++;
    if (!is_taken_out(ics, start)) {
        keep(ics, start, start + run->duration, start, ics->start.is_date);
    }
}

static void repeat(ics_t *ics, int64_t duration)
{
    run_t run = { .ics = ics, .duration = duration, .until = INT64_MAX };
    if (ics->rule_until.is_set) {
        ics_moment_t until = ics->rule_until;
        if (until.is_date) {
            until.seconds = SECONDS_PER_DAY - 1; /* the whole of that day */
        }
        run.until = unix_of(ics, &until);
    }
    int32_t interval = ics->interval > 0 ? ics->interval : 1;
    int32_t first = ics->start.days;
    /* With nothing to count, the days long before the window are stepped
     * over at once: a meeting every day for ten years is still one event. */
    int32_t window_day = (int32_t)((ics->from - duration) / SECONDS_PER_DAY) - 2;
    switch (ics->frequency) {
    case FREQ_DAILY:
    case FREQ_WEEKLY: {
        bool weekly = ics->frequency == FREQ_WEEKLY;
        int32_t span = weekly ? DAYS_PER_WEEK : 1;
        int32_t origin = weekly ? first - weekday_of(first) : first;
        int32_t step = interval * span;
        int32_t start = origin;
        if (ics->rule_count == 0 && window_day > origin) {
            start += (window_day - origin) / step * step;
        }
        uint8_t weekdays = ics->weekdays;
        if (weekly && weekdays == 0) { weekdays = (uint8_t)(1 << weekday_of(first)); }
        for (int32_t i = 0; i < STEPS_MAX && !run.is_over; i++) {
            for (int32_t day = 0; day < span; day++) {
                int32_t days = start + i * step + day;
                if (weekdays == 0 || (weekdays & (1 << weekday_of(days)))) {
                    occur(&run, days);
                }
            }
        }
        break;
    }
    case FREQ_MONTHLY:
    case FREQ_YEARLY: {
        int32_t year, month, day;
        app_date_from_days(first, &year, &month, &day);
        int32_t months = ics->frequency == FREQ_YEARLY ? 12 * interval : interval;
        int32_t wanted_day = ics->month_day != 0 ? ics->month_day : day;
        for (int32_t i = 0; i < STEPS_MAX / 10 && !run.is_over; i++) {
            int32_t month_index = (month - 1) + i * months;
            int32_t its_year = year + month_index / 12, its_month = month_index % 12 + 1;
            int32_t length = days_in_month(its_year, its_month);
            int32_t first_day = app_days_from_date(its_year, its_month, 1);
            int32_t its_day = wanted_day > 0 ? wanted_day : length + 1 + wanted_day;
            if (ics->nth_weekday != 0 && ics->weekdays != 0) {
                int32_t weekday = 0;
                while (!(ics->weekdays & (1 << weekday))) {
                    weekday++;
                }
                int32_t found = nth_weekday_of(its_year, its_month, ics->nth_weekday, weekday);
                if (found < 0) { continue; }
                its_day = found - first_day + 1;
            } else if (ics->weekdays != 0 && ics->month_day == 0 && !ics->has_set_position) {
                for (int32_t offset = 0; offset < length && !run.is_over; offset++) {
                    if (ics->weekdays & (1 << weekday_of(first_day + offset))) {
                        occur(&run, first_day + offset);
                    }
                }
                continue;
            }
            if (its_day >= 1 && its_day <= length) { /* no 31st in a month of thirty */
                occur(&run, first_day + its_day - 1);
            }
        }
        break;
    }
    default:
        break;
    }
}

static void finish_event(ics_t *ics)
{
    if (!ics->start.is_set) {
        return;
    }
    int64_t start = unix_of(ics, &ics->start);
    int64_t duration = ics->end.is_set     ? unix_of(ics, &ics->end) - start
                       : ics->duration_s > 0 ? ics->duration_s
                       : ics->start.is_date  ? SECONDS_PER_DAY
                                             : 0;
    if (duration < 0) {
        duration = 0;
    }
    if (ics->recurrence_id.is_set) {
        /* One occurrence of a repeating event, moved or cancelled: where
         * the rule had it is remembered, and what it is now is an event
         * of its own. */
        int64_t original = unix_of(ics, &ics->recurrence_id);
        bool    is_near = original > ics->from - 2 * SECONDS_PER_DAY && original < ics->until;
        if (is_near && ics->override_count < ICS_OVERRIDES_MAX) {
            ics->overrides[ics->override_count].uid = ics->uid;
            ics->overrides[ics->override_count].original = original;
            ics->override_count++;
        }
        if (!ics->is_cancelled) {
            keep(ics, start, start + duration, NOT_AN_ORIGINAL, ics->start.is_date);
        }
        return;
    }
    if (ics->is_cancelled) {
        return;
    }
    if (ics->has_rule && ics->frequency != FREQ_NONE) {
        repeat(ics, duration);
    } else {
        keep(ics, start, start + duration, start, ics->start.is_date);
    }
}

/* ---- lines ---- */

/* SUMMARY and X-WR-CALNAME with escapes undone: Perinkpheral OS draws what
 * is not ASCII as best it can. Cut between characters, never inside one. */
KEPT_ONE static void take_text(char *text, int32_t capacity, char *value)
{
    /* Unescape the mutable input line in place; the shared copy handles UTF-8
     * truncation after escapes have been removed. The result never grows. */
    char *out = value;
    for (const char *at = value; *at != '\0';) {
        char c = *at++;
        if (c == '\\' && *at != '\0') {
            c = *at == 'n' || *at == 'N' ? ' ' : *at;
            at++;
        }
        *out++ = (uint8_t)c < ' ' ? ' ' : c;
    }
    *out = '\0';
    app_text_copy(text, capacity, value);
}

KEPT_ONE static int32_t rule_number(const char *rule, const char *name, int32_t otherwise)
{
    int32_t length;
    const char *part = part_named(rule, name, &length);
    return part != NULL ? signed_number(&part) : otherwise;
}

static int32_t frequency_of(const char *part)
{
    static const char NAMES[] = "DAILY\0WEEKLY\0MONTHLY\0YEARLY\0";
    return ics_name_index(part, NAMES, true) + FREQ_DAILY;
}

/* "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;UNTIL=20270101T000000Z" */
static void take_rule(ics_t *ics, const char *rule)
{
    int32_t     length;
    const char *part = part_named(rule, "FREQ=", &length);
    ics->has_rule = true;
    /* Positional rules retain the plain-frequency fallback. */
    ics->has_set_position = part_named(rule, "BYSETPOS=", &length) != NULL;
    ics->frequency = frequency_of(part);
    ics->interval = rule_number(rule, "INTERVAL=", ics->interval);
    ics->rule_count = rule_number(rule, "COUNT=", ics->rule_count);
    if ((part = part_named(rule, "UNTIL=", &length)) != NULL) {
        moment_of(&ics->rule_until, part, "");
        /* An end written without a zone is in the event's own. */
        ics->rule_until.zone = ics->start.zone;
    }
    ics->month_day = (int8_t)rule_number(rule, "BYMONTHDAY=", ics->month_day);
    if ((part = part_named(rule, "BYDAY=", &length)) != NULL) {
        const char *end = part + length;
        while (part < end) {
            int32_t nth = signed_number(&part);
            int32_t weekday = part + 1 < end ? weekday_named(part) : -1;
            if (weekday >= 0) {
                ics->weekdays |= (uint8_t)(1 << weekday);
                if (nth != 0) {
                    ics->nth_weekday = (int8_t)nth;
                }
            }
            while (part < end && *part != ',') {
                part++;
            }
            part++;
        }
    }
}

/* "+0300", "-0530", "+013000" as seconds east of UTC. */
static int32_t offset_of(const char *value)
{
    int32_t sign = *value == '-' ? -1 : 1;
    if (*value == '-' || *value == '+') {
        value++;
    }
    int32_t hours = digits(value, 2), minutes = digits(value + 2, 2);
    return hours < 0 || minutes < 0 ? 0 : sign * (hours * 3600 + minutes * 60);
}

enum { COMPONENT_CALENDAR, COMPONENT_EVENT, COMPONENT_ZONE, COMPONENT_DAYLIGHT, COMPONENT_STANDARD };

static void zone_line(ics_t *ics, int32_t property, const char *value, int32_t component)
{
    if (ics->zone_count >= ICS_ZONES_MAX && property != PROP_END) {
        return;
    }
    ics_zone_t *zone = &ics->zones[ics->zone_count < ICS_ZONES_MAX ? ics->zone_count : 0];
    ics_transition_t *transition = ics->in_daylight ? &zone->daylight : &zone->standard;
    if (property == PROP_TZID) {
        zone->name = number_of(value, app_text_length(value));
    } else if (property == PROP_BEGIN) {
        ics->in_daylight = component == COMPONENT_DAYLIGHT;
        ics->in_standard = component == COMPONENT_STANDARD;
    } else if (property == PROP_END) {
        if (component == COMPONENT_ZONE) {
            ics->in_zone = false;
            if (ics->zone_count < ICS_ZONES_MAX) {
                if (!zone->has_daylight) {
                    zone->daylight.offset_s = zone->standard.offset_s;
                }
                ics->zone_count++;
            }
        }
        ics->in_daylight = ics->in_standard = false;
    } else if (!ics->in_daylight && !ics->in_standard) {
        return;
    } else if (property == PROP_TZOFFSETTO) {
        transition->offset_s = offset_of(value);
        if (ics->in_daylight) { zone->has_daylight = true; }
    } else if (property == PROP_DTSTART) {
        ics_moment_t moment;
        moment_of(&moment, value, "");
        transition->seconds = moment.seconds;
        /* The day it changed, in case no rule says when it changes. */
        int32_t month = digits(value + 4, 2), day = digits(value + 6, 2);
        if (month >= 1 && day >= 1 && transition->nth == 0) {
            transition->month = (int8_t)month;
            transition->weekday = (int8_t)day;
        }
    } else if (property == PROP_RRULE) {
        int32_t     length;
        const char *month = part_named(value, "BYMONTH=", &length);
        const char *day = part_named(value, "BYDAY=", &length);
        if (month != NULL && day != NULL) {
            int32_t its_month = signed_number(&month);
            int32_t nth = signed_number(&day);
            int32_t weekday = weekday_named(day);
            if (its_month >= 1 && its_month <= 12 && nth != 0 && weekday >= 0) {
                transition->month = (int8_t)its_month;
                transition->nth = (int8_t)nth;
                transition->weekday = (int8_t)weekday;
            }
        }
    }
}

static void begin_event(ics_t *ics)
{
    ics->in_event = true;
    ics->nested = 0;
    /* Reset the contiguous event fields together; zones and candidates survive. */
    for (size_t i = offsetof(ics_t, start); i < offsetof(ics_t, overrides); i++) {
        ((uint8_t *)ics)[i] = 0;
    }
}

/* "P1D", "PT1H30M", "P1W": what an event lasts when it has no end. */
static int32_t duration_of(const char *value)
{
    int32_t seconds = 0;
    bool    in_time = false;
    for (const char *at = value; *at != '\0';) {
        if (*at == 'T') {
            in_time = true;
            at++;
        } else if (is_digit(*at)) {
            int32_t amount = signed_number(&at);
            static const char UNITS[] = "WDHMS";
            static const int32_t FACTORS[] = { 7 * SECONDS_PER_DAY, SECONDS_PER_DAY, 3600, 60, 1 };
            for (int32_t i = 0; i < 5; i++) {
                if (*at == UNITS[i] && (*at != 'M' || in_time)) { seconds += amount * FACTORS[i]; }
            }
        } else {
            at++;
        }
    }
    return seconds;
}

static void event_line(ics_t *ics, int32_t property, const char *parameters, char *value)
{
    if (property == PROP_BEGIN) {
        ics->nested++;
        return;
    }
    if (property == PROP_END) {
        if (ics->nested > 0) {
            ics->nested--;
        } else {
            ics->in_event = false;
            finish_event(ics);
        }
        return;
    }
    if (ics->nested > 0) {
        return; /* an alarm's own lines */
    }
    ics_moment_t *moment = property == PROP_DTSTART ? &ics->start
                           : property == PROP_DTEND ? &ics->end
                           : property == PROP_RECURRENCE_ID ? &ics->recurrence_id : NULL;
    if (moment != NULL) {
        moment_of(moment, value, parameters);
    } else if (property == PROP_DURATION) {
        ics->duration_s = duration_of(value);
    } else if (property == PROP_SUMMARY) {
        take_text(ics->title, sizeof ics->title, value);
    } else if (property == PROP_UID) {
        ics->uid = number_of(value, app_text_length(value));
    } else if (property == PROP_RRULE) {
        take_rule(ics, value);
    } else if (property == PROP_STATUS) {
        ics->is_cancelled = app_text_equal(value, "CANCELLED");
    } else if (property == PROP_EXDATE) {
        /* One or several, separated by commas. */
        for (const char *at = value; *at != '\0' && ics->exdate_count < ICS_EXDATES_MAX;) {
            ics_moment_t moment;
            moment_of(&moment, at, parameters);
            /* Only the ones that could matter: a meeting of some years has
             * many, nearly all of them long past. */
            int64_t taken_out = moment.is_set ? unix_of(ics, &moment) : 0;
            if (moment.is_set && taken_out > ics->from - 2 * SECONDS_PER_DAY &&
                taken_out < ics->until) {
                ics->exdates[ics->exdate_count++] = moment;
            }
            while (*at != '\0' && *at != ',') {
                at++;
            }
            at += *at == ',';
        }
    }
}

/* One whole line: "NAME;PARAMETERS:VALUE". */
static void take_line(ics_t *ics)
{
    char *line = ics->line;
    line[ics->line_length] = '\0';
    ics->line_length = 0;
    /* The name ends at the first ';' or ':'; the value begins after the
     * first ':' that is not inside quotes. */
    char *parameters = line;
    while (*parameters != '\0' && *parameters != ';' && *parameters != ':') {
        parameters++;
    }
    char *value = parameters;
    bool  in_quotes = false;
    while (*value != '\0' && (*value != ':' || in_quotes)) {
        in_quotes ^= *value == '"';
        value++;
    }
    if (*value != ':') {
        return;
    }
    bool has_parameters = *parameters == ';';
    *parameters = '\0';
    *value++ = '\0';
    const char *parameter_text = has_parameters ? parameters + 1 : "";

    int32_t property = property_of(line);
    static const char COMPONENTS[] = "VCALENDAR\0VEVENT\0VTIMEZONE\0DAYLIGHT\0STANDARD\0";
    int32_t component = property == PROP_BEGIN || property == PROP_END
                            ? ics_name_index(value, COMPONENTS, false) : -1;
    if (!ics->in_event && !ics->in_zone && property == PROP_END &&
        component == COMPONENT_CALENDAR) {
        ics->complete = true;
    } else if (ics->in_event) {
        event_line(ics, property, parameter_text, value);
    } else if (ics->in_zone) {
        zone_line(ics, property, value, component);
    } else if (property == PROP_CALNAME) {
        take_text(ics->name, sizeof ics->name, value);
    } else if (property == PROP_BEGIN && component == COMPONENT_EVENT) {
        begin_event(ics);
    } else if (property == PROP_BEGIN && component == COMPONENT_ZONE) {
        ics->in_zone = true;
        if (ics->zone_count < ICS_ZONES_MAX) {
            ics->zones[ics->zone_count] = (ics_zone_t){ 0 };
        }
    }
}

void ics_begin(ics_t *ics, ics_event_t *events, int64_t from, int64_t until,
               int32_t device_offset_s)
{
    *ics = (ics_t){ .from = from, .until = until, .device_offset_s = device_offset_s,
                    .events = events };
}

void ics_feed(ics_t *ics, const char *data, int32_t length)
{
    for (int32_t i = 0; i < length; i++) {
        char c = data[i];
        if (ics->after_break) {
            ics->after_break = false;
            if (c == ' ' || c == '\t') {
                continue; /* the line goes on: a long one was folded */
            }
            take_line(ics);
        }
        if (c == '\n') {
            ics->after_break = true;
        } else if (c != '\r' && ics->line_length < ICS_LINE_MAX) {
            ics->line[ics->line_length++] = c;
        }
    }
}

int32_t ics_end(ics_t *ics)
{
    if (ics->line_length > 0) {
        take_line(ics);
    }
    /* What somebody moved or cancelled is not where the rule had it. */
    int32_t kept = 0;
    for (int32_t i = 0; i < ics->count && kept < ICS_EVENTS_MAX; i++) {
        bool is_replaced = false;
        for (int32_t j = 0; j < ics->override_count; j++) {
            if (ics->candidates[i].uid == ics->overrides[j].uid &&
                ics->candidates[i].original == ics->overrides[j].original) {
                is_replaced = true;
                break;
            }
        }
        if (!is_replaced) {
            ics->events[kept++] = ics->candidates[i];
        }
    }
    ics->count = kept;
    return kept;
}

/* 0 until the marker is whole, 1 when valid, -1 for another document.
 * The stream may split even a UTF-8 encoding mark between pieces. */
int ics_opening_feed(ics_opening_t *opening, const uint8_t *data, int32_t length)
{
    static const uint8_t MARKER[] = "\xEF\xBB\xBF" "BEGIN:VCALENDAR";
    /* Skip the optional encoding mark when the very first byte is ordinary text. */
    if (opening->length == 0 && length > 0 && data[0] != MARKER[0]) {
        opening->length = ICS_ENCODING_MARK_BYTES;
    }
    for (int32_t i = 0; i < length && opening->length < (int32_t)sizeof MARKER - 1; i++) {
        if (data[i] != MARKER[opening->length++]) { return -1; }
    }
    return opening->length == sizeof MARKER - 1 ? 1 : 0;
}
