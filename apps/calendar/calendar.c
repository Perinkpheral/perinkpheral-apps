/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What is coming, from any calendar that gives a link to itself - Google,
 * iCloud, Outlook, Nextcloud: the user pastes the calendar's secret
 * address on the device's page, Perinkpheral OS fetches it now and again, and
 * the application reads it as it comes into the next events (ics.c). No
 * account is bound to anything and nothing passes a server of ours.
 *
 * It reminds, too: some minutes before an event Perinkpheral OS says what begins,
 * over whatever has the screen (notification_post). Perinkpheral OS schedules the reminder
 * at a moment worked out from the events in the store, so it comes
 * although the application is not loaded - and after a restart, which
 * forgets alarms, it is set again.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "app_days.h"
#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "ics.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

#define SOURCES_MAX        4
#define FALLBACK_ALARM     1
#define REMIND_ALARM       0
#define TURN_TIMER         0
#define RETRY_TIMER        1
#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_DAY    86400
#define MS_PER_SECOND      1000
#define DAYS_AHEAD         14
#define HTTP_OK            200
/* A reminder that is this late - the device was off - is not given. */
#define LATE_SECONDS       (5 * SECONDS_PER_MINUTE)
/* How long before an event the reminder comes when nothing is chosen. */
#define LEAD_DEFAULT_S     (10 * SECONDS_PER_MINUTE)
/* The list is looked at again at least this often, whatever comes next. */
#define TURN_MAX_S         (6 * 3600)
/* What a feed answered when it is not a calendar: a page asking to sign in. */
#define STATUS_NOT_A_CALENDAR 1
#define STATUS_NO_TIME        2

/* Share the generated string lookup to keep interpreter code within its arena. */
__attribute__((noinline)) static const char *word(int32_t id)
{
    return app_string(id);
}

/* ---- what is kept ---- */

#define EVENTS_FILE    "events"
#define EVENTS_VERSION 4
typedef struct {
    int32_t     version;
    int32_t     count;
    ics_event_t events[ICS_EVENTS_MAX];
} stored_events_t;

#define STATE_KEY     "state"
#define STATE_VERSION 6
#define RETRY_FILE "state-retry"
typedef struct {
    int32_t  version;
    int32_t  status;      /* combined reason when the list is empty */
    int64_t  reminded_to; /* legacy watermark, retained for versions 3 and 4 */
    uint32_t feed_hash;   /* legacy combined identity */
    int32_t delivered_count;
    struct { uint32_t uid; int64_t start; } delivered[ICS_EVENTS_MAX];
    uint32_t source_hash[SOURCES_MAX];
    int64_t  reminded[SOURCES_MAX];
    int64_t  revision; /* orders the KV state and the failed-write journal */
} state_t;

static stored_events_t stored;
static state_t         state;
static bool state_dirty;
static bool state_ready;

/* All four bounded source snapshots share one file, outside the 2 KB KV store.
 * The first data record retains the version-3 file layout for migration. */
typedef struct {
    stored_events_t data;
    char name[ICS_NAME_MAX + 1];
} source_t;
static struct {
    source_t sources[SOURCES_MAX];
    int32_t status[SOURCES_MAX];
} cache;
#define sources cache.sources
_Static_assert(sizeof cache <= 16 * 1024, "calendar cache exceeds four file blocks");
/* Four 255-byte links, two choices, and state, including KV rounding and overhead. */
_Static_assert(4 * (64 + 256) + 2 * (64 + 32) + 64 +
               ((sizeof(state_t) + 31) / 32) * 32 <= HOST_KV_TOTAL_MAX,
               "calendar settings and reminders exceed the store");
static uint32_t configured;
static const char *const FEEDS[] = { "feed", "feed_2", "feed_3", "feed_4" };

/* The same identity is used by merge, navigation and reminder retries. */
static bool event_is(const ics_event_t *event, uint32_t uid, int64_t start)
{
    return event->uid == uid && event->start == start;
}

