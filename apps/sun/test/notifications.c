// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The SDK records notices; this wrapper also exercises a full device queue. */
#define notification_post post_to_queue
#include "../sun.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);
static bool queue_full;
int32_t post_to_queue(const host_notice_t *notice)
{
    return queue_full ? HOST_ERR_NO_SPACE : notification_post(notice);
}

static int64_t moment(int32_t year, int32_t month, int32_t day, int32_t hour)
{
    host_local_time_t parts = { .year = year, .month = month, .day = day, .hour = hour };
    int64_t at = 0;
    assert(time_from_local(&parts, &at) == HOST_OK);
    return at;
}

static void setup(void)
{
    standin_reset();
    queue_full = false;
    standin()->zone_offset = 3 * SECONDS_PER_HOUR;
    standin_clock(moment(2026, 11, 22, 0));
    standin_set("lat", "59.94");
    standin_set("lon", "30.31");
    standin_set("rise_notice", "Off");
    standin_set("set_notice", "Off");
    app_on_load();
}

static sun_day_t today(void)
{
    host_local_time_t date;
    assert(time_local(&date) == HOST_OK);
    date.hour = 12;
    date.minute = date.second = 0;
    int64_t noon;
    assert(time_from_local(&date, &noon) == HOST_OK);
    place_t place;
    assert(read_place(&place));
    return sun_on(noon, place.latitude, place.longitude);
}

static void assert_alarm(int64_t at)
{
    assert(standin()->alarms[NOTIFY_ALARM].kind == STANDIN_ALARM_AT);
    assert(standin()->alarms[NOTIFY_ALARM].at == at);
}

static void choices_and_notices(void)
{
    const char *choices[] = { "At the time", "5 min", "15 min", "30 min", "1 h" };
    const int32_t leads[] = { 0, 300, 900, 1800, 3600 };
    const char *keys[] = { "rise_notice", "set_notice" };
    for (int32_t event = 0; event < 2; event++) {
        for (int32_t option = 0; option < 5; option++) {
            setup();
            sun_day_t sun = today();
            assert(sun.rise == 1795327958LL && sun.set == 1795353440LL);
            int64_t at = event == 0 ? sun.rise : sun.set;
            standin_set(keys[event], choices[option]);
            assert(lead_seconds(keys[event]) == leads[option]);
            app_on_settings();
            assert_alarm(at - leads[option]);
            app_on_alarm(NOTIFY_ALARM); /* An early or repeated callback is harmless. */
            assert(standin()->notices == 0);
            standin_clock(at - leads[option]);
            app_on_alarm(NOTIFY_ALARM);
            assert(standin()->notices == 1);
            const host_notice_t *notice = &standin()->last_notice;
            char clock[16];
            app_time_text(at, HOST_TIME_CLOCK, clock, sizeof clock);
            assert(!strcmp(notice->first, clock));
            assert(!strcmp(notice->detail, app_string(event == 0 ? STR_SUNRISE : STR_SUNSET)));
            assert(notice->icon == (event == 0 ? HOST_ICON_SUNRISE : HOST_ICON_SUNSET));
            assert(notice->second[0] == '\0' && notice->ok[0] == '\0' && notice->back[0] == '\0');
            int writes = standin()->store_writes;
            app_on_alarm(NOTIFY_ALARM);
            app_on_load(); app_on_intent(HOST_INTENT_STARTED); app_on_alarm(NOTIFY_ALARM);
            assert(standin()->notices == 1 && standin()->store_writes == writes);
            assert_alarm(moment(2026, 11, 23, 0));
        }
    }
    setup();
    standin_set("rise_notice", NULL); standin_set("set_notice", NULL);
    assert(lead_seconds("rise_notice") == -1 && lead_seconds("set_notice") == -1);
    app_on_installed();
    assert(standin_alarms() == 0);
    standin_set("rise_notice", "unexpected");
    assert(lead_seconds("rise_notice") == -1);
    standin_set("rise_notice", "15 min"); app_on_settings();
    assert(standin_alarms() != 0);
    standin_set("rise_notice", "Off"); app_on_settings(); app_on_alarm(NOTIFY_ALARM);
    assert(standin_alarms() == 0 && standin()->notices == 0);
}

