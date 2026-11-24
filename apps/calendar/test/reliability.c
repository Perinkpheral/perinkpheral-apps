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
static bool no_kv, no_read, no_alarm;
static int capacity;
int32_t limited_notice(const host_notice_t *notice)
{
    if (capacity == 0) { return HOST_ERR_NO_SPACE; }
    capacity--;
    return notification_post(notice);
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
    return no_alarm ? HOST_ERR_NO_SPACE : alarm_set_every(id, seconds, first_in);
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
    no_kv = no_read = no_alarm = false;
    capacity = 100;
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_alarm = app_on_alarm, .on_terminate = app_on_terminate };
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
int main(void)
{
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
    puts("calendar: background storage outages, timely recovery, receipt migration and retained alarm recovery passed");
}