/* Rebuild the nearest events, with stable source precedence for duplicates. */
static void merge_events(void)
{
    int64_t now = time_now();
    stored.count = 0;
    for (int32_t source = 0; source < SOURCES_MAX; source++) {
        if (!(configured & (1 << source))) { continue; }
        const stored_events_t *data = &sources[source].data;
        for (int32_t i = 0; i < data->count; i++) {
            const ics_event_t *event = &data->events[i];
            /* Ended events still belong to the alarm during its late/retry window. */
            if (event->end <= now && event->start + LATE_SECONDS < now) { continue; }
            int32_t j = 0;
            while (j < stored.count && !event_is(&stored.events[j], event->uid, event->start)) { j++; }
            if (j < stored.count) {
                /* A duplicate reminds as the calendar that has reminded furthest: a calendar
                 * added later does not announce what another one already did. */
                if (state.reminded[source] > state.reminded[stored.events[j].source]) {
                    stored.events[j].source = (uint8_t)source;
                }
                continue;
            }
            stored.count = ics_insert(stored.events, stored.count, ICS_EVENTS_MAX, event, (uint8_t)source);
        }
    }
}

static bool save_state(void)
{
    if (!state_ready) { return false; }
    state.revision++;
    state_dirty = kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) != HOST_OK;
    /* A refused KV write must not lose accepted receipts at background unload.
     * Older journals are harmless once a newer revision reaches KV. */
    if (state_dirty) {
        return file_write(RETRY_FILE, sizeof RETRY_FILE - 1, 0, &state, sizeof state) == HOST_OK;
    }
    return true;
}

/* The OS re-arms a repeating alarm before calling us. Retain that wakeup
 * if moving it fails: a screen timer cannot recover a background unload.
 * Long waits are revisited daily (first_in is bounded by the contract). */
static void set_reminder(int64_t seconds)
{
    if (seconds > SECONDS_PER_DAY) { seconds = SECONDS_PER_DAY; }
    if (alarm_set_every(REMIND_ALARM, SECONDS_PER_MINUTE, (int32_t)seconds) != HOST_OK) {
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_WARNING, "", 0);
        log_write(HOST_LOG_ERROR, "Reminder alarm refused", 22);
        /* Keep a separate ordinary wakeup when all shared repeating slots
         * are occupied. Do not replace an existing repeating alarm: it can
         * still recover if the ordinary allocation is refused as well. */
        alarm_set(FALLBACK_ALARM, (int32_t)(seconds < SECONDS_PER_MINUTE ? seconds : SECONDS_PER_MINUTE));
        timer_set(RETRY_TIMER, SECONDS_PER_MINUTE * MS_PER_SECOND);
    } else {
        alarm_set(FALLBACK_ALARM, 0);
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    }
}

static void retry_alarm(void)
{
    set_reminder(SECONDS_PER_MINUTE);
}

static bool is_same(const void *one, const void *other, uint32_t size)
{
    const uint8_t *a = one, *b = other;
    while (size > 0) {
        size--;
        if (a[size] != b[size]) {
            return false;
        }
    }
    return true;
}

/* How long before an event the reminder comes; -1: never. The choices
 * are the manifest's: "Off", "At start", "5 min" ... */
static int32_t lead_seconds(void)
{
    static const char CHOICES[] = "Off\0At start\0" "5 min\0" "10 min\0" "15 min\0" "30 min\0";
    static const int32_t SECONDS[] = { LEAD_DEFAULT_S, -1, 0, 300, 600, 900, 1800 };
    char choice[16];
    app_setting("remind", choice, sizeof choice);
    int32_t index = ics_name_index(choice, CHOICES, false);
    return SECONDS[index + 1];
}

/* ---- the reminder ---- */

/* The scheduler and delivery retry use the same eligibility window. */
__attribute__((noinline)) static bool needs_reminder(const ics_event_t *event, int64_t now, int32_t lead)
{
    return !event->is_all_day && event->start - lead > state.reminded[event->source] &&
           event->start + LATE_SECONDS >= now;
}

