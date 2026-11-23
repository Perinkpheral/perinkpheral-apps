/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Space Launch: rocket launches from The Space Devs' Launch Library 2, and how many there
 * were. Four screens, switched with held UP and DOWN (sdk/lib/app_nav.h):
 *   Launches   the next launches and, scrolled up, the last ones with their results;
 *              OK opens a launch's page over the list, BACK closes it.
 *   Countries  this year's launches by the provider's country, bars against the leader.
 *   Months     a year's launches by month; UP and DOWN choose the year.
 *   History    orbital launches a year since 1957; UP and DOWN choose the year.
 * Requests are serialized by the application, with a persistent service-wide cooldown.
 * Lists refresh every half hour / three hours, complete annual statistics once a day.
 * Past years come with the package (files/history.bin, from GCAT).
 * A 429 keeps what is there and waits the time the server names. A reminder can come some
 * time before a launch that is Go, as a notification.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_days.h"
#include "app_draw.h"
#include "app_nav.h"
#include "app_state.h"
#include "app_text.h"
#include "json_pick.h"
#include "host_api.h"
#include "strings.h"

enum { POLL_UPCOMING, POLL_PREVIOUS, POLL_YEAR, POLL_COUNTRIES, POLLS };
/* The order of paths in the local streaming picker. */
enum { PICK_ID, PICK_NAME, PICK_NET, PICK_STATUS, PICK_PRECISION, PICK_PROVIDER, PICK_SITE, PICK_COUNT };
enum { YEAR_COUNT, YEAR_NET, YEAR_STATUS };
enum { SCREEN_LAUNCHES, SCREEN_COUNTRIES, SCREEN_MONTHS, SCREEN_HISTORY, SCREENS };
enum { VIEW_LAUNCH = 1 };
/* Launch Library 2 status ids. */
enum { ST_GO = 1, ST_TBD, ST_SUCCESS, ST_FAILURE, ST_HOLD, ST_FLIGHT, ST_PARTIAL, ST_TBC, ST_DEPLOYED };
/* Launch Library 2 NET precision ids: 0 second, 1 minute, 2 hour, 3..6 a part of a day to a
 * week, 7 month, 8..16 a quarter to a decade. */
#define PRECISION_HOUR    2
#define PRECISION_MONTH   7
#define PRECISION_UNKNOWN 255

#define LIST_MAX     10 /* launches a list request asks for */
#define TEXT_MAX     64 /* a picked value is at most 63 bytes */
#define PAGE_SIZE    100
#define COUNTRIES    6  /* USA, China, Russia, Europe, India, Japan; Others are the rest */
#define FIRST_YEAR   1957
#define ROWS         4
#define HTTP_OK      200
#define HTTP_TOO_MANY 429

#define MINUTE           60
#define HOUR             3600
#define DAY              86400
#define STALE_S          (40 * MINUTE) /* the list's age at which the header says when it was updated */
#define FRESH_S          (3 * HOUR)    /* reminders come only from a list this recent */
#define COUNTDOWN_S      DAY           /* "in 5 h" only from a list this recent */
#define LATE_S           (5 * MINUTE)  /* a reminder this late is not given */
#define SLIP_SHOWN_S     DAY           /* how long "Schedule changed" stays */
#define RETRY_DEFAULT_S  HOUR          /* a 429 without Retry-After */
#define REMIND_ALARM     0
#define FETCH_ALARM      1
#define SAVE_ALARM       2
#define PACE_ALARM       3
#define REQUEST_GAP      (5 * MINUTE)
#define REQUEST_LEASE    (10 * MINUTE)
#define TICK_TIMER       0
#define TICK_MS          (10 * MINUTE * 1000)
#define TOLD_MAX         16

__attribute__((noinline)) static const char *word(int32_t id)
{
    return app_string(id);
}

/* A number as text, in a buffer of its own until the next call. */
__attribute__((noinline)) static const char *number_text(int32_t number)
{
    static char text[12];
    app_text_t line;
    app_text_start(&line, text, sizeof text);
    app_text_add_number(&line, number);
    return text;
}

/* ---- what is kept ---- */

typedef struct {
    uint32_t id;               /* a hash of the launch's UUID */
    uint8_t  status, precision;
    uint16_t spare;            /* no padding: records are compared byte by byte */
    int64_t  net;              /* Unix seconds */
    int64_t  slipped_at;       /* when its time last moved; 0: never seen to */
    char     name[TEXT_MAX];   /* "Falcon 9 Block 5 | Starlink Group 10-5" */
    char     provider[TEXT_MAX];
    char     site[TEXT_MAX];
} launch_t;

/* A year by month: the layout of a record of files/history.bin. */
typedef struct {
    uint16_t months[12];
    uint16_t failures;
} year_t;

#define CACHE_VERSION 2
/* The staged annual refresh is persisted too: background deliveries use fresh instances.
 * Published statistics change only when every required page and country has arrived. */
typedef struct {
    int32_t year, total, offset, country;
    year_t sum;
    int32_t countries[COUNTRIES];
} annual_t;
typedef struct {
    int32_t version;
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
    int64_t seen; /* freshness travels with this bank, including after fallback */
} cache_t;
_Static_assert(sizeof(cache_t) <= 8 * 1024 - 16, "a bank fits in two file blocks");
_Static_assert(sizeof(year_t) == 26 && sizeof(launch_t) == 216, "records without padding");

typedef struct {
    uint32_t version, generation, checksum, spare;
    cache_t data;
} bank_t;
static const char *const BANKS[] = { "cache-a", "cache-b" };
/* Reuse the read/write scratch space for a one-time migration from the old file. */
typedef struct {
    int32_t version, count[2], spare;
    launch_t lists[2][LIST_MAX];
    int32_t year, total;
    uint32_t pages_have, countries_have;
    year_t pages[4];
    int32_t countries[COUNTRIES], last_year;
    year_t last;
} legacy_cache_t;
static union { bank_t bank; legacy_cache_t legacy; } bank_scratch;
#define bank_buffer bank_scratch.bank
static uint32_t cache_generation;
static int32_t cache_bank;

