/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Space Launch: rocket launches from The Space Devs' Launch Library 2, and how many there
 * were. Four screens, switched with held UP and DOWN:
 *   Launches   the next launches and, scrolled up, the last ones with their results;
 *              OK opens a launch's page over the list, BACK closes it.
 *   Countries  this year's launches by the provider's country, bars against the leader.
 *   Months     a year's launches by month; UP and DOWN choose the year.
 *   History    orbital launches a year since 1957; UP and DOWN choose the year.
 * Picked poll requests are gated by the application, with a persistent service-wide cooldown.
 * Gated picked polls refresh the lists every half hour / three hours and statistics daily.
 * Past years come with the package (files/history.bin, from GCAT).
 * A 429 keeps what is there and waits the time the server names. A reminder can come some
 * time before a launch that is Go, as a notification.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_days.h"
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h"

enum { POLL_UPCOMING, POLL_PREVIOUS, POLL_YEAR, POLLS };
enum { SCREEN_LAUNCHES, SCREEN_COUNTRIES, SCREEN_MONTHS, SCREEN_HISTORY, SCREENS };
/* Launch Library 2 status ids. */
enum { ST_GO = 1, ST_TBD, ST_SUCCESS, ST_FAILURE, ST_HOLD, ST_FLIGHT, ST_PARTIAL, ST_TBC, ST_DEPLOYED };
/* Launch Library 2 NET precision ids: 0 second, 1 minute, 2 hour, 3..6 a part of a day to a
 * week, 7 month, 8..16 a quarter to a decade. */
#define PRECISION_HOUR    2
#define PRECISION_MONTH   7
#define PRECISION_UNKNOWN 255

#define LIST_MAX     10 /* launches a list request asks for */
#define TEXT_MAX     64 /* a picked value is at most 63 bytes */
#define PAGE_SIZE    10
#define ROWS         4
#define COUNTRIES    6  /* USA, China, Russia, Europe, India, Japan; Others are the rest */
#define FIRST_YEAR   1957
#define HTTP_OK      200
#define HTTP_TOO_MANY 429

#define MINUTE           60
#define HOUR             3600
#define DAY              86400
#define STALE_S          (40 * MINUTE)
#define COUNTDOWN_S      DAY
#define SLIP_SHOWN_S     DAY
#define FRESH_S          (3 * HOUR)    /* reminders come only from a list this recent */
#define LATE_S           (5 * MINUTE)  /* a reminder this late is not given */
#define RETRY_DEFAULT_S  HOUR          /* a 429 without Retry-After */
#define REMIND_ALARM     0
#define FETCH_ALARM      1
#define SAVE_ALARM       2
#define REQUEST_GAP      (5 * MINUTE)
#define TOLD_MAX         16

__attribute__((noinline)) static const char *word(int32_t id)
{
    return app_string(id);
}

/* One wrapper owns the host formatter's missing terminator. */
__attribute__((noinline)) static int32_t format_text(char *out, int32_t size, const char *pattern,
                                                    const host_text_argument_t *args, int32_t count)
{
    int32_t length = text_fill(pattern, app_text_length(pattern), args, count, out, size - 1);
    out[length > 0 ? length : 0] = '\0';
    return length;
}

__attribute__((noinline)) static void fill_number(char *out, int32_t size, int32_t pattern, int32_t number)
{
    host_text_argument_t arg = { .number = number };
    format_text(out, size, pattern < 0 ? "{0}" : word(pattern), &arg, 1);
}

/* A number as text, in a buffer of its own until the next call. */
__attribute__((noinline)) static const char *number_text(int32_t number)
{
    static char text[12];
    fill_number(text, sizeof text, -1, number);
    return text;
}

/* The host formatter copies text within the same UTF-8 and capacity limits. */
static void copy_text(char *to, int32_t size, const char *from)
{
    if (size <= 0) { return; }
    host_text_argument_t arg = { .text = from, .length = app_text_length(from) };
    format_text(to, size, "{0}", &arg, 1);
}

static bool set_text(const char *key, const char *value)
{
    return kv_set(key, app_text_length(key), value, app_text_length(value)) == HOST_OK;
}

/* ---- what is kept ---- */

typedef struct {
    uint32_t id;               /* a hash of the launch's UUID */
    uint8_t  status, precision;
    uint16_t spare;            /* no padding: records are compared byte by byte */
    int64_t  net;              /* Unix seconds */
    int64_t  slipped_at;
    union {
        struct { char name[TEXT_MAX], provider[TEXT_MAX], site[TEXT_MAX]; };
        char text[3][TEXT_MAX];
    };
} launch_t;

/* A year by month: the layout of a record of files/history.bin. */
typedef struct {
    uint16_t months[12];
    uint16_t failures;
} year_t;

#define CACHE_VERSION 3
/* Background answers use fresh instances. Publish annual totals only after all UUID-ordered pages. */
typedef struct {
    int32_t year, total, offset;
    char last_id[37];
    year_t sum;
    int32_t countries[COUNTRIES];
} annual_t;
typedef struct {
    int32_t version;
    uint32_t revision;
    int32_t count[2];
    int32_t spare;
    launch_t lists[2][LIST_MAX];
    int32_t year, total;
    uint32_t pages_have, countries_have; /* complete published snapshots, never partial masks */
    year_t annual;
    int32_t countries[COUNTRIES];
    int32_t last_year;
    year_t last;
    annual_t staging;
} cache_t;
_Static_assert(sizeof(cache_t) + 8 <= 8192, "a bank fits in two file blocks");
_Static_assert(sizeof(year_t) == 26 && sizeof(launch_t) == 216, "records without padding");

typedef struct {
    uint32_t generation, checksum;
    cache_t data;
} bank_t;
static const char *const BANKS[] = { "cache-a", "cache-b" };
static const char *const GATES[] = { "list_gate", "past_gate", "year_gate" };
static bank_t bank_buffer;
static uint32_t cache_generation;
static int32_t cache_bank;

#define STATE_KEY "state"
#define STATE_VERSION 3
typedef struct {
    int32_t version;
    int32_t status[POLLS];
    int32_t told_next;
    uint32_t told[TOLD_MAX];
    union {
        struct { int64_t seen, cooldown, next_send, due[3]; };
        int64_t deadlines[6];
    };
    int64_t clock, uptime;
    uint32_t boot;
    int32_t active_poll, request, item, year, cursor;
    int32_t cache_pending;
    uint32_t revision;
} state_t;
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "state fits one KV value");
static cache_t cache;
static state_t state;
static int64_t seen_at;
static bool cache_dirty, state_dirty;


/* Share record transfers instead of repeating field loads and stores at each call site. */
__attribute__((noinline)) static void copy_record(void *to, const void *from, uint32_t size)
{
    __builtin_memcpy(to, from, size);
}
static void clear_record(void *to, uint32_t size)
{
    __builtin_memset(to, 0, size);
}

static bool is_same(const void *one, const void *other, uint32_t size)
{
    const uint8_t *a = one, *b = other;
    while (size-- > 0) { if (a[size] != b[size]) { return false; } }
    return true;
}

static bool save_state(void)
{
    state_dirty = kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) != HOST_OK;
    if (state_dirty) { alarm_set(SAVE_ALARM, MINUTE); }
    return !state_dirty;
}

static inline __attribute__((always_inline)) bool close_gate(int32_t poll)
{
    return set_text(GATES[poll], "");
}

static uint32_t checksum(const bank_t *bank)
{
    const uint8_t *bytes = (const uint8_t *)&bank->data;
    uint32_t hash = 2166136261u ^ bank->generation;
    uint32_t size = sizeof bank->data;
    while (size--) { hash = (hash ^ *bytes++) * 16777619u; }
    return hash;
}

static bool valid_bank(const bank_t *bank)
{
    return bank->data.version == CACHE_VERSION &&
           bank->checksum == checksum(bank) && (uint32_t)bank->data.count[0] <= LIST_MAX &&
           (uint32_t)bank->data.count[1] <= LIST_MAX;
}