/* The alarm for the reminder that comes next, or none. */
static void schedule_reminder(void)
{
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    if (state_dirty || !state_ready) { retry_alarm(); return; }
    if (now == 0 || lead < 0) {
        alarm_set(REMIND_ALARM, 0);
        alarm_set(FALLBACK_ALARM, 0);
        header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
        return;
    }
    for (int32_t i = 0; i < stored.count; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t due = event->start - lead;
        if (!needs_reminder(event, now, lead)) {
            continue;
        }
        set_reminder(due <= now ? 1 : due - now);
        return; /* the events are in order: this is the nearest */
    }
    alarm_set(REMIND_ALARM, 0);
    alarm_set(FALLBACK_ALARM, 0);
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
}

__attribute__((noinline)) static const char *event_title(const ics_event_t *event)
{
    return event->title[0] != '\0' ? event->title : word(STR_EVENT);
}

/* The reminder is Perinkpheral OS's to show: when it begins, and what it is. */
static bool remind_of(const ics_event_t *event)
{
    host_notice_t notice = { .icon = HOST_ICON_BELL };
    app_time_text(event->start, HOST_TIME_CLOCK, notice.first, sizeof notice.first);
    app_text_copy(notice.detail, sizeof notice.detail, event_title(event));
    return notification_post(&notice) == HOST_OK;
}

void app_on_alarm(int32_t id)
{
    (void)id;
    if (!state_ready) { app_on_load(); screen_redraw(HOST_REDRAW_NORMAL); }
    if (!state_ready) { retry_alarm(); return; }
    if (state_dirty) { save_state(); }
    int64_t now = time_now();
    int32_t lead = lead_seconds();
    bool changed = false;
    bool accepted = true;
    for (int32_t i = 0; i < stored.count && lead >= 0 && now != 0; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t due = event->start - lead;
        if (needs_reminder(event, now, lead) && due <= now) {
            bool delivered = false;
            for (int32_t j = 0; j < state.delivered_count; j++) {
                if (event_is(event, state.delivered[j].uid, state.delivered[j].start)) {
                    delivered = true;
                    break;
                }
            }
            if (delivered) { continue; }
            if (state.delivered_count == ICS_EVENTS_MAX) { accepted = false; break; }
            /* Commit this receipt before the host can accept the notice:
             * storage may become unavailable inside notification_post(),
             * and background termination discards every in-memory receipt. */
            state.delivered[state.delivered_count].uid = event->uid;
            state.delivered[state.delivered_count++].start = event->start;
            if (!save_state()) {
                state.delivered_count--;
                accepted = false;
                break;
            }
            if (!remind_of(event)) {
                state.delivered_count--;
                save_state(); /* A full queue must remain eligible after reload. */
                accepted = false;
                break;
            }
            changed = true;
        }
    }
    if (now != 0 && accepted) {
        changed |= state.delivered_count != 0;
        for (int32_t i = 0; i < SOURCES_MAX; i++) {
            if (sources[i].data.count == 0) { continue; }
            changed |= state.reminded[i] != now;
            state.reminded[i] = now;
        }
        state.delivered_count = 0;
    }
    if (changed) {
        save_state();
    }
    /* Only a refused round needs backoff; newly due reminders should arrive promptly. */
    if (accepted) { schedule_reminder(); }
    else { retry_alarm(); }
}

/* ---- the feed ---- */

static ics_t       reader;
static source_t    incoming;
static bool        is_reading;

static bool events_dirty;

static void save_events(void)
{
    events_dirty = file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &cache, sizeof cache) != HOST_OK;
}

static void store_events(int32_t source, int32_t count)
{
    source_t *saved = &sources[source];
    incoming.data.count = count;
    app_text_copy(incoming.name, sizeof incoming.name, reader.name);
    if (!is_same(saved, &incoming, sizeof incoming)) {
        *saved = incoming;
        events_dirty = true;
    }
    merge_events();
}

