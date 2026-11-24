// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kv_set refused_set
#define kv_get refused_get
#define alarm_set_at refused_at
#define alarm_set refused_alarm
#include "../sun.c"
#undef kv_set
#undef kv_get
#undef alarm_set_at
#undef alarm_set
#include "standin.h"

int32_t kv_set(const char *, int32_t, const void *, int32_t);
int32_t kv_get(const char *, int32_t, void *, int32_t);
int32_t alarm_set_at(int32_t, int64_t);
int32_t alarm_set(int32_t, int32_t);
static bool no_kv, no_read, no_alarm;
static int no_at;
int32_t refused_set(const char *key, int32_t n, const void *value, int32_t size)
{
    return no_kv ? HOST_ERR_FAILED : kv_set(key, n, value, size);
}
int32_t refused_get(const char *key, int32_t n, void *value, int32_t size)
{
    return no_read && n == 8 && !memcmp(key, "notified", 8) ? HOST_ERR_FAILED : kv_get(key, n, value, size);
}
int32_t refused_at(int32_t id, int64_t at)
{
    if (no_at > 0) { no_at--; return HOST_ERR_NO_SPACE; }
    return alarm_set_at(id, at);
}
int32_t refused_alarm(int32_t id, int32_t seconds)
{
    return no_alarm && seconds != 0 ? HOST_ERR_NO_SPACE : alarm_set(id, seconds);
}
static int64_t rise;
static void setup(void)
{
    no_kv = no_read = no_alarm = false; no_at = 0;
    standin_reset(); standin()->zone_offset = 3 * 3600;
    standin_clock(1795309200); /* 2026-11-22 00:00 local */
    standin_set("lat", "59.94"); standin_set("lon", "30.31");
    standin_set("rise_notice", "At the time"); standin_set("set_notice", "Off");
    app_on_load(); app_on_settings();
    rise = standin()->alarms[0].at;
    assert(rise > time_now());
    standin_clock(rise);
}
static void reload(void)
{
    app_on_terminate();
    memset(&receipts, 0, sizeof receipts);
    app_on_load();
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
    standin_clock(standin()->alarms[0].at); app_on_alarm(0);
    assert(standin()->notices == 2);
    reload(); app_on_alarm(0); assert(standin()->notices == 2);

    setup(); standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_alarm(0); app_on_alarm(0); assert(standin()->notices == 1);
    assert(standin_alarm_left(0) == 60);
    standin()->refuse_writes = HOST_OK; reload(); app_on_alarm(0);
    assert(standin()->notices == 1);

    /* Read failures cannot be mistaken for a missing receipt. */
    setup(); app_on_alarm(0); reload(); no_read = true;
    app_on_alarm(0); assert(standin()->notices == 1 && standin_alarm_left(0) == 60);
    no_read = false; app_on_alarm(0); assert(standin()->notices == 1);

    setup(); standin_clock(rise - 120);
    no_at = 1; app_on_settings(); assert(standin_alarm_left(0) == 60);
    no_at = 1; no_alarm = true; app_on_settings();
    assert(standin()->error_logs > 0 && standin()->timer_due_ms[1] != 0);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_WARNING);
    no_alarm = false; standin_pass(120000); app_on_timer(1);
    assert(standin()->notices == 1);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE);
    puts("sun: refused receipt writes across background unload, recovery, read errors and alarm refusal passed");
}
