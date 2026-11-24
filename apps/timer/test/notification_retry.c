// SPDX-License-Identifier: MIT
#include <assert.h>
#define notification_post queued_notice
#include "../timer.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);
static bool queue_full, fail_receipt;
int32_t queued_notice(const host_notice_t *notice)
{
    if (queue_full) { return HOST_ERR_NO_SPACE; }
    int32_t result = notification_post(notice);
    if (fail_receipt) { standin()->refuse_writes = HOST_ERR_NO_SPACE; }
    return result;
}
static void reload(void) { state = (timer_state_t){0}; app_on_load(); }
static void finish(void)
{
    standin_reset();
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(1500000);
    app_on_alarm(COUNTDOWN_ALARM);
}
int main(void)
{
    queue_full = true;
    finish();
    assert(standin()->notices == 0 && standin_alarm_left(2) == 1);
    reload();
    standin_pass(1000);
    queue_full = false;
    fail_receipt = true;
    app_on_alarm(2);
    assert(standin()->notices == 1 && standin_alarm_left(2) == 0);
    app_on_terminate();
    reload();
    app_on_alarm(COUNTDOWN_ALARM);
    standin_handlers.on_updated = app_on_updated;
    standin_updated();
    assert(standin()->notices == 1 && standin_alarms() == 0);

    fail_receipt = false;
    queue_full = true;
    finish();
    reload();
    standin_pass(61000);
    queue_full = false;
    standin_alarm(2);
    assert(standin()->notices == 0 && standin_alarms() == 0);

    /* A refused completion write is retried before any notice is posted. */
    standin_reset();
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(1500000);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_alarm(COUNTDOWN_ALARM);
    assert(standin()->notices == 0 && state.ends_at != 0);
    reload();
    standin()->refuse_writes = 0;
    app_on_alarm(COUNTDOWN_ALARM);
    assert(standin()->notices == 1 && state.ends_at == 0);
    return 0;
}