static void schedule_next_change(void);

/* Keep the original single-feed hash when optional addresses are absent. */
static uint32_t feed_hash(uint32_t hashes[SOURCES_MAX])
{
    char feed[256];
    uint32_t hash = 2166136261u;
    configured = 0;
    for (int32_t i = 0; i < SOURCES_MAX; i++) {
        hashes[i] = 0;
        int32_t length = app_setting(FEEDS[i], feed, sizeof feed);
        if (length == 0) { continue; }
        hashes[i] = ics_hash(2166136261u, feed, length);
        configured |= 1u << i;
        if (i != 0) { hash = (hash ^ (uint8_t)i) * 16777619u; }
        hash = ics_hash(hash, feed, length);
    }
    return hash;
}

static int32_t count_visible_events(int64_t now);

/* Failures beat loading and empty success. Among failures prefer connectivity,
 * then access, then feed/time/other errors; ties follow setting order. */
static void combine_status(void)
{
    int32_t status = configured ? HTTP_OK : HOST_FAILED_NOT_READY;
    int32_t priority = 0;
    int64_t now = time_now();
    bool visible = now != 0 && count_visible_events(now) > 0;
    for (int32_t i = 0; i < SOURCES_MAX && !visible; i++) {
        if (!(configured & (1 << i))) { continue; }
        int32_t candidate = cache.status[i];
        if (candidate == HTTP_OK) { continue; }
        int32_t rank = 1 + (candidate != 0) + 2 * (candidate == HOST_FAILED_NO_NETWORK) +
                       (candidate == 401 || candidate == 403 || candidate == HOST_FAILED_NOT_ALLOWED);
        if (rank > priority) { status = candidate; priority = rank; }
    }
    if (state.status != status) {
        state.status = status;
        save_state();
    }
}

static void finish_fetch(int32_t source, int32_t status)
{
    is_reading = false;
    /* A malformed configured URL is a failure, not an absent configuration. */
    if (status == HOST_FAILED_NOT_READY) { status = HOST_FAILED_NO_ANSWER; }
    events_dirty |= cache.status[source] != status;
    cache.status[source] = status;
    /* Source results survive off-screen deliveries. Identical rounds write nothing. */
    if (events_dirty) { save_events(); }
    schedule_reminder();
    schedule_next_change();
    screen_redraw(HOST_REDRAW_NORMAL);
}

static ics_opening_t opening;

static int64_t from_local(int32_t days, int32_t seconds)
{
    host_local_time_t parts = { .second = seconds };
    app_date_from_days(days, &parts.year, &parts.month, &parts.day);
    int64_t at = 0;
    if (time_from_local(&parts, &at) == HOST_OK) { return at; }
    /* Old DTSTARTs still supply recurrence durations, and distant UNTILs
     * bound rules, outside the OS conversion range. Visible dates use it. */
    return (int64_t)days * SECONDS_PER_DAY + seconds - time_zone_offset();
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t http_status)
{
    if (request < 0 || request >= SOURCES_MAX) {
        return;
    }
    if (!(configured & (1 << request))) {
        return; /* Empty polls cannot change the status established on load/settings. */
    }
    if (flags & HOST_DATA_FAILED) {
        finish_fetch(request, http_status); /* what was there stays */
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        int64_t now = time_now();
        opening = (ics_opening_t){ 0 };
        if (http_status != HTTP_OK) {
            finish_fetch(request, http_status);
        } else if (now == 0) {
            finish_fetch(request, STATUS_NO_TIME); /* "the next two weeks" needs a today */
        } else {
            incoming = (source_t){ .data.version = EVENTS_VERSION };
            ics_begin(&reader, incoming.data.events, now - LATE_SECONDS - 1, now + (int64_t)DAYS_AHEAD * SECONDS_PER_DAY,
                      from_local);
            is_reading = true;
        }
    }
    if (!is_reading) {
        return;
    }
    int start = ics_opening_feed(&opening, data, length);
    if (start < 0 || ((flags & HOST_DATA_LAST) && start == 0)) {
        finish_fetch(request, STATUS_NOT_A_CALENDAR);
        return;
    }
    ics_feed(&reader, (const char *)data, length);
    if (flags & HOST_DATA_LAST) {
        int32_t count = ics_end(&reader);
        if (reader.complete) { store_events(request, count); }
        finish_fetch(request, reader.complete ? HTTP_OK : STATUS_NOT_A_CALENDAR);
    }
}

