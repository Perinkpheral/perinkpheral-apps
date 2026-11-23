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
 * Perinkpheral OS polls the library and picks the values (manifest): the upcoming launches
 * every half hour, the previous ones every three hours, this year's pages and the counts by
 * country once a day. Past years come with the package (files/history.bin, from GCAT).
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
#include "app_values.h"
#include "host_api.h"
#include "strings.h"

enum { POLL_UPCOMING, POLL_PREVIOUS, POLL_YEAR, POLL_COUNTRIES, POLLS };
/* The order of "pick" in the manifest. */
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

#define LIST_MAX     10 /* launches a poll asks for */
#define TEXT_MAX     64 /* a picked value is at most 63 bytes */
#define PAGES        4  /* this year's pages of 100 completed launches */
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

#define CACHE_FILE    "cache"
#define CACHE_VERSION 1
typedef struct {
    int32_t  version;
    int32_t  count[2];                /* upcoming, previous */
    int32_t  spare;
    launch_t lists[2][LIST_MAX];      /* upcoming from the soonest; previous from the latest */
    int32_t  year;                    /* of the pages and countries, UTC */
    int32_t  total;                   /* the library's count of the year's completed launches */
    uint32_t pages_have, countries_have;
    year_t   pages[PAGES];
    int32_t  countries[COUNTRIES];
    int32_t  last_year;               /* the year before, kept when the year turned */
    year_t   last;
} cache_t;
_Static_assert(sizeof(cache_t) <= 8 * 1024, "the cache takes two file blocks");
_Static_assert(sizeof(year_t) == 26 && sizeof(launch_t) == 216, "records without padding, as files keep them");

#define STATE_KEY     "state"
#define STATE_VERSION 1
typedef struct {
    int32_t  version;
    int32_t  status[POLLS];   /* the last answer of each poll: HTTP status or HOST_FAILED_* */
    uint32_t postponed;       /* polls whose interval a 429 set */
    int32_t  told_next;
    uint32_t told[TOLD_MAX];  /* launches reminded of, by id */
} state_t;

#define SEEN_KEY "seen" /* int64: when the upcoming list was last taken */

static cache_t cache;
static state_t state;
static int64_t seen_at;
static bool    cache_dirty;

static bool is_same(const void *one, const void *other, uint32_t size)
{
    const uint8_t *a = one, *b = other;
    while (size-- > 0) {
        if (a[size] != b[size]) { return false; }
    }
    return true;
}

static void save_state(void)
{
    kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
}

static void save_cache(void)
{
    cache_dirty = file_write(CACHE_FILE, sizeof CACHE_FILE - 1, 0, &cache, sizeof cache) != HOST_OK;
}

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
    return app_value_element_text(path, element, text, sizeof text) ? number_of(text) : -1;
}

/* "2026-11-24T10:13:00Z" (or with ".123Z"); 0 when it is not such a moment. */
static int64_t parse_moment(const char *text)
{
    if (app_text_length(text) < 19) { return 0; }
    int32_t year = digits(text, 0, 4), month = digits(text, 5, 2), day = digits(text, 8, 2);
    int32_t hour = digits(text, 11, 2), minute = digits(text, 14, 2), second = digits(text, 17, 2);
    if (year < 1957 || month < 1 || month > 12 || day < 1 || hour < 0 || minute < 0 || second < 0) { return 0; }
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

/* This year so far, from the pages that came. */
static void sum_pages(year_t *year)
{
    *year = (year_t){ 0 };
    for (int32_t page = 0; page < PAGES; page++) {
        if (!(cache.pages_have & (1u << page))) { continue; }
        for (int32_t month = 0; month < 12; month++) { year->months[month] += cache.pages[page].months[month]; }
        year->failures += cache.pages[page].failures;
    }
}

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
 * polls are asked for the new one at once. */
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
        cache_dirty = true;
    }
    char stored[8];
    app_setting("year", stored, sizeof stored);
    const char *text = number_text(year);
    if (number_of(stored) != year && kv_set("year", 4, text, app_text_length(text)) == HOST_OK) {
        poll_run(POLL_YEAR);
        poll_run(POLL_COUNTRIES);
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
    if (app_value_count(path) == count) { app_value_element_text(path, element, text, TEXT_MAX); }
}

