// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* The stand-in accepts every notice; impose the device's queue capacity here. */
#define notification_post queued_notice
#include "../calendar.c"
#include "../ics.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);

static int queued;
int32_t queued_notice(const host_notice_t *notice)
{
    if (queued == 4) { return HOST_ERR_NO_SPACE; }
    queued++;
    return notification_post(notice);
}

static void prompt_reminders(void)
{
    standin_reset();
    standin_clock(1768485600); /* 2026-01-15 14:00 UTC */
    standin()->zone_offset = 0;
    standin_set("feed", "https://calendar.invalid/feed");
    standin_set("remind", "At start");
    app_on_load();
    stored.count = 1;
    stored.events[0] = (event_t){ .uid = 1, .start = time_now(), .end = time_now() + 600 };
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION; save_events(); save_state();

    /* A restart must not impose the queue's backoff on an untried reminder. */
    standin_restart();
    state = (state_t){0}; stored = (stored_events_t){0};
    app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert(standin_alarm_left(0) == 1);
    standin_pass(1000); queued = 0; app_on_alarm(0);
    assert(standin()->notices == 1);

    /* A newly fetched event can already be inside its lead time. */
    const char feed[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:new\r\nDTSTART:20260115T140500Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    state.reminded[0] = time_now() - 600;
    standin_set("remind", "10 min");
    app_on_data(0, (const uint8_t *)feed, sizeof feed - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(stored.count == 1 && state.status == 200 && standin_alarm_left(0) == 1);
    standin_pass(1000); app_on_alarm(0);
    assert(standin()->notices == 2);

    /* Moving the clock past the start must still deliver promptly. */
    standin_set("remind", "At start");
    app_on_settings();
    assert(standin_alarm_left(0) == stored.events[0].start - time_now());
    standin_clock(stored.events[0].start + 1);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(standin_alarm_left(0) == 1);
    standin_pass(1000); app_on_alarm(0);
    assert(standin()->notices == 3 && standin_alarm_left(0) == 0);
}

int main(void)
{
    prompt_reminders();
    standin_reset();
    standin_clock(1794955920);
    standin_set("feed", "https://calendar.invalid/feed");
    standin_set("remind", "At start");
    app_on_load();
    uint32_t hashes[SOURCES_MAX];
    state.feed_hash = feed_hash(hashes);
    state.version = 3;
    state.reminded_to = time_now() - 60;
    assert(kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, offsetof(state_t, delivered)) == HOST_OK);
    state = (state_t){0}; app_on_load();
    assert(state.version == STATE_VERSION && state.reminded[0] == time_now() - 60 && state.delivered_count == 0);
    stored.count = 5;
    for (int i = 0; i < stored.count; i++) {
        stored.events[i] = (event_t){ .uid = (uint32_t)i + 1, .start = time_now(), .end = time_now() + 600 };
        stored.events[i].title = (uint16_t)(i * 16);
        int length = snprintf(sources[0].titles + i * 16, 16, "Event %d", i + 1);
        assert(length > 0 && length < 16);
    }
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION; save_events(); combine_status();
    queued = 4;
    int writes = standin()->store_writes;
    app_on_alarm(0);
    assert(standin()->notices == 0 && state.delivered_count == 0 && state.reminded[0] < time_now());
    assert(standin()->store_writes > writes && standin_alarm_left(0) == 60);
    standin_pass(60000);
    queued = 0;
    app_on_alarm(0);
    assert(standin()->notices == 4 && state.delivered_count == 4 && state.reminded[0] < time_now());
    assert(standin_alarm_left(0) == 60);
    writes = standin()->store_writes;
    state = (state_t){0}; app_on_load();
    standin_pass(60000);
    app_on_alarm(0);
    assert(standin()->store_writes > writes && standin_alarm_left(0) == 60);
    assert(standin()->notices == 4 && state.delivered_count == 4);
    queued = 0;
    standin_pass(60000);
    app_on_alarm(0);
    assert(standin()->notices == 5 && !strcmp(standin()->last_notice.detail, "Event 5"));
    assert(state.reminded[0] == time_now() && state.delivered_count == 0);
    app_on_alarm(0);
    assert(standin()->notices == 5);

    /* Focus restores the map and the selected identity before any deferred drawing. */
    standin()->is_session_writable = true;
    app_on_focus(); list.selected = 2;
    app_on_blur();
    stored.events[0].end = time_now() - 1;
    sources[0].data = stored; sources[0].data.version = EVENTS_VERSION; save_events();
    memset(shown, 0, sizeof shown); list = (app_list_t){0};
    app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    assert(is_reading_one && read_uid == 3 && read_start == stored.events[2].start);
    assert(list.selected == 1 && stored.events[shown[1]].uid == 3);
    puts("calendar: prompt reminders after restart, fetch and clock change; full queue backoff across unload without duplicates; focus selection passed");
}