/* ---- when things happen ---- */

/* The list changes by itself when an event begins or ends. */
static void schedule_next_change(void)
{
    combine_status();
    int64_t now = time_now(), next = 0;
    for (int32_t i = 0; i < stored.count && now != 0; i++) {
        const ics_event_t *event = &stored.events[i];
        int64_t turn = event->start > now ? event->start : event->end;
        if (turn > now && (next == 0 || turn < next)) {
            next = turn;
        }
    }
    /* A timer is at most some hours away: whatever is further is looked
     * at again when it comes. */
    int64_t in_seconds = next == 0 || next - now > TURN_MAX_S ? TURN_MAX_S : next - now + 1;
    timer_set(TURN_TIMER, (int32_t)in_seconds * MS_PER_SECOND);
}

/* Keep untouched snapshots and reminder progress when a link changes. */
static void sync_sources(const uint32_t hashes[SOURCES_MAX])
{
    if (!state_ready) { merge_events(); return; }
    bool changed = false;
    for (int32_t i = 0; i < SOURCES_MAX; i++) {
        if (state.source_hash[i] == hashes[i]) { continue; }
        state.source_hash[i] = hashes[i];
        state.reminded[i] = 0;
        sources[i] = (source_t){ .data.version = EVENTS_VERSION };
        cache.status[i] = 0;
        changed = true;
    }
    if (changed) { save_state(); }
    if (changed || events_dirty) { save_events(); }
    merge_events();
    combine_status();
}

void app_on_load(void)
{
    uint32_t hashes[SOURCES_MAX];
    uint32_t hash = feed_hash(hashes);
    int32_t length = kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state);
    state_dirty = false;
    state_ready = length >= 0 || length == HOST_ERR_NOT_FOUND;
    bool old_state = (length == offsetof(state_t, delivered) && state.version == 3) ||
                     (length == offsetof(state_t, source_hash) && state.version == 4);
    if (old_state) {
        state.revision = 0;
        if (state.version == 3) { state.delivered_count = 0; }
        state.version = STATE_VERSION;
        for (int32_t i = 0; i < SOURCES_MAX; i++) {
            state.source_hash[i] = hash == state.feed_hash ? hashes[i] : 0;
            state.reminded[i] = state.reminded_to;
        }
    } else if (length == offsetof(state_t, revision) && state.version == 5) {
        state.version = STATE_VERSION;
        state.revision = 0;
    } else if (length != sizeof state || state.version != STATE_VERSION) {
        state = (state_t){ .version = STATE_VERSION };
    }
    state_t retry;
    int32_t recovered = file_read(RETRY_FILE, sizeof RETRY_FILE - 1, 0, &retry, sizeof retry);
    if (recovered == sizeof retry && retry.version == STATE_VERSION && retry.revision > state.revision) {
        state = retry;
        state_dirty = true;
    } else if (recovered < 0 && recovered != HOST_ERR_NOT_FOUND) {
        state_ready = false;
    }
    if ((uint32_t)state.delivered_count > ICS_EVENTS_MAX) { state.delivered_count = 0; }
    length = file_read(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &cache, sizeof cache);
    if (length != sizeof cache) {
        cache.status[0] = cache.status[1] = cache.status[2] = cache.status[3] = state.status;
    }
    bool legacy = length == sizeof(stored_events_t) && sources[0].data.version == 3;
    if (legacy) {
        sources[0].data.version = EVENTS_VERSION;
        sources[0].name[0] = '\0';
    }
    int32_t loaded = legacy ? 1 : (length == sizeof sources || length == sizeof cache) ? SOURCES_MAX : 0;
    for (int32_t i = 0; i < SOURCES_MAX; i++) {
        stored_events_t *data = &sources[i].data;
        if (i >= loaded || data->version != EVENTS_VERSION ||
            data->count < 0 || data->count > ICS_EVENTS_MAX) {
            sources[i] = (source_t){ .data.version = EVENTS_VERSION };
        }
    }
    sync_sources(hashes);
}