static uint8_t number_column(int32_t path, int32_t element, int32_t count, uint8_t otherwise)
{
    int32_t number = app_value_count(path) == count ? picked_number(path, element) : -1;
    return number < 0 || number >= PRECISION_UNKNOWN ? otherwise : (uint8_t)number;
}

static launch_t incoming[LIST_MAX];

/* A picked list of launches; false when the answer is not one. */
static bool take_launches(int32_t list)
{
    int32_t count = app_value_count(PICK_ID);
    if (count > LIST_MAX || app_value_count(PICK_NAME) != count || app_value_count(PICK_NET) != count ||
        (count == 0 && picked_number(PICK_COUNT, 0) < 0)) {
        return false;
    }
    int64_t now = time_now();
    for (int32_t i = 0; i < count; i++) {
        launch_t *launch = &incoming[i];
        char text[TEXT_MAX];
        *launch = (launch_t){ 0 };
        app_value_element_text(PICK_ID, i, text, sizeof text);
        launch->id = hash_of(text);
        app_value_element_text(PICK_NAME, i, launch->name, sizeof launch->name);
        app_value_element_text(PICK_NET, i, text, sizeof text);
        launch->net = parse_moment(text);
        if (launch->net == 0) { return false; }
        launch->status = number_column(PICK_STATUS, i, count, 0);
        launch->precision = number_column(PICK_PRECISION, i, count, PRECISION_UNKNOWN);
        column(PICK_PROVIDER, i, count, launch->provider);
        column(PICK_SITE, i, count, launch->site);
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

/* One page of this year's completed launches. */
static bool take_page(int32_t page)
{
    int32_t count = app_value_count(YEAR_NET), total = picked_number(YEAR_COUNT, 0);
    if (page < 0 || page >= PAGES || app_value_count(YEAR_STATUS) != count || total < 0) {
        return false;
    }
    year_t year = { 0 };
    for (int32_t i = 0; i < count; i++) {
        char text[TEXT_MAX];
        app_value_element_text(YEAR_NET, i, text, sizeof text);
        int32_t month = app_text_length(text) >= 19 ? digits(text, 5, 2) : 0; /* "2026-01-03T02:09:16Z" */
        if (digits(text, 0, 4) != cache.year || month < 1 || month > 12) { return false; } /* another year */
        year.months[month - 1]++;
        year.failures += picked_number(YEAR_STATUS, i) != ST_SUCCESS;
    }
    if (!(cache.pages_have & (1u << page)) || cache.total != total ||
        !is_same(&cache.pages[page], &year, sizeof year)) {
        cache.pages[page] = year;
        cache.pages_have |= 1u << page;
        cache.total = total;
        cache_dirty = true;
    }
    return true;
}

static bool take_country(int32_t country)
{
    int32_t total = picked_number(0, 0);
    if (country < 0 || country >= COUNTRIES || total < 0) { return false; }
    if (!(cache.countries_have & (1u << country)) || cache.countries[country] != total) {
        cache.countries[country] = total;
        cache.countries_have |= 1u << country;
        cache_dirty = true;
    }
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
    return lead >= 0 && now != 0 && now - seen_at < FRESH_S && launch->status == ST_GO &&
           (launch->precision < PRECISION_HOUR || launch->precision == PRECISION_UNKNOWN) &&
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
    (void)id;
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    for (int32_t i = 0; i < cache.count[0]; i++) {
        const launch_t *launch = &cache.lists[0][i];
        if (!needs_reminder(launch, now, lead) || launch->net - lead > now) { continue; }
        host_notice_t notice = { .icon = HOST_ICON_BELL };
        app_time_text(launch->net, HOST_TIME_CLOCK, notice.first, sizeof notice.first);
        char rocket[TEXT_MAX];
        app_text_copy(notice.detail, sizeof notice.detail, split_name(launch, rocket));
        if (notification_post(&notice) != HOST_OK) {
            alarm_set(REMIND_ALARM, MINUTE); /* the queue is full: once more in a minute */
            return;
        }
        state.told[state.told_next] = launch->id;
        state.told_next = (state.told_next + 1) % TOLD_MAX;
        save_state();
    }
    schedule_reminder();
}

/* ---- answers ---- */

static void update_header(void);

/* What came is kept; a 429 keeps what is there and moves the poll past the time it names. */
void app_on_picked(int32_t request, int32_t http_status)
{
    if (request < 0 || request >= POLLS) { return; }
    int32_t status = http_status;
    if (http_status == HTTP_OK) {
        bool taken = request == POLL_YEAR        ? take_page(answer_item())
                     : request == POLL_COUNTRIES ? take_country(answer_item())
                                                 : take_launches(request);
        if (!taken) { status = HOST_FAILED_NO_ANSWER; }
    }
    uint32_t bit = 1u << request, postponed = state.postponed;
    if (http_status == HTTP_TOO_MANY) {
        host_answer_info_t info = { 0 };
        answer_info(&info, sizeof info);
        int32_t wait = info.retry_after > 0 ? info.retry_after : RETRY_DEFAULT_S;
        poll_set_interval(request, wait < 10 ? 10 : wait > DAY ? DAY : wait);
        state.postponed |= bit;
    } else if (status == HTTP_OK && (state.postponed & bit)) {
        poll_set_interval(request, 0);
        state.postponed &= ~bit;
    }
    bool changed = state.status[request] != status || state.postponed != postponed;
    state.status[request] = status;
    if (changed) { save_state(); }
    if (request == POLL_UPCOMING && status == HTTP_OK && seen_at != time_now()) {
        seen_at = time_now();
        kv_set(SEEN_KEY, sizeof SEEN_KEY - 1, &seen_at, sizeof seen_at);
    }
    if (cache_dirty) { save_cache(); }
    if (request <= POLL_PREVIOUS) {
        schedule_reminder();
        update_header();
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the life of it ---- */

void app_on_load(void)
{
    if (!app_state_load(STATE_KEY, &state, sizeof state, STATE_VERSION)) {
        state = (state_t){ .version = STATE_VERSION };
    }
    if ((uint32_t)state.told_next >= TOLD_MAX) { state.told_next = 0; }
    if (kv_get(SEEN_KEY, sizeof SEEN_KEY - 1, &seen_at, sizeof seen_at) != sizeof seen_at) { seen_at = 0; }
    if (file_read(CACHE_FILE, sizeof CACHE_FILE - 1, 0, &cache, sizeof cache) != sizeof cache ||
        cache.version != CACHE_VERSION || (uint32_t)cache.count[0] > LIST_MAX || (uint32_t)cache.count[1] > LIST_MAX) {
        cache = (cache_t){ .version = CACHE_VERSION };
    }
    follow_year();
    if (cache_dirty) { save_cache(); }
}

void app_on_intent(int32_t intent)
{
    (void)intent; /* a restart forgot the alarm; a clock set anew moves its moment */
    schedule_reminder();
}

void app_on_installed(void)
{
    schedule_reminder();
}

void app_on_updated(void)
{
    schedule_reminder();
}

void app_on_settings(void)
{
    schedule_reminder();
    screen_redraw(HOST_REDRAW_NORMAL);
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
    app_nav_check(&place.nav, SCREENS);
    fit_rows();
    update_header();
    tick();
}

void app_on_timer(int32_t id)
{
    (void)id;
    update_header();
    screen_redraw(HOST_REDRAW_NORMAL);
    tick();
}

/* The year a chart shows: what UP and DOWN chose, within the years there are. */
static int32_t year_shown(int32_t chart)
{
    int32_t latest = latest_year(), year = place.chosen_year[chart] != 0 ? place.chosen_year[chart] : latest;
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
        if (year >= FIRST_YEAR && year <= latest_year()) {
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
    if ((launch->status == ST_GO || launch->status == ST_TBC) && launch->net <= now) { return STR_AWAITING; }
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
    if (time_local_at(launch->net, &then) != HOST_OK) { return; }
    if (launch->precision >= PRECISION_MONTH && launch->precision != PRECISION_UNKNOWN) {
        app_text_start(&text, out, size);
        if (launch->precision == PRECISION_MONTH) {
            char month[16];
            nth_word(STR_MONTHS, then.month - 1, month, sizeof month);
            app_text_add(&text, month);
            app_text_add(&text, " ");
        }
        app_text_add_number(&text, then.year);
        return;
    }
    if (style == HOST_TIME_NEAR_DAY && launch->precision <= PRECISION_HOUR && time_local(&today) == HOST_OK &&
        then.year == today.year && then.month == today.month && then.day == today.day) {
        style = HOST_TIME_CLOCK;
    }
    app_time_text(launch->net, style, out, size);
}

/* "in 17 h" while the time is known and the list fresh; below an hour, "Within 1 h". */
static void say_in(const launch_t *launch, int64_t now, char *out, int32_t size)
{
    int64_t seconds = launch->net - now;
    out[0] = '\0';
    if (seconds <= 0 || now == 0 || now - seen_at > COUNTDOWN_S ||
        (launch->precision > PRECISION_HOUR && launch->precision != PRECISION_UNKNOWN)) {
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
    int32_t top = app_nav_draw_title(&place.nav, word(STR_LAUNCHES), true) + 2;
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
    int32_t lines = ui_draw_text_lines(MARGIN, 10, HOST_UI_TEXT, mission, app_text_length(mission), width, 2);
    int32_t top = 12 + lines * ui_text_height(HOST_UI_TEXT);

    /* The rocket, the day, and what is uncertain about the time. */
    bool has_time = launch->precision <= PRECISION_HOUR || launch->precision == PRECISION_UNKNOWN;
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
        app_time_text(launch->net, HOST_TIME_CLOCK, line, sizeof line);
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

/* "2026 by country" and the count of screens. */
__attribute__((noinline)) static int32_t draw_year_title(int32_t pattern, int32_t year)
{
    char title[48];
    fill_number(title, sizeof title, pattern, year);
    return app_nav_draw_title(&place.nav, title, true);
}

/* Nothing to show yet: why. */
static void draw_empty(int32_t status)
{
    host_notice_t notice = { .icon = HOST_ICON_INFO };
    if (status == HTTP_OK) {
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
    int32_t top = draw_year_title(STR_BY_COUNTRY, cache.year) + 4;
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
    int32_t top = draw_year_title(STR_BY_MONTH, number) + 8;
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
    host_text_argument_t args[3] = { { .number = total }, { .number = total - year.failures },
                                     { .number = year.failures } };
    fill(line, sizeof line, STR_YEAR_LINE, args, 3);
    app_draw_centered(gfx_width() / 2, totals_top, HOST_FONT_SMALL, line);
}

static void draw_history(void)
{
    int32_t latest = latest_year(), count = latest - FIRST_YEAR + 1, values[HOST_CHART_VALUES_MAX], high = 0;
    if (count > HOST_CHART_VALUES_MAX) { count = HOST_CHART_VALUES_MAX; }
    for (int32_t i = 0; i < count; i++) {
        year_t year;
        values[i] = year_of(FIRST_YEAR + i, &year) ? year_total(&year) : HOST_CHART_NO_VALUE;
        if (values[i] > high) { high = values[i]; }
    }
    if (high == 0) {
        draw_empty(state.status[POLL_YEAR]);
        return;
    }
    int32_t top = app_nav_draw_title(&place.nav, word(STR_SINCE), true) + 8;
    int32_t inset = small_inset();
    int32_t caption_top = bottom_line_top();
    int32_t years_top = caption_top - 12 - gfx_font_cap_height(HOST_FONT_SMALL);
    host_chart_t chart = {
        .x = MARGIN, .y = top + 2, .width = gfx_width() - 2 * MARGIN, .height = years_top - 10 - (top + 2),
        .kind = HOST_CHART_LINE, .flags = HOST_CHART_AXIS | HOST_CHART_FROM_ZERO | HOST_CHART_RANGE_RIGHT,
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
    host_text_argument_t args[2] = { { .number = year }, { .number = value == HOST_CHART_NO_VALUE ? 0 : value } };
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