static void save_cache(void)
{
    int32_t target = 1 - cache_bank;
    bank_buffer.generation = cache_generation + 1;
    copy_record(&bank_buffer.data, &cache, sizeof cache);
    bank_buffer.checksum = checksum(&bank_buffer);
    cache_dirty = file_write(BANKS[target], 7, 0, &bank_buffer, sizeof bank_buffer) != HOST_OK;
    /* Read back before advancing the generation: never overwrite the last valid bank. */
    if (!cache_dirty) {
        cache_dirty = file_read(BANKS[target], 7, 0, &bank_buffer, sizeof bank_buffer) != sizeof bank_buffer ||
                      (!valid_bank(&bank_buffer) || bank_buffer.generation != cache_generation + 1);
    }
    if (cache_dirty) { alarm_set(SAVE_ALARM, MINUTE); }
    else {
        cache_bank = target;
        cache_generation = bank_buffer.generation;

    }
}

static void persist(void)
{
    if (cache_dirty) { save_cache(); }
    if (state.cache_pending != cache_dirty) { state.cache_pending = cache_dirty; state_dirty = true; }
    if (state_dirty) { save_state(); }

}

static void fetch(void);
static void update_header(void);

/* ---- time ---- */

static int32_t digits(const char *text, int32_t from, int32_t count)
{
    int32_t value = 0;
    for (int32_t i = from; i < from + count; i++) {
        int32_t digit = (uint8_t)text[i] - '0';
        if ((uint32_t)digit > 9) { return -1; }
        value = value * 10 + digit;
    }
    return value;
}

/* A whole number as the library writes ids and counts; -1 for anything else. */
static int32_t number_of(const char *text)
{
    int32_t length = app_text_length(text);
    return length > 0 && length < 10 ? digits(text, 0, length) : -1;
}

/* Shared validation of ISO NET and Retry-After clock components. */
static int32_t clock_seconds(const char *text)
{
    int32_t hour = digits(text, 0, 2), minute = digits(text, 3, 2), second = digits(text, 6, 2);
    if ((uint32_t)hour > 23 || (uint32_t)minute > 59 || (uint32_t)second > 59) { return -1; }
    return hour * HOUR + minute * MINUTE + second;
}

/* "2026-11-24T10:13:00Z" (or with ".123Z"); 0 when it is not such a moment. */
static int64_t parse_moment(const char *text)
{
    if (app_text_length(text) < 19) { return 0; }
    int32_t year = digits(text, 0, 4), month = digits(text, 5, 2), day = digits(text, 8, 2);
    int32_t seconds = clock_seconds(text + 11);
    if (year < FIRST_YEAR || month < 1 || month > 12 || day < 1 || seconds < 0 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':') { return 0; }
    int32_t end = 19;
    if (text[end] == '.') {
        end++;
        int32_t start = end;
        while ((uint32_t)((uint8_t)text[end] - '0') <= 9) { end++; }
        if (end == start) { return 0; }
    }
    if (text[end] != 'Z' || text[end + 1]) { return 0; }
    static const uint8_t DAYS[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int32_t limit = DAYS[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 || year % 400 == 0));
    if (day > limit) { return 0; }
    return (int64_t)app_days_from_date(year, month, day) * DAY + seconds;
}

static int32_t utc_year(int64_t moment)
{
    int32_t year, month, day;
    app_date_from_days((int32_t)(moment / DAY), &year, &month, &day);
    return year;
}

/* ---- the years ---- */

#define HISTORY_FILE "history.bin"
static year_t  history[128];
static int32_t history_count = -1; /* -1: not read yet */

static void read_history(void)
{
    if (history_count >= 0) { return; }
    uint16_t head[2] = { 0, 0 };
    history_count = 0;
    if (file_read(HISTORY_FILE, sizeof HISTORY_FILE - 1, 0, head, sizeof head) != sizeof head ||
        head[0] != FIRST_YEAR || head[1] > 128) {
        return;
    }
    int32_t bytes = head[1] * (int32_t)sizeof(year_t);
    if (file_read(HISTORY_FILE, sizeof HISTORY_FILE - 1, sizeof head, history, bytes) == bytes) {
        history_count = head[1];
    }
}



__attribute__((noinline)) static int32_t year_total(const year_t *year)
{
    int32_t total = 0;
    for (int32_t month = 0; month < 12; month++) { total += year->months[month]; }
    return total;
}

/* A year's launches by month, when they are known. */
__attribute__((noinline)) static bool year_of(int32_t number, year_t *year)
{
    read_history();
    const year_t *source;
    if (number == cache.year && cache.pages_have) {
        source = &cache.annual;
    } else if (number >= FIRST_YEAR && number < FIRST_YEAR + history_count) {
        source = &history[number - FIRST_YEAR];
    } else if (number == cache.last_year && cache.last_year != 0) {
        source = &cache.last;
    } else { return false; }
    copy_record(year, source, sizeof *year);
    return true;
}

/* The latest year there is anything for: this one once the clock is known. */
static int32_t latest_year(void)
{
    read_history();
    int32_t latest = cache.year > 0 ? cache.year : FIRST_YEAR + history_count - 1;
    return latest < FIRST_YEAR ? FIRST_YEAR : latest;
}

/* At the turn of the year the pages start again. The preceding snapshot remains incomplete
 * until packaged history supersedes it; the charts label it accordingly. */
static void follow_year(void)
{
    int64_t now = time_now();
    if (now == 0) { return; }
    int32_t year = utc_year(now);
    if (cache.year != year) {
        if (cache.year == year - 1 && cache.pages_have) {
            cache.last_year = cache.year;
            copy_record(&cache.last, &cache.annual, sizeof cache.last);
        }
        cache.year = year;
        cache.total = 0;
        cache.pages_have = cache.countries_have = 0;
        clear_record(&cache.staging, sizeof cache.staging);
        state.due[2] = 0;
        state_dirty = true;
        cache_dirty = true;
    }

}

/* ---- the launches ---- */

static bool is_done(const launch_t *launch)
{
    return launch->status == ST_SUCCESS || launch->status == ST_FAILURE || launch->status == ST_PARTIAL ||
           launch->status == ST_DEPLOYED;
}

static const launch_t *find_launch(uint32_t id)
{
    for (int32_t list = 0; list < 2; list++) {
        for (int32_t i = 0; i < cache.count[list]; i++) {
            if (cache.lists[list][i].id == id) { return &cache.lists[list][i]; }
        }
    }
    return NULL;
}

/* The rows of the list, in order of time: the previous launches that the upcoming list no longer
 * has (it keeps those of the last day), then the upcoming ones. */
static const launch_t *rows[2 * LIST_MAX];
static int32_t         row_count;

static void build_rows(void)
{
    row_count = 0;
    for (int32_t i = cache.count[1] - 1; i >= 0; i--) {
        const launch_t *launch = &cache.lists[1][i];
        bool doubled = false;
        for (int32_t j = 0; j < cache.count[0]; j++) { doubled |= cache.lists[0][j].id == launch->id; }
        if (!doubled) { rows[row_count++] = launch; }
    }
    for (int32_t j = 0; j < cache.count[0]; j++) { rows[row_count++] = &cache.lists[0][j]; }
}

static uint32_t hash_of(const char *text)
{
    uint32_t hash = 2166136261u;
    while (*text) { hash = (hash ^ (uint8_t)(*text++ | 32)) * 16777619u; }
    return hash ? hash : 1;
}

static bool uuid_valid(const char *text)
{
    for (int32_t i = 0; i < 36; i++) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') { return false; } }
        else { c |= 32; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { return false; } }
    }
    return !text[36];
}

static bool present_text(const char *text)
{
    if (!text[0] || is_same(text, "null", 5)) { return false; }
    bool nonspace = false;
    for (; *text; text++) {
        if ((uint8_t)*text < 32) { return false; }
        nonspace |= *text != ' ';
    }
    return nonspace;
}