static void restore_place(void);

void app_on_focus(void)
{
    restore_place(); /* where the user was, when it was unloaded for another */
    schedule_next_change();
    schedule_reminder();
}

void app_on_timer(int32_t id)
{
    if (id == RETRY_TIMER) { app_on_alarm(REMIND_ALARM); return; }
    if (state_dirty) { save_state(); schedule_reminder(); }
    screen_redraw(HOST_REDRAW_NORMAL);
    schedule_next_change();
}

void app_on_terminate(void)
{
    if (events_dirty) { save_events(); }
    if (state_dirty) { save_state(); }
    if (state_dirty || !state_ready) { retry_alarm(); }
}

void app_on_intent(int32_t intent)
{
    /* A restart forgot the alarm, and a clock set anew moves "in ten
     * minutes" to another moment. */
    (void)intent;
    combine_status();
    schedule_reminder();
}

/* A new version does not inherit the old one's alarm. */
void app_on_updated(void)
{
    schedule_reminder();
}

void app_on_settings(void)
{
    is_reading = false;
    uint32_t hashes[SOURCES_MAX];
    feed_hash(hashes);
    sync_sources(hashes);
    schedule_reminder();
    schedule_next_change();
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the buttons ---- */

/* What is over is not shown; the events are in order of their start. */
static int32_t    shown[ICS_EVENTS_MAX];
static app_list_t list;
static int32_t page_rows = 5; /* page size before the first draw */
static bool       is_reading_one; /* the page of one event, opened with OK */
static uint32_t   read_uid;       /* which: the list under it may change */
static int64_t    read_start;

/* Where the user is - the row, and the event open on its page - kept while the calendar waits
 * under another scene or is unloaded for another application's answer. */
typedef struct {
    int32_t  selected, first;
    int32_t  is_reading_one;
    uint32_t read_uid;
    int64_t  read_start;
    uint32_t selected_uid;
    int64_t  selected_start;
} place_t;
APP_SESSION_FITS(place_t);

void app_on_blur(void)
{
    place_t place = { .selected = list.selected, .first = list.first,
                      .is_reading_one = is_reading_one,
                      .read_uid = read_uid, .read_start = read_start };
    if (list.selected >= 0 && list.selected < list.count) {
        const ics_event_t *event = &stored.events[shown[list.selected]];
        place.selected_uid = event->uid;
        place.selected_start = event->start;
    }
    app_session_save(&place, sizeof place);
}

__attribute__((noinline)) static int32_t find_visible_event(uint32_t uid, int64_t start)
{
    for (int32_t row = 0; row < list.count; row++) {
        const ics_event_t *event = &stored.events[shown[row]];
        if (event_is(event, uid, start)) { return row; }
    }
    return -1;
}

/* This list always has a selected row; paging stops at its ends. */
__attribute__((noinline)) static int32_t clamp_row(int32_t row)
{
    return row < 0 || list.count == 0 ? 0 : row >= list.count ? list.count - 1 : row;
}

static void restore_place(void)
{
    list.count = count_visible_events(time_now());
    place_t place;
    if (app_session_load(&place, sizeof place)) {
        list.selected = place.selected;
        list.first = place.first;
        int32_t row = find_visible_event(place.selected_uid, place.selected_start);
        if (row >= 0) { list.selected = row; }
        is_reading_one = place.is_reading_one != 0;
        read_uid = place.read_uid;
        read_start = place.read_start;
    }
    list.selected = clamp_row(list.selected);
}

static int32_t count_visible_events(int64_t now)
{
    int32_t count = 0;
    for (int32_t i = 0; i < stored.count; i++) {
        if (stored.events[i].end > now) {
            shown[count++] = i;
        }
    }
    return count;
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_LONG_PRESSED) {
        for (int32_t i = 0; i < SOURCES_MAX; i++) {
            if (configured & (1 << i)) { poll_run(i); }
        }
        return;
    }
    if (is_reading_one) {
        return;
    }
    if (list.count == 0) { return; }
    if (event == BTN_OK_PRESSED) {
        is_reading_one = true;
        read_uid = stored.events[shown[list.selected]].uid;
        read_start = stored.events[shown[list.selected]].start;
        screen_redraw(HOST_REDRAW_NORMAL);
        return;
    }
    int32_t button = BTN_OF(event), action = BTN_ACTION_OF(event);
    if ((button != BTN_UP && button != BTN_DOWN) || action < BTN_PRESSED) { return; }
    int32_t before = list.selected;
    int32_t by = button == BTN_UP ? -1 : 1;
    if (action == BTN_PRESSED) {
        list.selected = (list.selected + list.count + by) % list.count;
    } else {
        by *= page_rows;
        list.selected = clamp_row(list.selected + by);
        if (list.selected != before) { list.first = clamp_row(list.first + by); }
    }
    if (list.selected != before) { screen_redraw(HOST_REDRAW_NO_BLINK); }
}