#define STATE_KEY "state"
#define STATE_VERSION 2
typedef struct {
    int32_t version;
    int32_t status[POLLS];
    int32_t told_next;
    uint32_t told[TOLD_MAX];
    /* A reservation means delivery may have happened. On recovery it remains told.
     * Only a definite notification_post failure releases it for another attempt. */
    uint32_t reservation;
    int64_t cooldown, next_send, due[3], lease;
    int32_t active_tag, request, item, year;
    int32_t cache_pending, completed_tag;
    int64_t completed_at, cooldown_uptime;
    uint32_t cooldown_boot;
} state_t;
_Static_assert(sizeof(state_t) <= HOST_KV_VALUE_MAX, "state fits one KV value");
static cache_t cache;
static state_t state;
static int64_t seen_at;
static bool cache_dirty, state_dirty;
static bool stream_open, request_pending;
static int32_t stream_status;

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

static uint32_t checksum(const bank_t *bank)
{
    const uint8_t *bytes = (const uint8_t *)bank;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < sizeof *bank; i++) {
        if (i < offsetof(bank_t, checksum) || i >= offsetof(bank_t, checksum) + sizeof bank->checksum) {
            hash = (hash ^ bytes[i]) * 16777619u;
        }
    }
    return hash;
}

static bool valid_bank(const bank_t *bank)
{
    return bank->version == CACHE_VERSION && bank->data.version == CACHE_VERSION &&
           bank->checksum == checksum(bank) && (uint32_t)bank->data.count[0] <= LIST_MAX &&
           (uint32_t)bank->data.count[1] <= LIST_MAX;
}

static void save_cache(void)
{
    int32_t target = 1 - cache_bank;
    bank_buffer = (bank_t){ .version = CACHE_VERSION, .generation = cache_generation + 1, .data = cache };
    bank_buffer.checksum = checksum(&bank_buffer);
    cache_dirty = file_write(BANKS[target], 7, 0, &bank_buffer, sizeof bank_buffer) != HOST_OK;
    /* Read back before advancing the generation: never overwrite the last valid bank. */
    if (!cache_dirty) {
        cache_dirty = file_read(BANKS[target], 7, 0, &bank_buffer, sizeof bank_buffer) != sizeof bank_buffer ||
                      !valid_bank(&bank_buffer);
    }
    if (cache_dirty) { alarm_set(SAVE_ALARM, MINUTE); }
    else {
        cache_bank = target;
        cache_generation = bank_buffer.generation;
        /* An earlier app version used one unprotected bank. Retire it only after a
         * verified snapshot exists, leaving room for the second checksummed bank. */
        if (file_size("cache", 5) > 0) { file_write("cache", 5, 0, NULL, 0); }
    }
}

static void persist(void)
{
    if (cache_dirty) { save_cache(); }
    if (state.cache_pending != cache_dirty) { state.cache_pending = cache_dirty; state_dirty = true; }
    if (state_dirty) { save_state(); }

}

/* Local picker adapters keep the validation code independent of transport. */
static json_pick_t picker;
static char picked_lists[HOST_LIST_BYTES];
static int32_t value_count(int32_t path) { return json_pick_count(&picker, path); }
static bool value_text(int32_t path, int32_t element, char *out, int32_t size)
{
    const char *value = json_pick_element(&picker, path, element);
    out[0] = '\0';
    if (value == NULL || app_text_length(value) >= size) { return false; }
    app_text_copy(out, size, value);
    return true;
}
static void fetch(void);

/* ---- time ---- */

static int32_t digits(const char *text, int32_t from, int32_t count)
{
    int32_t value = 0;
    for (int32_t i = from; i < from + count; i++) {
        if (text[i] < '0' || text[i] > '9') { return -1; }
        value = value * 10 + text[i] - '0';
    }
    return value;
}

/* A whole number as the library writes ids and counts; -1 for anything else. */
static int32_t number_of(const char *text)
{
    int32_t length = app_text_length(text);
    return length > 0 && length < 10 ? digits(text, 0, length) : -1;
}

/* Element `element` of a picked path as such a number. */
__attribute__((noinline)) static int32_t picked_number(int32_t path, int32_t element)
{
    char text[16];
    return value_text(path, element, text, sizeof text) ? number_of(text) : -1;
}

