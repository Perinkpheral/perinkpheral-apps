// SPDX-License-Identifier: MIT
/* Reminders: the soonest launch that is Go and timed to the minute, once, at the chosen lead;
 * a full notification queue tried again in a minute; nothing from a stale list, for an hour's
 * precision, when Off, or twice for a launch whose time moved; set again after a restart. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* The stand-in takes every notice: the device's queue of four is imposed here. */
#define notification_post queued_notice
#include "../launch.c"
#undef notification_post
#include "standin.h"
#include "fixtures.h"

int32_t notification_post(const host_notice_t *notice);
static bool is_full;
int32_t queued_notice(const host_notice_t *notice)
{
    return is_full ? HOST_ERR_NO_SPACE : notification_post(notice);
}

#define ARIANE 1795517100 /* Go, to the minute: 2026-11-24 10:45 UTC */
#define USSF   1795535700 /* Go, to the minute: 15:55 UTC */

static void start(const char *remind)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin_set("remind", remind);
    app_on_load();
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
}

int main(void)
{
    /* Progress is sooner but TBC: the first reminder is Ariane's, 15 minutes before. */
    start("15 min");
    assert(standin_alarm_left(REMIND_ALARM) == ARIANE - 15 * MINUTE - AT);

    /* The next morning, the list taken again just before: due now. */
    standin_clock(ARIANE - 15 * MINUTE - 600);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    standin_clock(ARIANE - 15 * MINUTE);
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1 && !strcmp(standin()->last_notice.first, "12:45"));
    assert(!strcmp(standin()->last_notice.detail, "Amazon Leo (LE-04)"));
    assert(standin()->last_notice.icon == HOST_ICON_BELL);
    assert(standin_alarm_left(REMIND_ALARM) == USSF - 15 * MINUTE - time_now());
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1);

    /* The time moved: it is not told again. */
    int32_t length;
    char *body = fixture("upcoming.json", &length);
    memcpy(strstr(body, "2026-11-24T10:45:00Z"), "2026-11-24T11:45:00Z", 20);
    standin_picked(POLL_UPCOMING, -1, 200, body, length);
    free(body);
    standin_clock(ARIANE + HOUR - 15 * MINUTE);
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(REMIND_ALARM) == USSF - 15 * MINUTE - time_now());

    /* A full queue: once more in a minute, then it is told. */
    standin_clock(USSF - 15 * MINUTE - 60);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    standin_clock(USSF - 15 * MINUTE);
    is_full = true;
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(REMIND_ALARM) == MINUTE);
    is_full = false;
    standin_pass(MINUTE * 1000);
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 2 && !strcmp(standin()->last_notice.first, "17:55"));

    /* A restart forgot the alarm; the started intent sets it again. */
    start("1 h");
    standin_restart();
    assert(standin_alarm_left(REMIND_ALARM) == 0);
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(standin_alarm_left(REMIND_ALARM) == ARIANE - HOUR - AT);
    app_on_updated();
    assert(standin_alarm_left(REMIND_ALARM) == ARIANE - HOUR - AT);

    /* A list older than three hours reminds of nothing; nor does Off. */
    standin_clock(AT + 3 * HOUR + 1);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(standin_alarm_left(REMIND_ALARM) == 0);
    start("Off");
    assert(standin_alarm_left(REMIND_ALARM) == 0);

    /* A launch timed to the hour is never reminded of: Unknown Payload at 09:35, Go, HR. */
    start("1 h");
    for (int32_t i = 0; i < cache.count[0]; i++) {
        if (cache.lists[0][i].precision == PRECISION_HOUR) {
            assert(cache.lists[0][i].status == ST_GO && !needs_reminder(&cache.lists[0][i], AT, HOUR));
        }
    }
    puts("space-launch: reminders at the lead, once, after a slip, a full queue, a restart, a stale list and Off passed");
    return 0;
}