void app_on_back(void)
{
    if (is_reading_one) {
        is_reading_one = false;
        screen_redraw(HOST_REDRAW_NORMAL);
    } else {
        screen_leave();
    }
}

/* ---- the picture ---- */

/* When an event is, as short as Perinkpheral OS says it: "15:00" today, "Sat
 * 15:00" this week, "21 Nov" beyond it; a whole day is its day alone, and
 * one that has begun is "Now". Returns the length. */
static int32_t say_when(const ics_event_t *event, int64_t now, char *line, int32_t size)
{
    if (event->start <= now) {
        app_text_copy(line, size, word(event->is_all_day ? STR_TODAY : STR_NOW));
        return app_text_length(line);
    }
    return app_time_text(event->start, event->is_all_day ? HOST_TIME_NEAR_DAY : HOST_TIME_NEAR, line,
                         size);
}

/* Why there is nothing to show: Perinkpheral OS's word for what the network
 * came to, and this application's own for the rest. */
static void draw_reason(void)
{
    host_notice_t notice = { .icon = HOST_ICON_WARNING };
    const char *first, *detail = "";
    if (state.status == STATUS_NOT_A_CALENDAR) {
        first = word(STR_NOT_A_CALENDAR);
        detail = word(STR_CHECK_THE_ADDRESS);
    } else if (state.status == STATUS_NO_TIME) {
        first = word(STR_NO_TIME_YET);
        notice.icon = HOST_ICON_CLOCK;
    } else if (ui_draw_reason(state.status) == 0) {
        first = word(STR_NO_EVENTS);
        notice.icon = HOST_ICON_CHECK;
    } else {
        return;
    }
    app_text_copy(notice.first, sizeof notice.first, first);
    app_text_copy(notice.detail, sizeof notice.detail, detail);
    ui_draw_notice(&notice);
}

#define MOMENT_MAX  47 /* "Завтра 15:04" is two bytes a letter */
#define TITLE_LINES 4

static bool same_local_day(int64_t first, int64_t second)
{
    host_local_time_t start, end;
    return time_local_at(first, &start) == HOST_OK && time_local_at(second, &end) == HOST_OK &&
           is_same(&start, &end, offsetof(host_local_time_t, weekday));
}