/* "2026-11-24T10:13:00Z" (or with ".123Z"); 0 when it is not such a moment. */
static int64_t parse_moment(const char *text)
{
    if (app_text_length(text) < 19) { return 0; }
    int32_t year = digits(text, 0, 4), month = digits(text, 5, 2), day = digits(text, 8, 2);
    int32_t hour = digits(text, 11, 2), minute = digits(text, 14, 2), second = digits(text, 17, 2);
    if (year < 1957 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        (text[19] != 'Z' && text[19] != '.')) { return 0; }
    int32_t end = 19;
    if (text[end] == '.') {
        end++;
        int32_t start = end;
        while (text[end] >= '0' && text[end] <= '9') { end++; }
        if (end == start) { return 0; }
    }
    if (text[end] != 'Z' || text[end + 1]) { return 0; }
    int32_t y, m, d;
    app_date_from_days(app_days_from_date(year, month, day), &y, &m, &d);
    if (y != year || m != month || d != day) { return 0; }
    return (int64_t)app_days_from_date(year, month, day) * DAY + hour * HOUR + minute * MINUTE + second;
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

static void sum_pages(year_t *year) { *year = cache.annual; }

static int32_t year_total(const year_t *year)
{
    int32_t total = 0;
    for (int32_t month = 0; month < 12; month++) { total += year->months[month]; }
    return total;
}

/* A year's launches by month, when they are known. */
__attribute__((noinline)) static bool year_of(int32_t number, year_t *year)
{
    read_history();
    if (number == cache.year && cache.pages_have) {
        sum_pages(year);
        return true;
    }
    if (number == cache.last_year && cache.last_year != 0) {
        *year = cache.last;
        return true;
    }
    if (number >= FIRST_YEAR && number < FIRST_YEAR + history_count) {
        *year = history[number - FIRST_YEAR];
        return true;
    }
    return false;
}

/* The latest year there is anything for: this one once the clock is known. */
static int32_t latest_year(void)
{
    read_history();
    int32_t latest = cache.year > 0 ? cache.year : FIRST_YEAR + history_count - 1;
    return latest < FIRST_YEAR ? FIRST_YEAR : latest;
}

/* At the turn of the year the pages start again: the year before is kept beside them, and the
 * annual refresh starts again for the new year. */
static void follow_year(void)
{
    int64_t now = time_now();
    if (now == 0) { return; }
    int32_t year = utc_year(now);
    if (cache.year != year) {
        if (cache.year == year - 1 && cache.pages_have) {
            cache.last_year = cache.year;
            sum_pages(&cache.last);
        }
        cache.year = year;
        cache.total = 0;
        cache.pages_have = cache.countries_have = 0;
        cache.staging = (annual_t){ 0 };
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
    while (*text) { hash = (hash ^ (uint8_t)*text++) * 16777619u; }
    return hash;
}

/* An optional column is used only when it has a value for every launch: a launch without one
 * would shift the rest onto the wrong launches. */
static void column(int32_t path, int32_t element, int32_t count, char *text)
{
    text[0] = '\0';
    if (value_count(path) == count) { value_text(path, element, text, TEXT_MAX); }
    if ((app_text_length(text) == 4 && is_same(text, "null", 4))) { text[0] = '\0'; }
}

static uint8_t number_column(int32_t path, int32_t element, int32_t count, uint8_t otherwise)
{
    int32_t number = value_count(path) == count ? picked_number(path, element) : -1;
    return number < 0 || number >= PRECISION_UNKNOWN ? otherwise : (uint8_t)number;
}

static bool uuid_valid(const char *text)
{
    if (app_text_length(text) != 36) { return false; }
    for (int32_t i = 0; i < 36; i++) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') { return false; } }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) { return false; }
    }
    return true;
}

static bool present_text(const char *text)
{
    if (!text[0] || (app_text_length(text) == 4 && is_same(text, "null", 4))) { return false; }
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
    app_text_copy(text + end, TEXT_MAX - end, "...");
}

static launch_t incoming[LIST_MAX];

/* A picked list of launches; false when the answer is not one. */
static bool take_launches(int32_t list)
{
    int32_t count = value_count(PICK_ID);
    if (count > LIST_MAX || value_count(PICK_NAME) != count || value_count(PICK_NET) != count ||
        (count == 0 && picked_number(PICK_COUNT, 0) < 0)) {
        return false;
    }
    int64_t now = time_now();
    for (int32_t i = 0; i < count; i++) {
        launch_t *launch = &incoming[i];
        char text[TEXT_MAX];
        *launch = (launch_t){ 0 };
        value_text(PICK_ID, i, text, sizeof text);
        if (!uuid_valid(text)) { return false; }
        launch->id = hash_of(text);
        for (int32_t j = 0; j < i; j++) { if (incoming[j].id == launch->id) { return false; } }
        value_text(PICK_NAME, i, launch->name, sizeof launch->name);
        if (!present_text(launch->name)) { return false; }
        mark_truncated(launch->name);
        value_text(PICK_NET, i, text, sizeof text);
        launch->net = parse_moment(text);
        if (launch->net == 0) { return false; }
        launch->status = number_column(PICK_STATUS, i, count, 0);
        launch->precision = number_column(PICK_PRECISION, i, count, PRECISION_UNKNOWN);
        if (launch->precision > 16) { launch->precision = PRECISION_UNKNOWN; }
        column(PICK_PROVIDER, i, count, launch->provider);
        column(PICK_SITE, i, count, launch->site);
        if (!present_text(launch->provider)) { app_text_copy(launch->provider, TEXT_MAX, "?"); }
        if (!present_text(launch->site)) { app_text_copy(launch->site, TEXT_MAX, "?"); }
        /* A launch whose time moved says so for a day. */
        const launch_t *before = find_launch(launch->id);
        if (before != NULL) {
            int64_t moved = launch->net - before->net;
            launch->slipped_at = moved >= MINUTE || moved <= -MINUTE ? now : before->slipped_at;
        }
    }
    if (cache.count[list] != count || !is_same(cache.lists[list], incoming, (uint32_t)(count * sizeof(launch_t)))) {
        cache.count[list] = count;
        for (int32_t i = 0; i < count; i++) { cache.lists[list][i] = incoming[i]; }
        cache_dirty = true;
    }
    return true;
}

/* Pages are folded into a private generation, without a fixed page ceiling. A changed
 * count, missing row or wrong year aborts the round; the last published year stays visible. */
static bool take_page(int32_t offset)
{
    annual_t *stage = &cache.staging;
    int32_t count = value_count(YEAR_NET), total = picked_number(YEAR_COUNT, 0);
    if (offset == -1) {
        /* Recheck the common total after the country round. A launch added during the
         * round invalidates it instead of becoming an incorrect Others remainder. */
        if (state.year != cache.year || stage->year != state.year || total != stage->total ||
            count != (total > 0 ? 1 : 0)) { return false; }
        if (count) {
            char text[TEXT_MAX];
            value_text(YEAR_NET, 0, text, sizeof text);
            if (!parse_moment(text) || utc_year(parse_moment(text)) != state.year) { return false; }
        }
        return true;
    }
    if (state.year != cache.year || stage->year != state.year || offset != stage->offset ||
        total < 0 || total > UINT16_MAX || value_count(YEAR_STATUS) != count ||
        (offset != 0 && stage->total != total) || offset > total ||
        count != (total - offset < PAGE_SIZE ? total - offset : PAGE_SIZE)) { return false; }
    year_t page = { 0 };
    for (int32_t i = 0; i < count; i++) {
        char text[TEXT_MAX];
        value_text(YEAR_NET, i, text, sizeof text);
        int64_t net = parse_moment(text);
        int32_t month = digits(text, 5, 2), status = picked_number(YEAR_STATUS, i);
        if (net == 0 || utc_year(net) != state.year ||
            (status != ST_SUCCESS && status != ST_FAILURE && status != ST_PARTIAL)) { return false; }
        page.months[month - 1]++;
        page.failures += status != ST_SUCCESS;
    }
    stage->total = total;
    for (int32_t m = 0; m < 12; m++) { stage->sum.months[m] += page.months[m]; }
    stage->sum.failures += page.failures;
    stage->offset += count;
    cache_dirty = true;
    return true;
}

static bool take_country(int32_t country)
{
    annual_t *stage = &cache.staging;
    int32_t total = picked_number(0, 0);
    if (state.year != cache.year || stage->year != state.year || country != stage->country ||
        country < 0 || country >= COUNTRIES || total < 0 || total > stage->total) { return false; }
    stage->countries[country] = total;
    stage->country++;
    cache_dirty = true;
    return true;
}

static bool publish_annual(void)
{
    annual_t *stage = &cache.staging;
    int32_t sum = 0;
    for (int32_t i = 0; i < COUNTRIES; i++) { sum += stage->countries[i]; }
    if (stage->offset != stage->total || stage->country != COUNTRIES || sum > stage->total) { return false; }
    cache.annual = stage->sum;
    cache.total = stage->total;
    for (int32_t i = 0; i < COUNTRIES; i++) { cache.countries[i] = stage->countries[i]; }
    cache.pages_have = 1;
    cache.countries_have = (1u << COUNTRIES) - 1;
    cache.staging = (annual_t){ 0 };
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
    app_text_copy(rocket, length + 1, name);
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
    if (id == PACE_ALARM) {
        /* This relative alarm is the pacing permit, independent of a clock adjustment.
         * The absolute service cooldown is still checked by fetch(). */
        if (!request_pending && state.status[state.request] != HTTP_OK) { state.next_send = time_now(); }
        fetch();
        return;
    }
    if (id == FETCH_ALARM) { fetch(); return; }
    if (id == SAVE_ALARM) { persist(); fetch(); return; }
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    for (int32_t i = 0; i < cache.count[0]; i++) {
        const launch_t *launch = &cache.lists[0][i];
        if (!needs_reminder(launch, now, lead) || launch->net - lead > now) { continue; }
        host_notice_t notice = { .icon = HOST_ICON_BELL };
        app_time_text(launch->net, HOST_TIME_CLOCK, notice.first, sizeof notice.first);
        char rocket[TEXT_MAX];
        app_text_copy(notice.detail, sizeof notice.detail, split_name(launch, rocket));
        int32_t slot = state.told_next;
        uint32_t before = state.told[slot];
        state.reservation = launch->id;
        state.told[slot] = launch->id;
        state.told_next = (slot + 1) % TOLD_MAX;
        if (!save_state()) {
            state.told[slot] = before;
            state.told_next = slot;
            state.reservation = 0;
            alarm_set(REMIND_ALARM, MINUTE);
            return;
        }
        if (notification_post(&notice) != HOST_OK) {
            state.told[slot] = before;
            state.told_next = slot;
            state.reservation = 0;
            save_state();
            alarm_set(REMIND_ALARM, MINUTE);
            return;
        }
        state.reservation = 0;
        save_state();
    }
    schedule_reminder();
}

/* ---- answers ---- */

static void update_header(void);

static const char *const LAUNCH_PATHS[] = {
    "results[*].id", "results[*].name", "results[*].net", "results[*].status.id",
    "results[*].net_precision.id", "results[*].launch_service_provider.name",
    "results[*].pad.location.name", "count"
};
static const char *const YEAR_PATHS[] = { "count", "results[*].net", "results[*].status.id" };
static const char *const COUNTRY_PATHS[] = { "count" };

static void wake_fetch(int64_t at)
{
    if (time_now() == 0 || at <= time_now()) { alarm_set(FETCH_ALARM, 1); }
    else if (at < HOST_TIME_MIN || at > HOST_TIME_MAX) { alarm_set(FETCH_ALARM, HOUR); }
    else { alarm_set_at(FETCH_ALARM, at); }
}

/* Persist the request identity before queuing. Only one request may be outstanding,
 * including across background unloads. The lease recovers interrupted raw streams. */
static void fetch(void)
{
    follow_year();
    persist();
    int64_t now = time_now();
    if (now == 0) { alarm_set(FETCH_ALARM, MINUTE); return; }
    /* The initial pair of lists can complete together. Annual work is paced; no batch
     * is queued ahead of an answer, so a 429 always closes the gate immediately. */
    bool initial_previous = !request_pending && cache.count[0] > 0 && state.request == POLL_UPCOMING &&
                            state.status[0] == HTTP_OK && state.due[1] <= now;
    int64_t pace = initial_previous ? now : state.next_send;
    int64_t cooldown = state.cooldown;
    if (state.cooldown_boot == time_boot_id() && state.cooldown_uptime > time_uptime_ms()) {
        int64_t until = now + (state.cooldown_uptime - time_uptime_ms() + 999) / 1000;
        if (until > cooldown) { cooldown = until; }
    }
    int64_t wait = cooldown > pace ? cooldown : pace;
    if (request_pending && state.lease > now) { if (state.lease > wait) { wait = state.lease; } }
    if (wait > now) { wake_fetch(wait); return; }
    if (cache_dirty || state_dirty) { alarm_set(FETCH_ALARM, MINUTE); return; }
    request_pending = false;
    int32_t kind = -1;
    for (int32_t i = 0; i < 3; i++) { if (state.due[i] <= now) { kind = i; break; } }
    if (kind < 0) {
        wait = state.due[0];
        for (int32_t i = 1; i < 3; i++) { if (state.due[i] < wait) { wait = state.due[i]; } }
        wake_fetch(wait);
        return;
    }
    if (kind == POLL_YEAR && cache.staging.year == 0) {
        cache.staging = (annual_t){ .year = cache.year, .total = -1 };
        cache_dirty = true;
        persist();
        if (cache_dirty) { alarm_set(FETCH_ALARM, MINUTE); return; }
    }
    state.request = kind;
    state.item = -1;
    state.year = cache.year;
    char url[512];
    app_text_t text;
    app_text_start(&text, url, sizeof url);
    app_text_add(&text, "https://ll.thespacedevs.com/2.3.0/launches/");
    if (kind == POLL_UPCOMING) {
        app_text_add(&text, "upcoming/?format=json&mode=normal&limit=10&ordering=net&hide_recent_previous=false");
    } else if (kind == POLL_PREVIOUS) {
        app_text_add(&text, "previous/?format=json&mode=normal&limit=10&ordering=-net");
    } else {
        annual_t *stage = &cache.staging;
        if (stage->country == COUNTRIES) {
            state.item = -1;
            app_text_add(&text, "previous/?offset=0&limit=1&format=json&mode=list&ordering=net&year=");
        } else if (stage->total >= 0 && stage->offset == stage->total) {
            state.request = POLL_COUNTRIES;
            state.item = stage->country;
            char providers[256];
            /* Hidden provider list is a single encoded setting item in the URL. */
            app_setting("lsp", providers, sizeof providers);
            const char *from = providers;
            for (int32_t i = 0; i < stage->country && *from; ) { if (*from++ == '\n') { i++; } }
            app_text_add(&text, "previous/?lsp__id=");
            for (; *from && *from != '\n'; from++) {
                if (*from == ',') { app_text_add(&text, "%2C"); }
                else if (*from >= '0' && *from <= '9') { char digit[2] = { *from, 0 }; app_text_add(&text, digit); }
                else { alarm_set(FETCH_ALARM, HOUR); return; }
            }
            app_text_add(&text, "&limit=1&format=json&mode=list&year=");
        } else {
            state.item = stage->offset;
            app_text_add(&text, "previous/?offset=");
            app_text_add_number(&text, stage->offset);
            app_text_add(&text, "&limit=100&format=json&mode=list&ordering=net&year=");
        }
        app_text_add_number(&text, state.year);
        app_text_add(&text, "&status__ids=3,4,7&include_suborbital=false");
    }
    /* A serialized request can reuse its binding within the year. Leases exceed the
     * host stream timeout; an abandoned response cannot overlap the next such binding. */
    state.active_tag = HOST_REQUEST_FIRST + ((state.year - FIRST_YEAR) % 30) * 8 + state.request;
    state.lease = now + REQUEST_LEASE;
    state.next_send = now + REQUEST_GAP;
    if (!save_state()) { request_pending = false; alarm_set(FETCH_ALARM, MINUTE); return; }
    static const char HEADERS[] = "Accept: application/json\nUser-Agent: perinkpheral-space-launch";
    request_pending = true;
    if (http_send(state.active_tag, HOST_HTTP_GET, url, app_text_length(url), HEADERS, sizeof HEADERS - 1, NULL, 0) != HOST_OK) {
        request_pending = false;
        save_state();
    }
    wake_fetch(state.lease);
}

/* Retry-After permits either delta seconds or an RFC 7231 GMT date. */
static int64_t retry_deadline(void)
{
    host_answer_info_t info = { 0 };
    answer_info(&info, sizeof info);
    if (info.retry_after > 0) { return time_now() + info.retry_after; }
    char header[64] = { 0 };
    int32_t size = answer_header("Retry-After", 11, 0, header, sizeof header - 1);
    if (size > 0) {
        int32_t seconds = number_of(header);
        if (seconds >= 0) { return time_now() + seconds; }
        if (size == 29 && app_text_equal(header + 25, " GMT") &&
            digits(header, 5, 2) > 0 && digits(header, 5, 2) <= 31 && digits(header, 12, 4) >= FIRST_YEAR) {
            static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
            for (int32_t month = 0; month < 12; month++) {
                if (!is_same(header + 8, MONTHS + 3 * month, 3)) { continue; }
                char iso[24];
                app_text_t text;
                app_text_start(&text, iso, sizeof iso);
                app_text_add_number(&text, digits(header, 12, 4)); app_text_add(&text, "-");
                app_text_add_padded(&text, month + 1, 2); app_text_add(&text, "-");
                app_text_add_padded(&text, digits(header, 5, 2), 2); app_text_add(&text, "T");
                for (int32_t i = 17; i < 25; i++) { char one[2] = { header[i], 0 }; app_text_add(&text, one); }
                app_text_add(&text, "Z");
                int64_t at = parse_moment(iso);
                if (at) { return at > time_now() ? at : time_now(); }
            }
        }
    }
    return time_now() + RETRY_DEFAULT_S;
}

static void finish_answer(int32_t status)
{
    int32_t request = state.request;
    if (status == HTTP_OK) {
        bool taken = request == POLL_YEAR ? take_page(state.item) :
                     request == POLL_COUNTRIES ? take_country(state.item) : take_launches(request);
        if (!taken) { status = HOST_FAILED_NO_ANSWER; }
    }
    if (request >= POLL_YEAR && state.year != cache.year && status != HTTP_TOO_MANY) {
        request_pending = false;
        state.completed_tag = state.active_tag;
        state.completed_at = time_now();
        save_state();
        wake_fetch(state.next_send);
        return;
    }
    if (status == HTTP_TOO_MANY) {
        int64_t until = retry_deadline();
        if (until > state.cooldown) { state.cooldown = until; }
        int64_t deadline = time_uptime_ms() + (state.cooldown > time_now() ? state.cooldown - time_now() : 0) * 1000;
        if (state.cooldown_boot != time_boot_id() || deadline > state.cooldown_uptime) {
            state.cooldown_uptime = deadline;
        }
        state.cooldown_boot = time_boot_id();
        /* No staged set survives a failed round, including a 429 in its countries. */
        cache.staging = (annual_t){ 0 };
        cache_dirty = true;
        for (int32_t i = 0; i < POLLS; i++) { state.status[i] = status; }
    } else {
        state.status[request] = status;
        if (status == HTTP_OK && request >= POLL_YEAR && !cache.pages_have && cache.staging.year) {
            state.status[request] = 0; /* a complete annual snapshot is still loading */
        }
        if (status < 0) {
            for (int32_t i = 0; i < POLLS; i++) { if (state.status[i] == 0) { state.status[i] = status; } }
        }
        if (request < POLL_YEAR) { state.due[request] = status == HTTP_OK ? time_now() + (request == 0 ? 30 * MINUTE : 3 * HOUR) : time_now(); }
        else if (status != HTTP_OK) {
            cache.staging = (annual_t){ 0 };
            cache_dirty = true;
            state.due[2] = time_now() + HOUR;
        } else if (request == POLL_YEAR && state.item == -1) {
            bool published = publish_annual();
            state.status[POLL_YEAR] = state.status[POLL_COUNTRIES] = published ? HTTP_OK : HOST_FAILED_NO_ANSWER;
            if (!published) { cache.staging = (annual_t){ 0 }; }
            state.due[2] = time_now() + (published ? DAY : HOUR);
        }
    }
    request_pending = false;
    state.completed_tag = state.active_tag;
    state.completed_at = time_now();
    state_dirty = true;
    if (request == POLL_UPCOMING && status == HTTP_OK && seen_at != time_now()) {
        seen_at = time_now();
        cache.seen = seen_at;
        cache_dirty = true;
    }
    persist();
    schedule_reminder();
    update_header();
    wake_fetch(state.cooldown > state.next_send ? state.cooldown : state.next_send);
    screen_redraw(HOST_REDRAW_NORMAL);
    if (request == POLL_UPCOMING && status == HTTP_OK) { fetch(); }
}

void app_on_data(int32_t tag, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    if (tag != state.active_tag || !state.active_tag) { return; }
    follow_year();
    if (flags & HOST_DATA_FIRST) {
        stream_open = true;
        stream_status = status;
        const char *const *paths = state.request == POLL_YEAR ? YEAR_PATHS :
                                  state.request == POLL_COUNTRIES ? COUNTRY_PATHS : LAUNCH_PATHS;
        json_pick_init(&picker, paths, state.request == POLL_YEAR ? 3 : state.request == POLL_COUNTRIES ? 1 : 8);
        json_pick_lists(&picker, picked_lists, sizeof picked_lists);
    }
    /* A 429's body can itself be interrupted: persist the cooldown on FIRST. */
    if ((flags & HOST_DATA_FIRST) && status == HTTP_TOO_MANY) {
        stream_open = false;
        finish_answer(status);
        return;
    }
    if (flags & HOST_DATA_FAILED) {
        stream_open = false;
        finish_answer(status);
        return;
    }
    if (!stream_open) { return; }
    json_pick_feed(&picker, (const char *)data, length);
    if (flags & HOST_DATA_LAST) {
        stream_open = false;
        status = stream_status;
        if (status == HTTP_OK && (!json_pick_finish(&picker) || json_pick_is_cut(&picker))) { status = HOST_FAILED_NO_ANSWER; }
        finish_answer(status);
    }
}

/* The old file cannot prove coherent annual statistics. Preserve its validated lists
 * and their freshness during upgrade; annual data is rebuilt through the staged path. */
static void migrate_cache(void)
{
    legacy_cache_t *old = &bank_scratch.legacy;
    if (file_read("cache", 5, 0, old, sizeof *old) != sizeof *old || old->version != 1 ||
        (uint32_t)old->count[0] > LIST_MAX || (uint32_t)old->count[1] > LIST_MAX) { return; }
    for (int32_t list = 0; list < 2; list++) {
        for (int32_t i = 0; i < old->count[list]; i++) {
            launch_t *launch = &old->lists[list][i];
            if (!launch->id || launch->net < HOST_TIME_MIN || launch->net > HOST_TIME_MAX ||
                launch->name[TEXT_MAX - 1] || launch->provider[TEXT_MAX - 1] || launch->site[TEXT_MAX - 1] ||
                !present_text(launch->name)) { return; }
        }
    }
    for (int32_t list = 0; list < 2; list++) {
        cache.count[list] = old->count[list];
        for (int32_t i = 0; i < old->count[list]; i++) {
            launch_t *launch = &cache.lists[list][i];
            *launch = old->lists[list][i];
            mark_truncated(launch->name);
            if (launch->precision > 16) { launch->precision = PRECISION_UNKNOWN; }
            if (!present_text(launch->provider)) { app_text_copy(launch->provider, TEXT_MAX, "?"); }
            if (!present_text(launch->site)) { app_text_copy(launch->site, TEXT_MAX, "?"); }
        }
    }
    if (kv_get("seen", 4, &cache.seen, sizeof cache.seen) != sizeof cache.seen) { cache.seen = 0; }
    cache_dirty = true;
}

/* ---- the life of it ---- */

void app_on_load(void)
{
    cache_dirty = state_dirty = stream_open = false;
    if (!app_state_load(STATE_KEY, &state, sizeof state, STATE_VERSION)) {
        state = (state_t){ .version = STATE_VERSION };
        typedef struct { int32_t version, status[4]; uint32_t postponed; int32_t next; uint32_t told[16]; } legacy_t;
        legacy_t old;
        if (kv_get(STATE_KEY, sizeof STATE_KEY - 1, &old, sizeof old) == sizeof old && old.version == 1) {
            state.told_next = old.next;
            for (int32_t i = 0; i < TOLD_MAX; i++) { state.told[i] = old.told[i]; }
            state_dirty = true;
        }
    }
    if ((uint32_t)state.told_next >= TOLD_MAX) { state.told_next = 0; }
    cache = (cache_t){ .version = CACHE_VERSION };
    cache_generation = 0;
    cache_bank = 1;
    bool have = false;
    for (int32_t i = 0; i < 2; i++) {
        if (file_read(BANKS[i], 7, 0, &bank_buffer, sizeof bank_buffer) == sizeof bank_buffer && valid_bank(&bank_buffer) &&
            (!have || (int32_t)(bank_buffer.generation - cache_generation) > 0)) {
            cache = bank_buffer.data;
            cache_generation = bank_buffer.generation;
            cache_bank = i;
            have = true;
        }
    }
    if (!have) { migrate_cache(); }
    seen_at = cache.seen;
    if (cache.staging.year) { state.due[2] = 0; state_dirty = true; }
    if (state.cache_pending) {
        state.due[state.request < POLL_YEAR ? state.request : POLL_YEAR] = 0;
        request_pending = false;
        state.lease = 0;
        state.cache_pending = 0;
        state.status[state.request] = HOST_FAILED_NO_ANSWER;
        state_dirty = true;
    }
    /* An interrupted reservation is deliberately consumed (at most once delivery). */
    if (state.reservation) { state.reservation = 0; state_dirty = true; }
    request_pending = state.active_tag && state.lease > time_now() &&
                      !(state.completed_tag == state.active_tag && state.completed_at >= state.lease - REQUEST_LEASE);
    follow_year();
    persist();
    if (alarm_set_every(PACE_ALARM, REQUEST_GAP, REQUEST_GAP) != HOST_OK) {
        log_write(HOST_LOG_ERROR, "Fetch alarm unavailable", 23);
    }
    wake_fetch(state.cooldown > time_now() ? state.cooldown : time_now() + 1);
}

void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_TIME_CHANGED && time_now() && state.cooldown_boot == time_boot_id() &&
        state.cooldown_uptime > time_uptime_ms()) {
        state.cooldown = time_now() + (state.cooldown_uptime - time_uptime_ms() + 999) / 1000;
        state_dirty = true;
    }
    if (intent == HOST_INTENT_STARTED) {
        request_pending = false; /* a restart cleared the host request queue */
        state_dirty = true;
    }
    fetch();
    schedule_reminder();
}

