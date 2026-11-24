// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kv_set refused_set
#define kv_get refused_get
#define alarm_set_every refused_every
#define notification_post limited_notice
#include "../sun.c"
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
    return no_read && n == 8 && !memcmp(key, "notified", 8) ? HOST_ERR_FAILED : kv_get(key, n, value, size);
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
static int64_t rise;
static void prepare(void)
{
    no_kv = no_read = no_alarm = outage_on_accept = outage_on_refusal = false;
    capacity = 100; other_repeating = 0;
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_alarm = app_on_alarm, .on_terminate = app_on_terminate, .on_updated = app_on_updated };
    standin_reset(); standin()->is_new = false; standin()->zone_offset = 3 * 3600;
    standin_clock(1795309200); /* 2026-11-22 00:00 local */
    standin_set("lat", "59.94"); standin_set("lon", "30.31");
    standin_set("rise_notice", "At the time"); standin_set("set_notice", "Off");
    app_on_load();
    rise = sun_on(time_now() + 12 * SECONDS_PER_HOUR, 59940000, 30310000).rise;
}
static void setup(void)
{
    prepare(); app_on_settings();
    assert(rise > time_now());
    standin_pass((rise - time_now()) * 1000);
}
static void reload(void)
{
    standin_terminate();
    memset(&receipts, 0, sizeof receipts);
    standin_load();
}
/* Start with no repeating allocation, including the update path that
 * clears old alarms. Ordinary capacity must preserve a background wakeup. */
static void full_repeating_pool(bool update, bool release)
{
    prepare();
    standin_clock(rise - 120);
    assert(standin_alarms() == 0);
    if (update) {
        app_on_settings();
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

    /* The original pair of timestamps remains readable after the update. */
    setup();
    int64_t legacy[2] = { rise, 0 };
    assert(kv_set(NOTIFIED_KEY, sizeof NOTIFIED_KEY - 1, legacy, sizeof legacy) == HOST_OK);
    reload(); app_on_alarm(0); assert(standin()->notices == 0);

    setup(); no_kv = true;
    app_on_alarm(0); assert(standin()->notices == 1);
    app_on_settings(); app_on_intent(HOST_INTENT_TIME_CHANGED); app_on_alarm(0);
    assert(standin()->notices == 1 && standin_alarm_left(0) == 60);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);
    no_kv = false; app_on_alarm(0); reload();
    /* Newer KV wins over the failed-save journal on the following day. */
    standin_clock(rise + 86400); app_on_settings();
    standin_pass(standin_alarm_left(0) * 1000); app_on_alarm(0);
    assert(standin()->notices == 2);
    reload(); app_on_alarm(0); assert(standin()->notices == 2);

    /* Storage stays refused through termination, reload and a real alarm.
     * Delivery waits for a writable receipt store, then happens just once. */
    setup(); standin()->refuse_writes = HOST_ERR_FAILED;
    standin_alarm(0); assert(standin()->notices == 0);
    reload(); assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    standin_pass(60000); assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 0);
    reload(); standin()->refuse_writes = HOST_OK;
    standin_pass(60000); assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 1);
    reload(); app_on_alarm(0); assert(standin()->notices == 1);

    setup(); app_on_alarm(0); standin()->refuse_writes = HOST_ERR_FAILED;
    reload(); standin_pass(60000); app_on_alarm(0);
    assert(standin()->notices == 1);

    setup(); standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_alarm(0); reload(); standin_pass(301000);
    standin()->refuse_writes = HOST_OK; standin_alarm(0);
    assert(standin()->notices == 0); /* Outside the five-minute window. */

    /* Read failures cannot be mistaken for a missing receipt. */
    setup(); app_on_alarm(0); reload(); no_read = true;
    app_on_alarm(0); assert(standin()->notices == 1 && standin_alarm_left(0) == 60);
    no_read = false; app_on_alarm(0); assert(standin()->notices == 1);

    /* Keep the OS's re-armed alarm through refusal and background unload.
     * Freeing capacity needs no screen timer or lifecycle event to recover. */
    setup(); standin_clock(rise - 120); app_on_settings();
    standin_pass(120000); no_alarm = true;
    standin_alarm(0);
    assert(standin()->notices == 1);
    assert(standin()->error_logs > 0 && standin()->timer_due_ms[RETRY_TIMER] != 0);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_WARNING);
    reload(); assert(standin()->timer_due_ms[RETRY_TIMER] == 0);
    assert(standin_alarm_left(0) == 60);
    no_alarm = false; standin_pass(60000);
    assert(standin_alarm_due(0)); standin_alarm(0);
    assert(standin()->notices == 1 && standin_alarm_left(0) > 60);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE);
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
    puts("sun: acceptance/refusal-boundary outages, queue retries, update/unload recovery and repeating-pool exhaustion passed");
}
