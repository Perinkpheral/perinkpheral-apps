// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kv_set refused_set
#define kv_get refused_get
#define alarm_set_every refused_every
#define notification_post limited_notice
#include "../calendar.c"
#include "../ics.c"
#undef kv_set
#undef kv_get
#undef alarm_set_every
#undef notification_post
#include "standin.h"

int32_t kv_set(const char *, int32_t, const void *, int32_t);
int32_t kv_get(const char *, int32_t, void *, int32_t);
int32_t alarm_set_every(int32_t, int32_t, int32_t);
int32_t notification_post(const host_notice_t *);
static bool no_kv, no_read, no_alarm, outage_on_accept, outage_on_refusal;
static int capacity, other_repeating;
int32_t limited_notice(const host_notice_t *notice)
{
    if (capacity == 0) {
        if (outage_on_refusal) { standin()->refuse_writes = HOST_ERR_FAILED; }
        return HOST_ERR_NO_SPACE;
    }
    capacity--;
    int32_t result = notification_post(notice);
    if (outage_on_accept && result == HOST_OK) { standin()->refuse_writes = HOST_ERR_FAILED; }
    return result;
}
int32_t refused_set(const char *key, int32_t n, const void *value, int32_t size)
{
    return no_kv ? HOST_ERR_FAILED : kv_set(key, n, value, size);
}
int32_t refused_get(const char *key, int32_t n, void *value, int32_t size)
{
    return no_read && n == 5 && !memcmp(key, "state", 5) ? HOST_ERR_FAILED : kv_get(key, n, value, size);
}
int32_t refused_every(int32_t id, int32_t seconds, int32_t first_in)
{
    /* The SDK stand-in has no shared pool. Model other apps holding all
     * 24 slots, while leaving ordinary alarm capacity available. */
    if (no_alarm || (other_repeating == HOST_ALARM_REPEATING_MAX &&
                     standin()->alarms[id].kind != STANDIN_ALARM_EVERY)) {
        return HOST_ERR_NO_SPACE;
    }
    return alarm_set_every(id, seconds, first_in);
}
static void reload(void)
{
    /* A background instance has no writable session and loses all globals. */
    standin_terminate();
    memset(&state, 0, sizeof state);
    memset(&cache, 0, sizeof cache);
    memset(&stored, 0, sizeof stored);
    state_dirty = state_ready = events_dirty = is_reading = false;
    standin_load();
}
static void setup(void)
{
    no_kv = no_read = no_alarm = outage_on_accept = outage_on_refusal = false;
    capacity = 100; other_repeating = 0;
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_alarm = app_on_alarm, .on_terminate = app_on_terminate, .on_updated = app_on_updated };
    standin_reset(); standin()->is_new = false; standin_clock(1768485600);
    standin()->zone_offset = 0;
    standin_set("feed", "https://calendar.invalid/feed");
    standin_set("remind", "At start");
    app_on_load();
    stored.count = 1;
    stored.events[0] = (ics_event_t){ .uid = 7, .start = time_now(), .end = time_now() + 600 };
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION;
    save_events(); save_state();
}
/* Start with no repeating allocation, including the update path that
 * clears old alarms. Ordinary capacity must preserve a background wakeup. */
static void full_repeating_pool(bool update, bool release)
{
    setup();
    stored.events[0].start += 120; stored.events[0].end += 120;
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION; save_events();
    assert(standin_alarms() == 0);
    if (update) {
        schedule_reminder();
        assert(standin()->alarms[0].kind == STANDIN_ALARM_EVERY);
    }
    other_repeating = HOST_ALARM_REPEATING_MAX;
    if (update) { standin_updated(); }
    else { app_on_settings(); }
    assert(standin()->error_logs > 0);
    reload();
    assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    /* Only now may another application release its repeating slot. */
    if (release) { other_repeating--; }
    capacity = 0;
    for (int minute = 0; minute < 2; minute++) {
        standin_pass(60000);
        int due = -1;
        for (int id = 0; id < HOST_ALARM_COUNT; id++) {
            if (standin_alarm_due(id)) { due = id; break; }
        }
        assert(due >= 0);
        standin_alarm(due);
        assert(standin()->notices == 0);
        reload();
    }
    /* The fallback also preserves a queue-full retry through unload. */
    capacity = 1;
    standin_pass(60000);
    int due = -1;
    for (int id = 0; id < HOST_ALARM_COUNT; id++) {
        if (standin_alarm_due(id)) { due = id; break; }
    }
    assert(due >= 0); standin_alarm(due);
    assert(standin()->notices == 1);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);
}

static void acceptance_outage(bool journal_only)
{
    /* Refuse both stores inside the accepting host call, not before it or
     * after the handler. Keep the outage through termination and a fresh
     * instance, then recover after another background retry. */
    setup(); no_kv = journal_only; outage_on_accept = true;
    standin_alarm(0); assert(standin()->notices == 1);
    assert(standin()->refuse_writes == HOST_ERR_FAILED);
    reload(); standin_pass(60000);
    assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 1);
    reload();
    standin()->refuse_writes = HOST_OK; no_kv = outage_on_accept = false;
    standin_updated(); standin_pass(60000);
    for (int id = 0; id < HOST_ALARM_COUNT; id++) {
        if (standin_alarm_due(id)) { standin_alarm(id); }
    }
    assert(standin()->notices == 1);
}