void app_on_installed(void) { fetch(); schedule_reminder(); }
void app_on_updated(void) { fetch(); schedule_reminder(); }
void app_on_settings(void)
{
    fetch();
    schedule_reminder();
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_terminate(void)
{
    if (stream_open) {
        /* The host drops an interrupted raw request when this instance is unloaded. */
        state.lease = 0;
        state_dirty = true;
        request_pending = false;
    }
    stream_open = false;
    /* Refused writes remain scheduled off screen. The persisted cache_pending marker
     * restores the affected deadline if this instance loses an uncommitted snapshot. */
    persist();
}

/* ---- where the user is ---- */

/* Where the user is, kept in the session while the application waits in the stack. */
typedef struct {
    app_nav_t nav;
    uint32_t  selected_id;    /* the launch chosen in the list; 0: the next one */
    int32_t   first;          /* the list's top row */
    int32_t   chosen_year[2]; /* of Months and History; 0: the latest */
} place_t;
APP_SESSION_FITS(place_t);
static place_t place = { .nav = { .screens = SCREENS } };
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

static void tick(void)
{
    timer_set(TICK_TIMER, TICK_MS);
}

void app_on_focus(void)
{
    if (!app_session_load(&place, sizeof place)) { place = (place_t){ .nav = { .screens = SCREENS } }; }
    fetch();
    schedule_reminder();
    app_nav_check(&place.nav, SCREENS);
    fit_rows();
    update_header();
    tick();
}

void app_on_timer(int32_t id)
{
    (void)id;
    fetch();
    update_header();
    screen_redraw(HOST_REDRAW_NORMAL);
    tick();
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
    if (app_nav_button(&place.nav, event) != APP_NAV_IGNORED) {
        screen_redraw(HOST_REDRAW_SOON);
        return;
    }
    int32_t by = event == BTN_UP_PRESSED ? -1 : event == BTN_DOWN_PRESSED ? 1 : 0;
    if (place.nav.screen == SCREEN_LAUNCHES) {
        fit_rows();
        if (event == BTN_OK_PRESSED && app_nav_view(&place.nav) == APP_NAV_ON_SCREEN && row_count > 0) {
            place.selected_id = rows[selected]->id; /* the page stays with it while the list changes */
            app_nav_open(&place.nav, VIEW_LAUNCH);
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
    if (app_nav_button(&place.nav, BTN_BACK_PRESSED) == APP_NAV_IGNORED) {
        screen_leave();
    } else {
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

/* ---- words ---- */

static int32_t fill(char *out, int32_t size, int32_t pattern, const host_text_argument_t *args, int32_t count)
{
    const char *text = word(pattern);
    int32_t length = text_fill(text, app_text_length(text), args, count, out, size - 1);
    out[length > 0 ? length : 0] = '\0';
    return length;
}

__attribute__((noinline)) static void fill_number(char *out, int32_t size, int32_t pattern, int32_t number)
{
    host_text_argument_t arg = { .number = number };
    fill(out, size, pattern, &arg, 1);
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

static int32_t status_word(const launch_t *launch, int64_t now)
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
    app_text_t text;
    out[0] = '\0';
    if (launch->precision == PRECISION_UNKNOWN) {
        app_text_copy(out, size, word(STR_TIME_UNKNOWN));
        return;
    }
    if (launch->precision > PRECISION_HOUR) {
        /* Coarse NETs name a supplied UTC calendar period, not a local instant. */
        app_date_from_days((int32_t)(launch->net / DAY), &then.year, &then.month, &then.day);
        app_text_start(&text, out, size);
        char month[16];
        nth_word(STR_MONTHS, then.month - 1, month, sizeof month);
        if (launch->precision < PRECISION_MONTH) {
            app_text_add_number(&text, then.day);
            app_text_add(&text, " ");
        }
        if (launch->precision <= PRECISION_MONTH) {
            app_text_add(&text, month);
            app_text_add(&text, " ");
        }
        app_text_add_number(&text, then.year);
        return;
    }
    if (time_local_at(launch->net, &then) != HOST_OK) { return; }
    if (style == HOST_TIME_NEAR_DAY && time_local(&today) == HOST_OK &&
        then.year == today.year && then.month == today.month && then.day == today.day) {
        style = HOST_TIME_CLOCK;
    }
    if (style == HOST_TIME_CLOCK && launch->precision == PRECISION_HOUR) {
        app_text_start(&text, out, size);
        app_text_add_padded(&text, then.hour, 2);
        app_text_add(&text, ":--");
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
        app_text_copy(out, size, word(STR_WITHIN_HOUR));
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

static int32_t top_of(int32_t font, int32_t baseline)
{
    return baseline - gfx_font_ascent(font);
}

/* Draws `text` cut with "..." to `width`. */
__attribute__((noinline)) static void draw_fitted(int32_t x, int32_t y, int32_t font, const char *text, int32_t width)
{
    char fitted[96];
    int32_t length = gfx_fit_text(font, text, app_text_length(text), width, fitted, sizeof fitted);
    gfx_draw_text(x, y, font, fitted, length > 0 ? length : 0);
}

/* "Ariane 64 Block 2   Go" */
static void add_spaced(app_text_t *text, const char *first, const char *second)
{
    app_text_add(text, first);
    if (first[0] && second[0]) { app_text_add(text, "   "); }
    app_text_add(text, second);
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
    app_text_t text;
    app_text_start(&text, caption, sizeof caption);
    add_spaced(&text, rocket, word(status_word(launch, now)));
    draw_fitted(MARGIN, top_of(HOST_FONT_SMALL, caption_baseline), HOST_FONT_SMALL, caption, value_right - MARGIN);
    gfx_set_color(HOST_INK);
}

static void draw_launches(int64_t now)
{
    int32_t top = app_nav_draw_title(&place.nav, NULL, true) + 2;
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

static void draw_launch(const launch_t *launch, int64_t now)
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
    int32_t note = !has_time                              ? STR_TIME_UNKNOWN
                   : launch->precision == PRECISION_HOUR ? STR_TIME_UNCONFIRMED
                   : launch->slipped_at != 0 && now - launch->slipped_at < SLIP_SHOWN_S && !is_done(launch)
                       ? STR_SCHEDULE_CHANGED
                       : -1;
    char day[48];
    app_text_t text;
    say_day(launch, HOST_TIME_DATE, day, sizeof day);
    app_text_start(&text, line, sizeof line);
    add_spaced(&text, rocket, day);
    if (note >= 0) {
        app_text_add(&text, "   ");
        app_text_add(&text, word(note));
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
    app_text_start(&text, line, sizeof line);
    app_text_add(&text, word(status_word(launch, now)));
    say_in(launch, now, soon, sizeof soon);
    if (soon[0]) {
        app_text_add(&text, ", ");
        app_text_add(&text, soon);
    }
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
    host_notice_t notice = { .icon = HOST_ICON_INFO };
    if (status == HTTP_TOO_MANY) {
        notice.icon = HOST_ICON_HOURGLASS;
        app_text_copy(notice.first, sizeof notice.first, word(STR_PLEASE_WAIT));
        app_text_copy(notice.detail, sizeof notice.detail, word(STR_RETRY_LATER));
        ui_draw_notice(&notice);
    } else if (status == HTTP_OK) {
        app_text_copy(notice.first, sizeof notice.first, word(STR_NO_LAUNCHES));
        ui_draw_notice(&notice);
    } else {
        ui_draw_reason(status);
    }
}

static void draw_countries(void)
{
    static const uint8_t NAMES[] = { STR_USA, STR_CHINA, STR_RUSSIA, STR_EUROPE, STR_INDIA, STR_JAPAN, STR_OTHERS };
    int32_t counts[COUNTRIES + 1], order[COUNTRIES + 1], shown = 0, sum = 0;
    for (int32_t i = 0; i < COUNTRIES; i++) {
        if (!(cache.countries_have & (1u << i))) { continue; }
        int32_t at = shown++;
        while (at > 0 && counts[at - 1] < cache.countries[i]) { /* the most first */
            counts[at] = counts[at - 1];
            order[at] = order[at - 1];
            at--;
        }
        counts[at] = cache.countries[i];
        order[at] = i;
        sum += cache.countries[i];
    }
    if (shown == 0) {
        draw_empty(state.status[POLL_COUNTRIES]);
        return;
    }
    if (shown == COUNTRIES && cache.total >= sum) {
        counts[shown] = cache.total - sum;
        order[shown++] = COUNTRIES;
    }
    int32_t top = app_nav_draw_title(&place.nav, NULL, true) + 4;
    int32_t pitch = (gfx_height() - top - 4) / (COUNTRIES + 1);
    int32_t name_width = 0, number_width = app_text_width(HOST_FONT_TEXT | HOST_FONT_TABULAR, "000");
    for (int32_t i = 0; i <= COUNTRIES; i++) {
        int32_t w = app_text_width(HOST_FONT_TEXT, word(NAMES[i]));
        if (w > name_width) { name_width = w; }
    }
    int32_t bar_left = MARGIN + name_width + 12;
    int32_t bar_width = gfx_width() - MARGIN - number_width - 12 - bar_left;
    int32_t cap = gfx_font_cap_height(HOST_FONT_TEXT), ascent = gfx_font_ascent(HOST_FONT_TEXT);
    int32_t leader = counts[0] > 0 ? counts[0] : 1;
    for (int32_t i = 0; i < shown; i++) {
        int32_t middle = top + i * pitch + pitch / 2;
        int32_t text_y = middle - cap / 2 - (ascent - cap);
        app_draw_text(MARGIN, text_y, HOST_FONT_TEXT, word(NAMES[order[i]]));
        ui_draw_bar(bar_left, middle - 7, bar_width, counts[i], leader);
        app_draw_right(gfx_width() - MARGIN, text_y, HOST_FONT_TEXT | HOST_FONT_TABULAR, number_text(counts[i]));
    }
}

static int32_t small_inset(void)
{
    return gfx_font_ascent(HOST_FONT_SMALL) - gfx_font_cap_height(HOST_FONT_SMALL);
}

/* The caption line at the bottom of a chart's screen, centred. */
static int32_t bottom_line_top(void)
{
    return gfx_height() - 10 - gfx_font_cap_height(HOST_FONT_SMALL) - small_inset();
}

static void draw_months(int64_t now)
{
    int32_t number = year_shown(0);
    year_t year;
    if (!year_of(number, &year)) {
        draw_empty(state.status[POLL_YEAR]);
        return;
    }
    int32_t top = app_nav_draw_title(&place.nav, NULL, true) + 8;
    int32_t small_cap = gfx_font_cap_height(HOST_FONT_SMALL), inset = small_inset();
    int32_t totals_top = bottom_line_top();
    int32_t labels_top = totals_top - 12 - small_cap;
    int32_t values[12], high = 1;
    /* The months of this year still to come are gaps, not zeros. */
    host_local_time_t today = { 0 };
    int32_t last_month = 12;
    if (number == cache.year && now != 0) {
        int32_t day;
        app_date_from_days((int32_t)(now / DAY), &today.year, &last_month, &day);
    }
    for (int32_t i = 0; i < 12; i++) {
        values[i] = i < last_month ? year.months[i] : HOST_CHART_NO_VALUE;
        if (i < last_month && year.months[i] > high) { high = year.months[i]; }
    }
    host_chart_t chart = {
        .x = MARGIN, .y = top + small_cap + 6, .width = gfx_width() - 2 * MARGIN,
        .height = labels_top - 8 - (top + small_cap + 6),
        .kind = HOST_CHART_BARS, .flags = HOST_CHART_AXIS | HOST_CHART_FROM_ZERO, .count = 12,
    };
    ui_draw_chart(&chart, values);
    /* The bar geometry repeated from the kit: slot i spans x + i*w/n .. x + (i+1)*w/n less a
     * 2-pixel gap; rows = height - 1; value v at y + rows - 1 - v*(rows-1)/max. */
    int32_t rows_high = chart.height - 1, widest = 0;
    char label[16];
    for (int32_t i = 0; i < 12; i++) {
        nth_word(STR_MONTHS, i, label, sizeof label);
        int32_t w = app_text_width(HOST_FONT_SMALL, label);
        if (w > widest) { widest = w; }
    }
    /* The names when they fit their slots, otherwise the months' numbers. */
    bool numbers = widest > chart.width / 12 - 1;
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
    int32_t top = app_nav_draw_title(&place.nav, NULL, true) + 8;
    int32_t inset = small_inset();
    int32_t caption_top = bottom_line_top();
    int32_t years_top = caption_top - 12 - gfx_font_cap_height(HOST_FONT_SMALL);
    host_chart_t chart = {
        .x = MARGIN, .y = top + 2, .width = gfx_width() - 2 * MARGIN, .height = years_top - 10 - (top + 2),
        .kind = HOST_CHART_AREA, .flags = HOST_CHART_AXIS | HOST_CHART_FROM_ZERO | HOST_CHART_RANGE_RIGHT,
        .count = count,
    };
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
        } else if (app_nav_view(&place.nav) == VIEW_LAUNCH) {
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