/* The picker cuts between UTF-8 characters. A full scalar may have lost its tail. */
static void mark_truncated(char *text)
{
    if (app_text_length(text) < TEXT_MAX - 4) { return; }
    int32_t end = TEXT_MAX - 4;
    while (end > 0 && ((uint8_t)text[end] & 0xc0) == 0x80) { end--; }
    copy_text(text + end, TEXT_MAX - end, "...");
}

/* Indices of the manifest's picked paths. */
enum { PICK_ID, PICK_NAME, PICK_NET, PICK_STATUS, PICK_PRECISION, PICK_PROVIDER, PICK_SITE, PICK_COUNT };
enum { PICK_LSP = PICK_COUNT + 1 };
__attribute__((noinline)) static bool value_text(int32_t path, int32_t element, char *out, int32_t size)
{
    int32_t length = answer_element(path, element, out, size - 1);
    out[length > 0 ? length : 0] = '\0';
    return length >= 0;
}
#define value_count answer_count
__attribute__((noinline)) static int32_t picked_number(int32_t path, int32_t element)
{
    char text[16];
    return value_text(path, element, text, sizeof text) ? number_of(text) : -1;
}
__attribute__((noinline)) static uint8_t number_column(int32_t path, int32_t element, int32_t count, uint8_t otherwise)
{
    int32_t number = value_count(path) == count ? picked_number(path, element) : -1;
    return number < 0 || number >= PRECISION_UNKNOWN ? otherwise : (uint8_t)number;
}

static launch_t incoming[LIST_MAX];
/* A picked list of launches; false when the answer is not one. */
static bool take_launches(int32_t list)
{
    int32_t count = value_count(PICK_ID), total = picked_number(PICK_COUNT, 0);
    if (total < 0 || count != (total < LIST_MAX ? total : LIST_MAX) ||
        value_count(PICK_NAME) != count || value_count(PICK_NET) != count) {
        return false;
    }
    int64_t now = time_now();
    for (int32_t i = 0; i < count; i++) {
        launch_t *launch = &incoming[i];
        char text[TEXT_MAX];
        clear_record(launch, sizeof *launch);
        value_text(PICK_ID, i, text, sizeof text);
        if (!uuid_valid(text)) { return false; }
        launch->id = hash_of(text);
        for (int32_t j = 0; j < i; j++) { if (incoming[j].id == launch->id) { return false; } }
        static const uint8_t TEXT_PATHS[] = { PICK_NAME, PICK_PROVIDER, PICK_SITE };
        for (int32_t field = 0; field < 3; field++) {
            char *destination = launch->text[field];
            if (value_count(TEXT_PATHS[field]) == count) { value_text(TEXT_PATHS[field], i, destination, TEXT_MAX); }
            if (!present_text(destination)) {
                if (!field) { return false; }
                copy_text(destination, TEXT_MAX, "?");
            }
        }
        mark_truncated(launch->name);
        value_text(PICK_NET, i, text, sizeof text);
        launch->net = parse_moment(text);
        if (launch->net == 0) { return false; }
        launch->status = number_column(PICK_STATUS, i, count, 0);
        launch->precision = number_column(PICK_PRECISION, i, count, PRECISION_UNKNOWN);
        if (launch->precision > 16) { launch->precision = PRECISION_UNKNOWN; }
        /* A launch whose time moved says so for a day. */
        const launch_t *before = find_launch(launch->id);
        if (before != NULL) {
            int64_t moved = launch->net - before->net;
            launch->slipped_at = moved >= MINUTE || moved <= -MINUTE ? now : before->slipped_at;
        }
    }
    if (cache.count[list] != count || !is_same(cache.lists[list], incoming, (uint32_t)(count * sizeof(launch_t)))) {
        cache.count[list] = count;
        if (!list) { cache.revision++; }
        copy_record(cache.lists[list], incoming, count * sizeof(launch_t));
        cache_dirty = true;
    }
    return true;
}


/* Stable UUID order, including the boundary between pages, is independent of NET.
 * Country counts come from these same rows, so they cannot mix annual generations. */
static bool take_page(void)
{
    annual_t *stage = &cache.staging;
    int32_t count = value_count(PICK_ID), total = picked_number(PICK_COUNT, 0);
    if (state.year != cache.year || state.item != stage->offset || total < state.item || total > UINT16_MAX ||
        (state.item && stage->total != total) || value_count(PICK_NET) != count ||
        value_count(PICK_STATUS) != count || value_count(PICK_LSP) > count ||
        count != (total - state.item < PAGE_SIZE ? total - state.item : PAGE_SIZE)) { return false; }
    annual_t next;
    copy_record(&next, stage, sizeof next);
    next.year = state.year; next.total = total;
    static const uint16_t PROVIDERS[] = { 121,124,141,147,257,265,44,266,285,1030,0,88,189,184,194,259,272,274,1021,1040,1049,1080,17,263,0,63,193,96,103,111,163,154,119,0,115,27,159,1046,1051,1066,46,0,31,190,1099,0,98,37,1083,1002,1009,0 };
    for (int32_t i = 0; i < count; i++) {
        char id[TEXT_MAX], text[TEXT_MAX];
        value_text(PICK_ID, i, id, sizeof id);
        int32_t cmp = 0;
        for (int32_t j = 0; j < 36 && !cmp; j++) { cmp = (uint8_t)(id[j] | 32) - (uint8_t)(next.last_id[j] | 32); }
        value_text(PICK_NET, i, text, sizeof text);
        int64_t net = parse_moment(text);
        int32_t status = picked_number(PICK_STATUS, i), provider = picked_number(PICK_LSP, i);
        if (!uuid_valid(id) || cmp <= 0 || !net || digits(text, 0, 4) != state.year ||
            (status != ST_SUCCESS && status != ST_FAILURE && status != ST_PARTIAL)) { return false; }
        copy_text(next.last_id, sizeof next.last_id, id);
        next.sum.months[digits(text, 5, 2) - 1]++;
        next.sum.failures += status != ST_SUCCESS;
        /* Country totals do not depend on row alignment: missing IDs count under Others. */
        int32_t country = 0;
        for (uint32_t j = 0; j < sizeof PROVIDERS / sizeof PROVIDERS[0]; j++) {
            if (PROVIDERS[j] && PROVIDERS[j] == provider) { next.countries[country]++; break; }
            if (!PROVIDERS[j]) { country++; }
        }
    }
    next.offset += count;
    copy_record(stage, &next, sizeof next);
    if (next.offset == total) {
        copy_record(&cache.annual, &next.sum, sizeof next.sum);
        cache.total = total;
        copy_record(cache.countries, next.countries, sizeof cache.countries);
        cache.pages_have = 1;
        cache.countries_have = (1u << COUNTRIES) - 1;
        clear_record(stage, sizeof *stage);
        state.due[POLL_YEAR] = time_now() + DAY;
    }
    cache_dirty = true;
    return true;
}

/* "Falcon 9 Block 5 | NROL-136": the rocket into `rocket`, and the mission returned; a name
 * without " | " is all mission. */
__attribute__((noinline)) static const char *split_name(const launch_t *launch, char *rocket)
{
    const char *name = launch->name;
    int32_t length = 0;
    while (name[length] && !(name[length] == ' ' && name[length + 1] == '|')) { length++; }
    rocket[0] = '\0';
    if (!name[length]) { return name; }
    copy_text(rocket, length + 1, name);
    return name + length + (name[length + 2] == ' ' ? 3 : 2);
}

/* ---- reminders ---- */

static int32_t lead_seconds(void)
{
    char choice[8]; /* "Off", "15 min", "1 h" */
    app_setting("remind", choice, sizeof choice);
    return choice[0] != '1' ? -1 : choice[1] == '5' ? 15 * MINUTE : HOUR;
}

