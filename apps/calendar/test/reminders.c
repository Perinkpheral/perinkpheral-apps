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

int main(void)
{
    standin_reset();
    standin_clock(1794955920);
    standin_set("feed", "https://calendar.invalid/feed");
    standin_set("remind", "At start");
    app_on_load();
    state.version = 3;
    state.reminded_to = time_now() - 60;
    assert(kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, offsetof(state_t, delivered)) == HOST_OK);
    state = (state_t){0}; app_on_load();
    assert(state.version == STATE_VERSION && state.reminded_to == time_now() - 60 && state.delivered_count == 0);
    stored.count = 5;
    for (int i = 0; i < stored.count; i++) {
        stored.events[i] = (ics_event_t){ .uid = (uint32_t)i + 1, .start = time_now(), .end = time_now() + 60 };
        snprintf(stored.events[i].title, sizeof stored.events[i].title, "Event %d", i + 1);
    }
    save_events();
    queued = 4;
    app_on_alarm(0);
    assert(standin()->notices == 0 && state.delivered_count == 0 && state.reminded_to < time_now());
    queued = 0;
    app_on_alarm(0);
    assert(standin()->notices == 4 && state.delivered_count == 4 && state.reminded_to < time_now());
    assert(standin_alarm_left(0) == 1);
    state = (state_t){0}; app_on_load();
    app_on_alarm(0);
    assert(standin()->notices == 4 && state.delivered_count == 4);
    queued = 0;
    app_on_alarm(0);
    assert(standin()->notices == 5 && !strcmp(standin()->last_notice.detail, "Event 5"));
    assert(state.reminded_to == time_now() && state.delivered_count == 0);
    app_on_alarm(0);
    assert(standin()->notices == 5);

    /* Focus restores the map and the selected identity before any deferred drawing. */
    standin()->is_session_writable = true;
    app_on_focus(); list.selected = 2;
    app_on_blur();
    stored.events[0].end = time_now() - 1;
    save_events();
    memset(shown, 0, sizeof shown); list = (app_list_t){0};
    app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    assert(is_reading_one && read_uid == 3 && read_start == stored.events[2].start);
    assert(list.selected == 1 && shown[1] == 2);
    puts("calendar: full notification queue retries across unload without duplicates; focus restores selection before OK passed");
}