static void lifecycle_and_queue(void)
{
    setup();
    sun_day_t sun = today();
    standin_set("rise_notice", "15 min"); standin_set("set_notice", "30 min");
    app_on_installed(); assert_alarm(sun.rise - 900);
    standin_set("rise_notice", "1 h"); app_on_settings(); assert_alarm(sun.rise - 3600);
    standin_set("rise_notice", "Off"); app_on_settings(); assert_alarm(sun.set - 1800);
    standin_set("rise_notice", "15 min"); app_on_settings(); assert_alarm(sun.rise - 900);
    standin_restart();
    assert(standin_alarms() == 0);
    app_on_load(); app_on_intent(HOST_INTENT_STARTED); assert_alarm(sun.rise - 900);
    alarm_set(NOTIFY_ALARM, 0); app_on_updated(); assert_alarm(sun.rise - 900);

    /* A setting changed inside its lead window delivers promptly. */
    standin_clock(sun.rise - 100);
    app_on_settings(); assert_alarm(time_now() + 1);
    standin_pass(1000);
    queue_full = true;
    app_on_alarm(NOTIFY_ALARM);
    assert(standin()->notices == 0);
    assert_alarm(time_now() + 60);
    standin_pass(60000); app_on_load();
    queue_full = false;
    app_on_alarm(NOTIFY_ALARM);
    assert(standin()->notices == 1);
    assert_alarm(sun.set - 1800);
    app_on_alarm(NOTIFY_ALARM); assert(standin()->notices == 1);
    standin_restart(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert_alarm(sun.set - 1800); /* An accepted sunrise remains remembered. */

    standin_clock(sun.set + 1);
    app_on_intent(HOST_INTENT_TIME_CHANGED); assert_alarm(time_now() + 1);
    standin_pass(1000); app_on_alarm(NOTIFY_ALARM);
    assert(standin()->last_notice.icon == HOST_ICON_SUNSET);
    setup();
    sun = today();
    standin_set("set_notice", "At the time");
    standin_clock(sun.set + 301); app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert_alarm(moment(2026, 11, 23, 0)); /* A stale event is not replayed. */
    app_on_alarm(NOTIFY_ALARM); assert(standin()->notices == 0);

    setup();
    standin_set("rise_notice", "5 min"); app_on_settings();
    int64_t old = standin()->alarms[NOTIFY_ALARM].at;
    standin_set("lon", "25.0"); app_on_settings();
    assert_alarm(today().rise - 300);
    assert(standin()->alarms[NOTIFY_ALARM].at != old);
    standin()->zone_offset = 0;
    app_on_intent(HOST_INTENT_TIME_CHANGED); assert_alarm(moment(2026, 11, 22, 0));
    standin_clock(0); app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(standin_alarms() == 0);
    standin_clock(moment(2026, 11, 22, 0));
    standin_set("lat", ""); app_on_settings();
    assert(standin_alarms() == 0);
}

static void polar_and_rollover(void)
{
    setup();
    standin_set("rise_notice", "1 h"); standin_set("set_notice", "1 h");
    standin_set("lat", "89"); standin_set("lon", "0");
    for (int32_t month = 6; month <= 12; month += 6) {
        standin_clock(moment(2026, month, 21, 12));
        assert(today().sky == (month == 6 ? SKY_POLAR_DAY : SKY_POLAR_NIGHT));
        app_on_settings(); assert_alarm(moment(2026, month, 22, 0));
        app_on_alarm(NOTIFY_ALARM); assert(standin()->notices == 0);
        standin_clock(moment(2026, month, 22, 0));
        app_on_alarm(NOTIFY_ALARM); assert(standin()->notices == 0);
        assert_alarm(moment(2026, month, 23, 0));
    }
    setup();
    standin_set("rise_notice", "15 min");
    standin_clock(moment(2026, 12, 31, 23)); app_on_settings();
    assert_alarm(moment(2027, 1, 1, 0));
    standin_clock(moment(2027, 1, 1, 0)); app_on_alarm(NOTIFY_ALARM);
    assert_alarm(today().rise - 900);

    /* Tomorrow's sunrise lead can begin before today's midnight, including
     * after a notice today was accepted. */
    setup(); standin()->zone_offset = 0;
    standin_set("lat", "0"); standin_set("lon", "82.5");
    standin_set("rise_notice", "1 h");
    int64_t midnight = moment(2026, 3, 21, 0);
    standin_clock(midnight);
    sun_day_t tomorrow = today();
    assert(tomorrow.rise > midnight && tomorrow.rise < midnight + 3600);
    standin_clock(moment(2026, 3, 20, 0));
    sun_day_t sun = today();
    app_on_settings(); assert_alarm(time_now() + 1);
    standin_pass(1000); app_on_alarm(NOTIFY_ALARM);
    assert(standin()->notices == 1);
    assert_alarm(tomorrow.rise - 3600);
    standin_clock(tomorrow.rise - 3600);
    app_on_load(); app_on_alarm(NOTIFY_ALARM);
    assert(standin()->notices == 2);
    assert(sun.rise < time_now());
    assert_alarm(midnight);
    standin_clock(midnight); app_on_alarm(NOTIFY_ALARM);
    assert(standin()->notices == 2); /* Midnight does not duplicate the early notice. */

    /* A local date beyond the date line is also the notification's date. */
    setup(); standin()->zone_offset = 13 * SECONDS_PER_HOUR;
    standin_set("lat", "-13.833"); standin_set("lon", "-171.75");
    standin_set("rise_notice", "15 min");
    standin_clock(moment(2026, 12, 31, 0)); app_on_settings();
    assert_alarm(today().rise - 900);
    standin_clock(moment(2026, 12, 31, 23)); app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert_alarm(moment(2027, 1, 1, 0));
    standin_clock(moment(2027, 1, 1, 0)); app_on_alarm(NOTIFY_ALARM);
    assert_alarm(today().rise - 900);
}

int main(void)
{
    choices_and_notices();
    lifecycle_and_queue();
    polar_and_rollover();
    puts("sun: lead choices, notice fields, Off, lifecycle, queue retry, polar dates and rollover passed");
    return 0;
}