static bool was_told(uint32_t id)
{
    for (int32_t i = 0; i < TOLD_MAX; i++) {
        if (state.told[i] == id && id != 0) { return true; }
    }
    return false;
}

/* A launch is reminded of once, at the lead it has when it comes: Go, its time to the minute,
 * from a list fresh enough to believe. Once told it is not told again, wherever its time moves. */
__attribute__((noinline)) static bool needs_reminder(const launch_t *launch, int64_t now, int32_t lead)
{
    return lead >= 0 && now != 0 && seen_at != 0 && now >= seen_at && now - seen_at < FRESH_S && launch->status == ST_GO &&
           (launch->precision < PRECISION_HOUR) &&
           launch->net > now && launch->net - lead + LATE_S >= now && !was_told(launch->id);
}

static void schedule_reminder(void)
{
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    alarm_set(REMIND_ALARM, 0);
    for (int32_t i = 0; i < cache.count[0]; i++) {
        const launch_t *launch = &cache.lists[0][i];
        if (!needs_reminder(launch, now, lead)) { continue; }
        int64_t due = launch->net - lead;
        if (due <= now) { alarm_set(REMIND_ALARM, 1); }
        else { alarm_set_at(REMIND_ALARM, due); }
        return; /* the soonest: the list is in order of time */
    }
}

void app_on_alarm(int32_t id)
{
    if (id == FETCH_ALARM) {
        /* A background poll that cannot start may have no callback. Pending polls coalesce;
         * a queued repeat reads the gate after the preceding answer has closed it. */
        int32_t pending = state.active_poll;
        fetch();
        if (pending) { poll_run(pending - 1); }
        update_header(); screen_redraw(HOST_REDRAW_NORMAL); return;
    }
    if (id == SAVE_ALARM) { persist(); fetch(); return; }
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    for (int32_t i = 0; i < cache.count[0]; i++) {
        const launch_t *launch = &cache.lists[0][i];
        if (!needs_reminder(launch, now, lead) || launch->net - lead > now) { continue; }
        host_notice_t notice;
        clear_record(&notice, sizeof notice);
        notice.icon = HOST_ICON_BELL;
        app_time_text(launch->net, HOST_TIME_CLOCK, notice.first, sizeof notice.first);
        char rocket[TEXT_MAX];
        copy_text(notice.detail, sizeof notice.detail, split_name(launch, rocket));
        /* A failed preflight consumes no launch ID. Handlers run to completion on normal unloads. */
        if (!save_state()) { alarm_set(REMIND_ALARM, MINUTE); return; }
        if (notification_post(&notice) != HOST_OK) {
            alarm_set(REMIND_ALARM, MINUTE);
            return;
        }
        state.told[state.told_next] = launch->id;
        state.told_next = (state.told_next + 1) % TOLD_MAX;
        if (!save_state()) {
            uint32_t receipt[2] = { launch->id, ~launch->id };
            file_write("told", 4, 0, receipt, sizeof receipt);
        }
    }
    schedule_reminder();
}

/* ---- answers ---- */


/* Rebase every scheduling deadline after a backward adjustment, even after a restart.
 * Within one boot cooldown also follows elapsed uptime, so a forward jump cannot waive it. */
static void rebase_clock(void)
{
    int64_t now = time_now(), uptime = time_uptime_ms();
    uint32_t boot = time_boot_id();
    if (!now) { return; }
    int64_t elapsed = state.boot == boot ? (uptime - state.uptime) / 1000 : 0;
    int64_t delta = state.clock ? now - state.clock - elapsed : 0;
    if (state.boot != boot && delta > 0) { delta = 0; }
    int64_t *deadlines = state.deadlines;
    for (int32_t i = 0; i < 6; i++) { if (deadlines[i]) { deadlines[i] += delta; } }
    seen_at = state.revision == cache.revision ? state.seen : 0;
    if (delta || state.boot != boot) { state_dirty = true; }
    state.clock = now; state.uptime = uptime; state.boot = boot;
}

/* One enabled poll at a time. Queued work has no clock lease; the callback closes
 * its required header gate before another endpoint is enabled. Disabled polls fail
 * template expansion locally, including automatic reconnect and settings polls. */
static void fetch(void)
{
    rebase_clock();
    follow_year();
    persist();
    int64_t now = time_now(), wait = state.cooldown > state.next_send ? state.cooldown : state.next_send;
    if (state.active_poll) { return; }
    if (!now) { return; }
    if (wait > now) { return; }
    if (cache_dirty || state_dirty) { return; }
    int32_t kind = -1;
    for (int32_t i = 0; i < 3; i++) {
        int32_t candidate = (state.cursor + i) % 3;
        if (state.due[candidate] <= now) { kind = candidate; break; }
    }
    if (kind < 0) { return; }
    for (int32_t i = 0; i < POLLS; i++) { if (!close_gate(i)) { return; } }
    if (kind == POLL_YEAR && !cache.staging.year) {
        clear_record(&cache.staging, sizeof cache.staging);
        cache.staging.year = cache.year;
        cache.staging.total = -1;
    }
    state.request = kind;
    state.cursor = (kind + 1) % 3;
    state.item = cache.staging.offset;
    state.year = cache.year;
    state.active_poll = kind + 1;
    state.next_send = now + REQUEST_GAP;
    if (!save_state()) { state.active_poll = 0; return; }
    bool ready = true;
    if (kind == POLL_YEAR) {
        ready = set_text("yr", number_text(state.year));
        const char *offset = number_text(state.item);
        ready = set_text("offset", offset) && ready;
    }
    const char *gate = GATES[state.active_poll - 1];
    if (!ready || !set_text(gate, "1") || poll_run(state.active_poll - 1) != HOST_OK) {
        close_gate(kind);
        state.active_poll = 0; state.due[kind] = now + HOUR; save_state();
    }

}

/* Retry-After permits either delta seconds or an RFC 7231 GMT date. */
static int64_t retry_deadline(void)
{
    int64_t now = time_now();
    int32_t info[3] = { 0 }; /* status, length, Retry-After: only this metadata prefix is needed */
    answer_info((host_answer_info_t *)info, sizeof info);
    if (info[2] > 0) { return now + info[2]; }
    char header[64] = { 0 };
    int32_t size = answer_header("Retry-After", 11, 0, header, sizeof header - 1);
    if (size > 0) {
        int32_t seconds = number_of(header);
        if (seconds >= 0) { return now + seconds; }
        if (size == 29 && is_same(header + 25, " GMT", 4)) {
            static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
            for (int32_t month = 1; month <= 12; month++) {
                if (!is_same(header + 8, MONTHS + 3 * (month - 1), 3)) { continue; }
                char iso[] = "0000-00-00T00:00:00Z";
                copy_record(iso, header + 12, 4);
                iso[5] = '0' + month / 10; iso[6] = '0' + month % 10;
                copy_record(iso + 8, header + 5, 2);
                copy_record(iso + 11, header + 17, 8);
                int64_t at = parse_moment(iso);
                if (at) { return at > now ? at : now; }
            }
        }
    }
    return now + RETRY_DEFAULT_S;
}

static void finish_answer(int32_t status)
{
    int64_t now = time_now();
    int32_t kind = state.request;
    state.next_send = now + REQUEST_GAP;
    if (status == HTTP_OK && !(kind == POLL_YEAR ? take_page() : take_launches(kind))) { status = HOST_FAILED_NO_ANSWER; }
    /* A closed private gate is not a setting the user must fix. */
    if (status != HOST_FAILED_NOT_READY) { state.status[kind] = status; }
    if (status != HTTP_OK) {
        state.due[kind] = now + HOUR;
        if (kind == POLL_YEAR) { clear_record(&cache.staging, sizeof cache.staging); cache_dirty = true; }
    } else {
        close_gate(kind);
        if (kind < POLL_YEAR) {
            state.due[kind] = now + (kind == POLL_UPCOMING ? 30 * MINUTE : 3 * HOUR);
            if (kind == POLL_UPCOMING) { state.seen = seen_at = now; state.revision = cache.revision; }
        } else if (cache.staging.year && !cache.pages_have) { state.status[kind] = 0; }
    }
    /* A failed binding remains ready for a host retry until another endpoint is chosen.
     * Backoff governs new requests, not acceptance of a complete response already coming. */
    state.active_poll = 0;
    state_dirty = true;
    persist();
    schedule_reminder();
    update_header();
    screen_redraw(HOST_REDRAW_NORMAL);
    fetch();
}