static void refusal_outage(bool journal_only, bool update, bool rollback_outage)
{
    /* Optionally refuse both stores inside the refusing host call, after
     * the preflight receipt. Keep the outage through termination and a fresh
     * delivery; screen timers cannot carry this retry. */
    setup(); no_kv = journal_only; capacity = 0; outage_on_refusal = rollback_outage;
    schedule_reminder();
    standin_pass(1000); assert(standin_alarm_due(0));
    standin_alarm(0);
    assert(standin()->notices == 0);
    assert(standin()->refuse_writes == (rollback_outage ? HOST_ERR_FAILED : HOST_OK));
    reload();
    assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    if (update) { standin_updated(); reload(); }
    standin_pass(60000);
    for (int id = 0; id < HOST_ALARM_COUNT; id++) {
        if (standin_alarm_due(id)) { standin_alarm(id); }
    }
    assert(standin()->notices == 0);
    reload();
    standin()->refuse_writes = HOST_OK; no_kv = outage_on_refusal = false;
    capacity = 100; /* Leave room for duplicates so the assertions detect them. */
    standin_pass(60000);
    for (int id = 0; id < HOST_ALARM_COUNT; id++) {
        if (standin_alarm_due(id)) { standin_alarm(id); }
    }
    assert(standin()->notices <= 1);
    if (!rollback_outage) { assert(standin()->notices == 1); }
    reload(); app_on_alarm(0);
    /* Accepted limitation: queue refusal plus failed rollback in both receipt
     * stores through termination may lose the reminder; delivery is at most once. */
    assert(standin()->notices <= 1);
    if (!rollback_outage) { assert(standin()->notices == 1); }
}

int main(void)
{
    acceptance_outage(false);
    acceptance_outage(true);

    /* Version 5 receipts from 0.5.2 migrate without replaying notices. */
    setup(); app_on_alarm(0);
    state.version = 5;
    assert(kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, offsetof(state_t, revision)) == HOST_OK);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);

    setup();
    no_kv = true;
    app_on_alarm(0); assert(standin()->notices == 1);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);
    /* A stale journal must not override a later successful KV receipt. */
    no_kv = false; app_on_alarm(0); reload();
    stored.events[0].uid++; stored.events[0].start += 60; stored.events[0].end += 60;
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION;
    save_events(); standin_pass(60000);
    app_on_alarm(0); assert(standin()->notices == 2);
    reload(); app_on_alarm(0); assert(standin()->notices == 2);

    /* Preserve the accepted subset when both KV and the notice queue refuse. */
    setup(); stored.count = 5;
    for (int i = 0; i < 5; i++) {
        stored.events[i] = (ics_event_t){ .uid = (uint32_t)i + 1,
            .start = time_now(), .end = time_now() + 600 };
    }
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION;
    save_events(); no_kv = true; capacity = 4;
    app_on_alarm(0); assert(standin()->notices == 4);
    reload(); capacity = 1; app_on_alarm(0); assert(standin()->notices == 5);
    no_kv = false; reload(); app_on_alarm(0); assert(standin()->notices == 5);

    /* Keep both stores refused through termination and fresh background loads.
     * Without the delivery gate, the first accepted notice is replayed. */
    setup(); schedule_reminder();
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin_pass(1000); standin_alarm(0); assert(standin()->notices == 0);
    reload();
    assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    standin_pass(60000); assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 0);
    reload(); standin()->refuse_writes = HOST_OK;
    standin_pass(60000); assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 1);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);

    /* A notice stays suppressed if its receipt predates the outage. */
    setup(); app_on_alarm(0); standin()->refuse_writes = HOST_ERR_FAILED;
    reload(); standin_pass(60000); app_on_alarm(0);
    assert(standin()->notices == 1);

    /* An outage that outlasts the delivery window cannot produce a stale notice. */
    setup(); standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_alarm(0); reload(); standin_pass((LATE_SECONDS + 1) * 1000);
    standin()->refuse_writes = HOST_OK; standin_alarm(0);
    assert(standin()->notices == 0);

    setup(); no_read = true; reload(); app_on_alarm(0);
    assert(standin()->notices == 0 && standin_alarm_left(0) == 60);
    assert(stored.count == 1); /* A receipt read failure keeps the cached list. */
    no_read = false; app_on_alarm(0); assert(standin()->notices == 1);

    /* The OS re-arms before delivery, even when every scheduling attempt
     * in the handler fails. Termination cancels timers, but not this alarm. */
    setup(); stored.events[0].start += 120;
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION; save_events();
    schedule_reminder();
    assert(standin_alarm_left(0) == 120);
    no_alarm = true; capacity = 0; standin_pass(120000); standin_alarm(0);
    assert(standin()->error_logs > 0 && standin()->timer_due_ms[RETRY_TIMER] != 0);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_WARNING);
    reload(); assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    assert(standin_alarm_left(0) == 60);
    no_alarm = false; capacity = 1; standin_pass(60000);
    assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 1);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE);

    /* The first callback is bounded to a day; it then moves to the event. */
    setup(); stored.events[0].start += 2 * SECONDS_PER_DAY;
    stored.events[0].end += 2 * SECONDS_PER_DAY;
    schedule_reminder(); assert(standin_alarm_left(0) == SECONDS_PER_DAY);
    standin_pass(SECONDS_PER_DAY * 1000); standin_alarm(0);
    assert(standin()->notices == 0 && standin_alarm_left(0) == SECONDS_PER_DAY);
    full_repeating_pool(false, false);
    full_repeating_pool(false, true);
    full_repeating_pool(true, false);
    full_repeating_pool(true, true);
    /* Queue refusal alone must recover with either receipt store and update path. */
    refusal_outage(false, false, false);
    refusal_outage(true, false, false);
    refusal_outage(false, true, false);
    refusal_outage(true, true, false);
    /* Double fault: preserve at-most-once delivery, accepting possible loss. */
    refusal_outage(false, false, true);
    refusal_outage(true, false, true);
    refusal_outage(false, true, true);
    refusal_outage(true, true, true);
    puts("calendar: acceptance/refusal-boundary outages, queue retries, update/unload recovery and repeating-pool exhaustion passed");
}
