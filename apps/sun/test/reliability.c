// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kv_set refused_set
#define kv_get refused_get
#define alarm_set_every refused_every
#include "../sun.c"
#undef kv_set
#undef kv_get
#undef alarm_set_every
#include "standin.h"

int32_t kv_set(const char *, int32_t, const void *, int32_t);
int32_t kv_get(const char *, int32_t, void *, int32_t);
int32_t alarm_set_every(int32_t, int32_t, int32_t);
static bool no_kv, no_read, no_alarm;
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
    return no_alarm ? HOST_ERR_NO_SPACE : alarm_set_every(id, seconds, first_in);
}
static int64_t rise;
static void setup(void)
{
    no_kv = no_read = no_alarm = false;
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_alarm = app_on_alarm, .on_terminate = app_on_terminate };
    standin_reset(); standin()->is_new = false; standin()->zone_offset = 3 * 3600;
    standin_clock(1795309200); /* 2026-11-22 00:00 local */
    standin_set("lat", "59.94"); standin_set("lon", "30.31");
    standin_set("rise_notice", "At the time"); standin_set("set_notice", "Off");
    app_on_load(); app_on_settings();
    rise = time_now() + standin_alarm_left(0);
    assert(rise > time_now());
    standin_pass((rise - time_now()) * 1000);
}
static void reload(void)
{
    standin_terminate();
    memset(&receipts, 0, sizeof receipts);
    standin_load();
}
int main(void)
{
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
    puts("sun: background storage outages, timely recovery, receipt migration and retained alarm recovery passed");
}