void app_on_picked(int32_t poll, int32_t status)
{
    rebase_clock();
    follow_year();
    if (status == HTTP_TOO_MANY) {
        int64_t until = retry_deadline();
        if (until > state.cooldown) { state.cooldown = until; }
        for (int32_t i = 0; i < POLLS; i++) { state.status[i] = status; }
        for (int32_t i = 0; i < POLLS; i++) { close_gate(i); }
        state_dirty = true;
        persist();
        if (poll + 1 != state.active_poll) { screen_redraw(HOST_REDRAW_NORMAL); return; }
    } else if (poll != state.request || (!state.active_poll && status != HTTP_OK)) { return; }
    finish_answer(status);
}

/* ---- the life of it ---- */

void app_on_load(void)
{
    cache_dirty = state_dirty = false;
    if (!app_state_load(STATE_KEY, &state, sizeof state, STATE_VERSION)) {
        clear_record(&state, sizeof state);
        state.version = STATE_VERSION;
    }
    if (state.boot != time_boot_id()) { state.active_poll = 0; state_dirty = true; }
    /* Separate delivery receipt survives KV failure after a successful post. */
    uint32_t receipt[2];
    if (file_read("told", 4, 0, receipt, sizeof receipt) == sizeof receipt && receipt[0] && receipt[1] == ~receipt[0] && !was_told(receipt[0])) {
        state.told[state.told_next] = receipt[0];
        state.told_next = (state.told_next + 1) % TOLD_MAX;
        state_dirty = true;
    }
    clear_record(&cache, sizeof cache);
    cache.version = CACHE_VERSION;
    cache_generation = 0;
    cache_bank = 1;
    bool have = false;
    for (int32_t i = 0; i < 2; i++) {
        if (file_read(BANKS[i], 7, 0, &bank_buffer, sizeof bank_buffer) == sizeof bank_buffer && valid_bank(&bank_buffer) &&
            (!have || (int32_t)(bank_buffer.generation - cache_generation) > 0)) {
            copy_record(&cache, &bank_buffer.data, sizeof cache);
            cache_generation = bank_buffer.generation;
            cache_bank = i;
            have = true;
        }
    }
    seen_at = state.revision == cache.revision ? state.seen : 0;
    if (state.cache_pending) {
        state.due[state.request < POLL_YEAR ? state.request : POLL_YEAR] = 0;

        state.cache_pending = 0;
        state.status[state.request] = HOST_FAILED_NO_ANSWER;
        state_dirty = true;
    }
    rebase_clock();
    follow_year();
    persist();
    if (alarm_set_every(FETCH_ALARM, REQUEST_GAP, REQUEST_GAP) != HOST_OK) { log_write(HOST_LOG_ERROR, "Fetch alarm unavailable", 23); }
}

void app_on_intent(int32_t intent)
{
    (void)intent;
    fetch();
    schedule_reminder();
}

void app_on_installed(void) { fetch(); schedule_reminder(); }
void app_on_updated(void) { fetch(); schedule_reminder(); }
void app_on_settings(void) { fetch(); schedule_reminder(); }

void app_on_terminate(void)
{
    persist();
}

/* ---- where the user is ---- */

/* Where the user is, kept in the session while the application waits in the stack. */
typedef struct {
    struct { int32_t screen, depth; } nav;
    uint32_t  selected_id;    /* the launch chosen in the list; 0: the next one */
    int32_t   first;          /* the list's top row */
    int32_t   chosen_year[2]; /* of Months and History; 0: the latest */
} place_t;
APP_SESSION_FITS(place_t);
static place_t place;
static int32_t selected; /* the row of the chosen launch */

/* The row of the next launch: the first that has no result yet. */
static int32_t next_row(void)
{
    for (int32_t row = 0; row < row_count; row++) {
        if (!is_done(rows[row])) { return row; }
    }
    return row_count - 1;
}

/* The rows as they are now, the chosen launch wherever it went. */
__attribute__((noinline)) static void fit_rows(void)
{
    build_rows();
    selected = -1;
    for (int32_t row = 0; row < row_count; row++) {
        if (rows[row]->id == place.selected_id) { selected = row; }
    }
    if (selected < 0) {
        selected = next_row();
        place.first = selected;
    }
    if (place.first > row_count - ROWS) { place.first = row_count - ROWS; }
    if (place.first > selected) { place.first = selected; }
    if (place.first < selected - ROWS + 1) { place.first = selected - ROWS + 1; }
    if (place.first < 0) { place.first = 0; }
}

void app_on_blur(void)
{
    app_session_save(&place, sizeof place);
}

void app_on_focus(void)
{
    if (!app_session_load(&place, sizeof place)) { clear_record(&place, sizeof place); }
    fetch();
    schedule_reminder();
    if ((uint32_t)place.nav.screen >= SCREENS) { place.nav.screen = place.nav.depth = 0; }
    fit_rows();
    update_header();
}

/* The year a chart shows: what UP and DOWN chose, within the years there are. */
static int32_t year_shown(int32_t chart)
{
    int32_t latest = latest_year();
    if (chart && latest >= FIRST_YEAR + HOST_CHART_VALUES_MAX) { latest = FIRST_YEAR + HOST_CHART_VALUES_MAX - 1; }
    int32_t year = place.chosen_year[chart] != 0 ? place.chosen_year[chart] : latest;
    return year < FIRST_YEAR ? FIRST_YEAR : year > latest ? latest : year;
}

void app_on_button(int32_t event)
{
    if (!place.nav.depth && (event == BTN_UP_LONG_PRESSED || event == BTN_DOWN_LONG_PRESSED)) {
        place.nav.screen = (place.nav.screen + (event == BTN_UP_LONG_PRESSED ? SCREENS - 1 : 1)) % SCREENS;
        screen_redraw(HOST_REDRAW_SOON);
        return;
    }
    int32_t by = event == BTN_UP_PRESSED ? -1 : event == BTN_DOWN_PRESSED ? 1 : 0;
    if (place.nav.screen == SCREEN_LAUNCHES) {
        fit_rows();
        if (event == BTN_OK_PRESSED && !place.nav.depth && row_count > 0) {
            place.selected_id = rows[selected]->id; /* the page stays with it while the list changes */
            place.nav.depth = 1;
            screen_redraw(HOST_REDRAW_NORMAL);
        } else if (by != 0 && selected + by >= 0 && selected + by < row_count) {
            place.selected_id = rows[selected + by]->id;
            fit_rows();
            screen_redraw(HOST_REDRAW_NO_BLINK);
        }
    } else if (by != 0 && place.nav.screen != SCREEN_COUNTRIES) {
        int32_t chart = place.nav.screen == SCREEN_HISTORY, year = year_shown(chart) + by;
        if (year >= FIRST_YEAR && year <= latest_year() &&
            (!chart || year < FIRST_YEAR + HOST_CHART_VALUES_MAX)) {
            place.chosen_year[chart] = year;
            screen_redraw(HOST_REDRAW_NO_BLINK);
        }
    }
}

