// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../metals.c"
#include "standin.h"

static void reload(void)
{
    app_on_blur();
    status = 0;
    pending = false;
    session = (session_t){0};
    month = incoming = (month_t){0};
    scan = (scanner_t){0};
    app_on_load();
    app_on_focus();
}

static void fail(void)
{
    standin()->request_count = 0;
    app_on_data(REQUEST, 0, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    assert(standin_subject.intents & (1u << HOST_INTENT_TIME_CHANGED));
    assert(standin_subject.intents & (1u << HOST_INTENT_NETWORK_UP));
    standin_clock(0);
    app_on_load();
    app_on_focus();
    assert(!pending && status == HOST_FAILED_NO_NETWORK && standin()->sends == 0);
    app_on_intent(HOST_INTENT_STARTED);
    assert(standin_alarm_left(ALARM) == EVERY_SECONDS);
    standin_clock(standin_subject.at);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(pending && standin()->sends == 1);
    app_on_intent(HOST_INTENT_NETWORK_UP);
    assert(standin()->sends == 1);
    fail();
    app_on_button(BTN_DOWN_PRESSED);
    standin_handlers.on_draw = app_on_draw;
    standin_draw();
    uint64_t failure_picture = standin()->picture;
    int writes = standin()->store_writes;
    reload();
    assert(session.metal == 1 && status == HOST_FAILED_NO_NETWORK && !pending);
    standin_draw();
    assert(standin()->picture == failure_picture && standin()->sends == 1);
    app_on_intent(HOST_INTENT_NETWORK_UP);
    assert(pending && standin()->sends == 2);
    /* The queued request survives an unload; focus must not duplicate it. */
    standin()->request_count = 0;
    reload();
    assert(pending && status == 0 && standin()->sends == 2);
    fail();
    reload();
    assert(status == HOST_FAILED_NO_NETWORK && !pending && standin()->sends == 2);
    assert(standin()->store_writes >= writes);
    puts("Metals: clock and network recovery, failure display and empty restored scene passed");
    return 0;
}