/* Shared by the date and clock rows; a date repeats only across midnight. */
__attribute__((noinline)) static void format_range(char *text, int64_t first,
                                                  int64_t last, int32_t style)
{
    int32_t length = app_time_text(first, style, text, MOMENT_MAX + 1);
    if (style == HOST_TIME_DATE && same_local_day(first, last)) { return; }
    /* Each endpoint is bounded by MOMENT_MAX, leaving room for the separator. */
    text[length++] = ' ';
    text[length++] = '-';
    text[length++] = ' ';
    app_time_text(last, style, text + length, MOMENT_MAX + 1);
}

__attribute__((noinline)) static void detail_text(int32_t top, int32_t style, const char *text)
{
    ui_draw_text(HOST_UI_MARGIN, top, style, text, app_text_length(text),
                 gfx_width() - 2 * HOST_UI_MARGIN);
}

/* The event leads; the dates below include overnight and multi-day ranges. */
static void draw_event(const ics_event_t *event)
{
    char       when[2 * MOMENT_MAX + 8];
    int32_t    width = gfx_width() - 2 * HOST_UI_MARGIN;

    const char *name = event_title(event);
    bool caption = (configured & (configured - 1)) != 0;
    int32_t top = HOST_UI_MARGIN;
    int32_t lines = ui_draw_text_lines(HOST_UI_MARGIN, top, HOST_UI_TEXT, name,
                                       app_text_length(name), width, TITLE_LINES - caption);
    top += lines * ui_text_height(HOST_UI_TEXT) + HOST_UI_MARGIN;

    /* iCalendar's all-day DTEND is the first day outside the event. */
    int64_t last = event->is_all_day && event->end > event->start ? event->end - 1 : event->end;
    format_range(when, event->start, last, HOST_TIME_DATE);
    detail_text(top, HOST_UI_CAPTION, when);
    top += ui_text_height(HOST_UI_CAPTION) + HOST_UI_MARGIN / 2;

    if (event->is_all_day) {
        app_text_copy(when, sizeof when, word(STR_ALL_DAY));
    } else {
        format_range(when, event->start, event->end, HOST_TIME_CLOCK);
    }
    detail_text(top, lines <= 2 ? HOST_UI_TITLE : HOST_UI_TEXT, when);
    if (caption) {
        _Static_assert(STR_CALENDAR_4 == STR_CALENDAR_1 + SOURCES_MAX - 1, "calendar labels must be consecutive");
        int32_t source = event->source;
        const char *calendar = sources[source].name;
        if (calendar[0] == '\0') { calendar = word(STR_CALENDAR_1 + source); }
        int32_t bottom = gfx_height() - HOST_UI_ACTIONS_HEIGHT - HOST_UI_MARGIN -
                         ui_text_height(HOST_UI_CAPTION);
        detail_text(bottom, HOST_UI_CAPTION, calendar);
    }
    app_draw_actions(NULL, word(STR_BACK));
}

void app_on_draw(void)
{
    int64_t now = time_now();
    int32_t count = count_visible_events(now);
    if (count == 0 || now == 0) {
        is_reading_one = false;
        draw_reason();
        return;
    }
    list.count = count;
    list.selected = clamp_row(list.selected);
    if (is_reading_one) {
        /* The event being read, wherever it is now; one that is over, or
         * gone from the calendar, gives the screen back to the list. */
        int32_t row = find_visible_event(read_uid, read_start);
        if (row >= 0) {
            list.selected = row;
            draw_event(&stored.events[shown[row]]);
            return;
        }
        is_reading_one = false;
    }
    int32_t first = ui_list_first(&list), visible = ui_list_visible(&list);
    if (first >= 0) { list.first = first; }
    page_rows = visible;
    for (int32_t row = first; row < first + visible && row < count; row++) {
        const ics_event_t *event = &stored.events[shown[row]];
        char when[MOMENT_MAX + 1];
        int32_t when_length = say_when(event, now, when, sizeof when);
        /* Perinkpheral OS's row keeps the moment whole and cuts the name. */
        const char *name = event_title(event);
        ui_draw_list_row(&list, row, name, app_text_length(name), when, when_length, HOST_ICON_NONE);
    }
}