void app_on_back(void)
{
    if (!place.nav.depth) {
        screen_leave();
    } else {
        place.nav.depth = 0;
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

/* ---- words ---- */

static int32_t fill(char *out, int32_t size, int32_t pattern, const host_text_argument_t *args, int32_t count)
{
    return format_text(out, size, word(pattern), args, count);
}

/* The header says when the list was taken once it is old: what is shown stays meanwhile. */
static void update_header(void)
{
    char line[48] = "";
    if (seen_at != 0 && time_now() - seen_at > STALE_S) {
        char when[32];
        app_time_text(seen_at, HOST_TIME_NEAR, when, sizeof when);
        host_text_argument_t arg = { .text = when, .length = app_text_length(when) };
        fill(line, sizeof line, STR_UPDATED, &arg, 1);
    }
    header_set_text(HOST_HEADER_LEFT, line, app_text_length(line));
}

__attribute__((noinline)) static int32_t status_word(const launch_t *launch, int64_t now)
{
    static const uint8_t WORDS[] = { STR_STATUS_UNKNOWN, STR_STATUS_GO, STR_STATUS_TBD, STR_STATUS_SUCCESS,
                                     STR_STATUS_FAILURE, STR_STATUS_HOLD, STR_STATUS_FLIGHT, STR_STATUS_PARTIAL,
                                     STR_STATUS_TBC, STR_STATUS_DEPLOYED };
    if (launch->precision <= PRECISION_HOUR && (launch->status == ST_GO || launch->status == ST_TBC) && launch->net <= now) { return STR_AWAITING; }
    return WORDS[launch->status < sizeof WORDS ? launch->status : 0];
}

/* Word number `index` of a list of words separated by spaces: a month's name. */
static int32_t nth_word(int32_t list, int32_t index, char *out, int32_t size)
{
    const char *text = word(list);
    int32_t length = 0;
    while (index > 0 && *text) { index -= *text++ == ' '; }
    while (text[length] != ' ' && text[length] != '\0' && length < size - 1) {
        out[length] = text[length];
        length++;
    }
    out[length] = '\0';
    return length;
}

/* When a launch is, as exactly as it is known: "14:09" today, "Tmrw", "24 Nov", "Dec 2026",
 * "2027". */
static void say_day(const launch_t *launch, int32_t style, char *out, int32_t size)
{
    host_local_time_t then, today;
    out[0] = '\0';
    if (launch->precision == PRECISION_UNKNOWN) {
        copy_text(out, size, word(STR_TIME_UNKNOWN));
        return;
    }
    if (launch->precision > PRECISION_HOUR) {
        /* Coarse NETs name a supplied UTC calendar period, not a local instant. */
        app_date_from_days((int32_t)(launch->net / DAY), &then.year, &then.month, &then.day);
        char month[16];
        nth_word(STR_MONTHS, then.month - 1, month, sizeof month);
        host_text_argument_t args[3] = { { .number = then.day },
            { .text = month, .length = app_text_length(month) }, { .number = then.year } };
        format_text(out, size, launch->precision < PRECISION_MONTH ? "{0} {1} {2}" :
            launch->precision == PRECISION_MONTH ? "{1} {2}" : "{2}", args, 3);
        return;
    }
    if (time_local_at(launch->net, &then) != HOST_OK) { return; }
    if (style == HOST_TIME_NEAR_DAY && time_local_at(time_now(), &today) == HOST_OK &&
        then.year == today.year && then.month == today.month && then.day == today.day) {
        style = HOST_TIME_CLOCK;
    }
    if (style == HOST_TIME_CLOCK && launch->precision == PRECISION_HOUR) {
        out[0] = '0' + then.hour / 10;
        out[1] = '0' + then.hour % 10;
        copy_text(out + 2, size - 2, ":--");
    } else { app_time_text(launch->net, style, out, size); }
}

/* "in 17 h" while the time is known and the list fresh; below an hour, "Within 1 h". */
static void say_in(const launch_t *launch, int64_t now, char *out, int32_t size)
{
    int64_t seconds = launch->net - now;
    out[0] = '\0';
    if (seconds <= 0 || now == 0 || now - seen_at > COUNTDOWN_S ||
        (launch->precision > PRECISION_HOUR)) {
        return;
    }
    if (seconds < HOUR) {
        copy_text(out, size, word(STR_WITHIN_HOUR));
    } else if (seconds < 2 * DAY) {
        fill_number(out, size, STR_IN_HOURS, (int32_t)((seconds + HOUR / 2) / HOUR));
    } else {
        fill_number(out, size, STR_IN_DAYS, (int32_t)((seconds + DAY / 2) / DAY));
    }
}

/* ---- drawing ---- */

#define MARGIN HOST_UI_MARGIN
/* The kit's list row and scroll bar, repeated for two-line rows: not in the contract. */
#define KIT_BAR_INSET   8
#define KIT_SCROLL_ROOM 12

/* The accepted counter shares the text baseline; no screen title is drawn. */
static int32_t screen_top(void)
{
    char counter[] = "1/4";
    counter[0] += place.nav.screen;
    int32_t right = gfx_width() - MARGIN;
    int32_t width = gfx_text_width(HOST_FONT_SMALL, counter, 3);
    int32_t top = MARGIN + gfx_font_ascent(HOST_FONT_TEXT) - gfx_font_ascent(HOST_FONT_SMALL);
    ui_draw_text(right - width, top, HOST_UI_CAPTION, counter, 3, width);
    return MARGIN + ui_text_height(HOST_UI_TEXT);
}

static int32_t top_of(int32_t font, int32_t baseline)
{
    return baseline - gfx_font_ascent(font);
}

/* Draws `text` cut with "..." to `width`. */
__attribute__((noinline)) static void draw_fitted(int32_t x, int32_t y, int32_t font, const char *text, int32_t width)
{
    ui_draw_text(x, y, font == HOST_FONT_SMALL ? HOST_UI_CAPTION : HOST_UI_TEXT,
                 text, app_text_length(text), width);
}

/* "Ariane 64 Block 2   Go" */
/* Reuse the host formatter for the caption and detail string joins. */
__attribute__((noinline)) static void join_text(char *out, int32_t size, const char *first,
                                               const char *second, const char *separator)
{
    host_text_argument_t args[3] = { { .text = first, .length = app_text_length(first) },
        { .text = first[0] && second[0] ? separator : "", .length = app_text_length(separator) },
        { .text = second, .length = app_text_length(second) } };
    if (!first[0] || !second[0]) { args[1].length = 0; }
    format_text(out, size, "{0}{1}{2}", args, 3);
}

static void draw_row(int32_t row, int32_t top, int32_t right, int64_t now)
{
    const launch_t *launch = rows[row];
    bool is_selected = row == selected;
    int32_t baseline = top + 7 + gfx_font_cap_height(HOST_FONT_TEXT);
    int32_t caption_baseline = baseline + 7 + gfx_font_cap_height(HOST_FONT_SMALL);
    if (is_selected) {
        gfx_draw_rect(KIT_BAR_INSET, top, right - 2 * KIT_BAR_INSET, caption_baseline + 7 - top, HOST_RECT_FILLED);
        gfx_set_color(HOST_PAPER);
    }
    char when[48];
    say_day(launch, HOST_TIME_NEAR_DAY, when, sizeof when);
    int32_t value_right = right - MARGIN;
    int32_t when_width = app_text_width(HOST_FONT_TEXT, when);
    app_draw_text(value_right - when_width, top_of(HOST_FONT_TEXT, baseline), HOST_FONT_TEXT, when);

    /* The mission above, the rocket and the status below. */
    char rocket[TEXT_MAX];
    const char *mission = split_name(launch, rocket);
    draw_fitted(MARGIN, top_of(HOST_FONT_TEXT, baseline), HOST_FONT_TEXT, mission,
                value_right - when_width - 14 - MARGIN);
    char caption[96];
    join_text(caption, sizeof caption, rocket, word(status_word(launch, now)), "   ");
    draw_fitted(MARGIN, top_of(HOST_FONT_SMALL, caption_baseline), HOST_FONT_SMALL, caption, value_right - MARGIN);
    gfx_set_color(HOST_INK);
}

static void draw_launches(int64_t now)
{
    int32_t top = screen_top() + 2;
    int32_t pitch = (gfx_height() - top) / ROWS;
    int32_t right = gfx_width() - (row_count > ROWS ? KIT_SCROLL_ROOM : 0);
    for (int32_t row = place.first; row < place.first + ROWS && row < row_count; row++) {
        draw_row(row, top + (row - place.first) * pitch, right, now);
    }
    if (row_count > ROWS) {
        int32_t track_top = top + 4, track = ROWS * pitch - 12;
        int32_t thumb = track * ROWS / row_count;
        int32_t thumb_top = (track - thumb) * place.first / (row_count - ROWS);
        int32_t left = gfx_width() - 4 - 5;
        gfx_draw_rect(left + 2, track_top, 1, track, HOST_RECT_FILLED);
        gfx_draw_rect(left, track_top + thumb_top, 5, thumb, HOST_RECT_FILLED);
    }
}

__attribute__((noinline)) static int32_t caption_line(int32_t top, const char *text)
{
    ui_draw_text(MARGIN, top, HOST_UI_CAPTION, text, app_text_length(text), gfx_width() - 2 * MARGIN);
    return top + ui_text_height(HOST_UI_CAPTION);
}

__attribute__((noinline)) static void draw_launch(const launch_t *launch, int64_t now)
{
    int32_t width = gfx_width() - 2 * MARGIN;
    char rocket[TEXT_MAX], line[128];
    const char *mission = split_name(launch, rocket);
    int32_t length = app_text_length(mission), split = -1;
    /* Keep a fitting parenthetical suffix together when a mission needs two lines.
     * This also works in the proportional Jersey faces. */
    for (int32_t i = 1; i < length; i++) {
        if (mission[i] == '(' && mission[i - 1] == ' ' &&
            app_text_width(HOST_FONT_TEXT, mission) > width &&
            app_text_width(HOST_FONT_TEXT, mission + i) <= width) { split = i; break; }
    }
    int32_t lines;
    if (split > 0 && gfx_text_width(HOST_FONT_TEXT, mission, split - 1) <= width) {
        ui_draw_text(MARGIN, 10, HOST_UI_TEXT, mission, split - 1, width);
        ui_draw_text(MARGIN, 10 + ui_text_height(HOST_UI_TEXT), HOST_UI_TEXT, mission + split, length - split, width);
        lines = 2;
    } else { lines = ui_draw_text_lines(MARGIN, 10, HOST_UI_TEXT, mission, length, width, 2); }
    int32_t top = 12 + lines * ui_text_height(HOST_UI_TEXT);

    /* The rocket, the day, and what is uncertain about the time. */
    bool has_time = launch->precision <= PRECISION_HOUR;
    int32_t note = launch->precision == PRECISION_UNKNOWN ? -1
                   : !has_time                          ? STR_TIME_UNKNOWN
                   : launch->precision == PRECISION_HOUR ? STR_TIME_UNCONFIRMED
                   : launch->slipped_at != 0 && now - launch->slipped_at < SLIP_SHOWN_S && !is_done(launch)
                       ? STR_SCHEDULE_CHANGED
                       : -1;
    char day[48];
    say_day(launch, HOST_TIME_DATE, day, sizeof day);
    join_text(line, sizeof line, rocket, day, "   ");
    if (note >= 0) {
        char dated[128];
        copy_text(dated, sizeof dated, line);
        join_text(line, sizeof line, dated, word(note), "   ");
    }
    top = caption_line(top, line) + 10;

    /* The local time large, when it is known to the hour; less large under a long mission. */
    int32_t big = (lines > 1 ? HOST_FONT_LARGE : HOST_FONT_DISPLAY) | HOST_FONT_TABULAR;
    if (has_time) {
        say_day(launch, HOST_TIME_CLOCK, line, sizeof line);
        app_draw_text(MARGIN, top - (gfx_font_ascent(big) - gfx_font_cap_height(big)), big, line);
        top += gfx_font_cap_height(big) + 14;
    }

    /* The status, and how soon. */
    char soon[48];
    say_in(launch, now, soon, sizeof soon);
    join_text(line, sizeof line, word(status_word(launch, now)), soon, ", ");
    int32_t text_inset = gfx_font_ascent(HOST_FONT_TEXT) - gfx_font_cap_height(HOST_FONT_TEXT);
    ui_draw_text(MARGIN, top - text_inset, HOST_UI_TEXT, line, app_text_length(line), width);

    /* Who launches, and from where. */
    int32_t bottom = gfx_height() - HOST_UI_ACTIONS_HEIGHT - 6 - 2 * ui_text_height(HOST_UI_CAPTION);
    caption_line(caption_line(bottom, launch->provider), launch->site);
    app_draw_actions(NULL, word(STR_BACK));
}

/* Nothing to show yet: why. */
static void draw_empty(int32_t status)
{
    host_notice_t notice;
    clear_record(&notice, sizeof notice);
    notice.icon = HOST_ICON_INFO;
    if (status == HTTP_TOO_MANY) {
        notice.icon = HOST_ICON_HOURGLASS;
        copy_text(notice.first, sizeof notice.first, word(STR_PLEASE_WAIT));
        copy_text(notice.detail, sizeof notice.detail, word(STR_RETRY_LATER));
        ui_draw_notice(&notice);
    } else if (status == HTTP_OK) {
        copy_text(notice.first, sizeof notice.first, word(STR_NO_LAUNCHES));
        ui_draw_notice(&notice);
    } else {
        /* The first paced request already knows the network is unavailable. Infer this
         * only while waiting for data, so a successful retry restores the normal view. */
        if (!status && state.status[POLL_UPCOMING] == HOST_FAILED_NO_NETWORK) { status = HOST_FAILED_NO_NETWORK; }
        ui_draw_reason(status);
    }
}

__attribute__((noinline)) static void draw_countries(void)
{
    static const uint8_t NAMES[] = { STR_USA, STR_CHINA, STR_RUSSIA, STR_EUROPE, STR_INDIA, STR_JAPAN, STR_OTHERS };
    if (!cache.countries_have) { draw_empty(state.status[POLL_YEAR]); return; }
    int32_t order[COUNTRIES + 1] = { 0, 1, 2, 3, 4, 5, 6 };
    int32_t counts[COUNTRIES + 1], sum = 0;
    for (int32_t i = 0; i < COUNTRIES; i++) { counts[i] = cache.countries[i]; sum += counts[i]; }
    counts[COUNTRIES] = cache.total - sum;
    for (int32_t i = 1; i < COUNTRIES; i++) {
        int32_t at = i;
        while (at > 0 && counts[order[at - 1]] < counts[i]) {
            order[at] = order[at - 1]; at--;
        }
        order[at] = i;
    }
    int32_t top = screen_top() + 4;
    int32_t pitch = (gfx_height() - top - 4) / (COUNTRIES + 1);
    int32_t name_width = 0, number_width = app_text_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, "000");
    for (int32_t i = 0; i <= COUNTRIES; i++) {
        int32_t w = app_text_width(HOST_FONT_TEXT, word(NAMES[i]));
        if (w > name_width) { name_width = w; }
    }
    int32_t bar_left = MARGIN + name_width + 12;
    int32_t bar_width = gfx_width() - MARGIN - number_width - 12 - bar_left;
    int32_t cap = gfx_font_cap_height(HOST_FONT_TEXT), ascent = gfx_font_ascent(HOST_FONT_TEXT);
    int32_t leader = counts[order[0]] > 0 ? counts[order[0]] : 1;
    for (int32_t i = 0; i <= COUNTRIES; i++) {
        int32_t middle = top + i * pitch + pitch / 2;
        int32_t text_y = middle - cap / 2 - (ascent - cap);
        app_draw_text(MARGIN, text_y, HOST_FONT_TEXT, word(NAMES[order[i]]));
        ui_draw_bar(bar_left, middle - 7, bar_width, counts[order[i]], leader);
        app_draw_right(gfx_width() - MARGIN, text_y, HOST_FONT_TEXT | HOST_FONT_TABULAR, number_text(counts[order[i]]));
    }
}

static int32_t small_inset(void)
{
    return gfx_font_ascent(HOST_FONT_SMALL) - gfx_font_cap_height(HOST_FONT_SMALL);
}

/* The caption line at the bottom of a chart's screen, centred. */
static int32_t bottom_line_top(void)
{
    return gfx_height() - 10 - gfx_font_ascent(HOST_FONT_SMALL);
}

/* Both charts use the same frame, with room for the accepted month values and year ticks. */
__attribute__((noinline)) static int32_t chart_frame(host_chart_t *chart, int32_t count, bool history_chart)
{
    int32_t cap = gfx_font_cap_height(HOST_FONT_SMALL);
    int32_t caption = bottom_line_top(), labels = caption - 12 - cap;
    chart->x = MARGIN;
    chart->y = screen_top() + 8 + (history_chart ? 2 : cap + 6);
    int32_t year = cache.last_year;
    if (year >= FIRST_YEAR + history_count &&
        (history_chart ? year < FIRST_YEAR + count : year == year_shown(0))) {
        char note[64];
        fill_number(note, sizeof note, STR_INCOMPLETE, year);
        caption_line(MARGIN, note);
    }
    chart->width = gfx_width() - 2 * MARGIN;
    chart->height = labels - (history_chart ? 10 : 8) - chart->y;
    chart->kind = history_chart ? HOST_CHART_AREA : HOST_CHART_BARS;
    chart->flags = HOST_CHART_AXIS | HOST_CHART_FROM_ZERO | (history_chart ? HOST_CHART_RANGE_RIGHT : 0);
    chart->decimals = 0;
    chart->count = count;
    return caption;
}

static void draw_months(int64_t now)
{
    int32_t number = year_shown(0);
    year_t year;
    if (!year_of(number, &year)) {
        draw_empty(state.status[POLL_YEAR]);
        return;
    }
    int32_t small_cap = gfx_font_cap_height(HOST_FONT_SMALL), inset = small_inset();
    host_chart_t chart;
    int32_t totals_top = chart_frame(&chart, 12, false);
    int32_t labels_top = totals_top - 12 - small_cap;
    int32_t values[12], high = 1;
    /* The months of this year still to come are gaps, not zeros. */
    host_local_time_t today;
    int32_t last_month = 12;
    if (number == cache.year && now != 0) {
        int32_t day;
        app_date_from_days((int32_t)(now / DAY), &today.year, &last_month, &day);
    }
    for (int32_t i = 0; i < 12; i++) {
        values[i] = i < last_month ? year.months[i] : HOST_CHART_NO_VALUE;
        if (i < last_month && year.months[i] > high) { high = year.months[i]; }
    }
    ui_draw_chart(&chart, values);
    /* The bar geometry repeated from the kit: slot i spans x + i*w/n .. x + (i+1)*w/n less a
     * 2-pixel gap; rows = height - 1; value v at y + rows - 1 - v*(rows-1)/max. */
    int32_t rows_high = chart.height - 1;
    char label[16];
    /* Each translation names its widest abbreviation in the supported font families. */
    bool numbers = app_text_width(HOST_FONT_SMALL, word(STR_MONTH_WIDTH)) > chart.width / 12 - 1;
    for (int32_t i = 0; i < 12; i++) {
        int32_t from = chart.x + i * chart.width / 12, to = chart.x + (i + 1) * chart.width / 12;
        int32_t centre = from + (to - from - 2) / 2;
        nth_word(STR_MONTHS, i, label, sizeof label);
        app_draw_centered(centre, labels_top - inset, HOST_FONT_SMALL, numbers ? number_text(i + 1) : label);
        if (values[i] == HOST_CHART_NO_VALUE) { continue; }
        int32_t bar_top = chart.y + rows_high - 1 - values[i] * (rows_high - 1) / high;
        app_draw_centered(centre, bar_top - 5 - small_cap - inset, HOST_FONT_SMALL, number_text(values[i]));
    }
    char line[96];
    int32_t total = year_total(&year);
    host_text_argument_t args[4] = { { .number = number }, { .number = total },
                                     { .number = total - year.failures }, { .number = year.failures } };
    fill(line, sizeof line, STR_YEAR_LINE, args, 4);
    app_draw_centered(gfx_width() / 2, totals_top, HOST_FONT_SMALL, line);
}

static void draw_history(void)
{
    int32_t latest = latest_year(), count = latest - FIRST_YEAR + 1, values[HOST_CHART_VALUES_MAX], high = 0;
    if (count > HOST_CHART_VALUES_MAX) { count = HOST_CHART_VALUES_MAX; }
    latest = FIRST_YEAR + count - 1;
    for (int32_t i = 0; i < count; i++) {
        year_t year;
        values[i] = year_of(FIRST_YEAR + i, &year) ? year_total(&year) : HOST_CHART_NO_VALUE;
        if (values[i] > high) { high = values[i]; }
    }
    if (high == 0) {
        draw_empty(state.status[POLL_YEAR]);
        return;
    }
    int32_t inset = small_inset();
    host_chart_t chart;
    int32_t caption_top = chart_frame(&chart, count, true);
    int32_t years_top = caption_top - 12 - gfx_font_cap_height(HOST_FONT_SMALL);
    ui_draw_chart(&chart, values);
    /* x of point i, repeated from the kit: the range column (the widest caption + 6) comes off
     * the width; x = left + i*(width-2)/(count-1). */
    int32_t plot = chart.width - app_text_width(HOST_FONT_SMALL, number_text(high)) - 6; /* the least has fewer digits */
    int32_t marks[] = { 1957, 1970, 1990, 2010, latest };
    for (int32_t m = 0; m < 5; m++) {
        int32_t x = chart.x + (marks[m] - FIRST_YEAR) * (plot - 2) / (count - 1) + 1;
        const char *number = number_text(marks[m]);
        int32_t w = app_text_width(HOST_FONT_SMALL, number), left = x - w / 2;
        if (left < chart.x) { left = chart.x; }
        if (left + w > chart.x + plot) { left = chart.x + plot - w; }
        gfx_draw_rect(x, years_top - 8, 1, 4, HOST_RECT_FILLED);
        app_draw_text(left, years_top - inset, HOST_FONT_SMALL, number);
    }
    /* The chosen year: a dotted line over the chart, its number below. */
    int32_t year = year_shown(1), x = chart.x + (year - FIRST_YEAR) * (plot - 2) / (count - 1) + 1;
    for (int32_t y = chart.y; y < chart.y + chart.height; y += 4) { gfx_draw_rect(x, y, 1, 2, HOST_RECT_FILLED); }
    char line[96];
    int32_t value = values[year - FIRST_YEAR];
    const char *no_data = word(STR_NO_DATA);
    host_text_argument_t args[2] = { { .number = year },
        value == HOST_CHART_NO_VALUE ? (host_text_argument_t){ .text = no_data, .length = app_text_length(no_data) } :
                                      (host_text_argument_t){ .number = value } };
    fill(line, sizeof line, STR_HISTORY_LINE, args, 2);
    app_draw_centered(gfx_width() / 2, caption_top, HOST_FONT_SMALL, line);
}

void app_on_draw(void)
{
    int64_t now = time_now();
    fit_rows();
    if (place.nav.screen == SCREEN_LAUNCHES) {
        if (row_count == 0) {
            draw_empty(state.status[POLL_UPCOMING]);
        } else if (place.nav.depth) {
            draw_launch(rows[selected], now);
        } else {
            draw_launches(now);
        }
    } else if (place.nav.screen == SCREEN_COUNTRIES) {
        draw_countries();
    } else if (place.nav.screen == SCREEN_MONTHS) {
        draw_months(now);
    } else {
        draw_history();
    }
}
