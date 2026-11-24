// SPDX-License-Identifier: MIT
#include <assert.h>
#define notification_post queued_notice
#include "../timer.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);
static bool queue_full, refuse_requeue;
int32_t queued_notice(const host_notice_t *notice)
{
    if (queue_full && refuse_requeue) { standin()->refuse_writes = HOST_ERR_NO_SPACE; }
    return queue_full ? HOST_ERR_NO_SPACE : notification_post(notice);
}
static void update(void)
{
    state = (timer_state_t){0};
    app_on_load();
    standin_updated();
}
static void finish(void)
{
    standin_reset();
    standin_handlers.on_updated = app_on_updated;
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(1500000);
    queue_full = true;
    app_on_alarm(COUNTDOWN_ALARM);
    assert(standin()->notices == 0 && standin_alarm_left(NOTICE_ALARM) == 1);
}
int main(void)
{
    finish();
    standin_pass(1000);
    update();
    assert(standin_alarm_left(NOTICE_ALARM) == 1);
    queue_full = false;
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    standin_pass(1000);
    app_on_alarm(NOTICE_ALARM);
    assert(standin()->notices == 0 && standin_alarm_left(NOTICE_ALARM) == 1);
    state = (timer_state_t){0};
    app_on_load();
    standin()->refuse_writes = 0;
    standin_pass(1000);
    app_on_alarm(NOTICE_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
    update();
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);

    finish();
    standin_pass(61000);
    queue_full = false;
    update();
    assert(standin()->notices == 0 && standin_alarm_left(NOTICE_ALARM) == 0);

    /* A failed requeue write must not leave a tentative snapshot that can
     * resurrect the notice after a successful retry and an update. */
    refuse_requeue = true;
    finish();
    assert(save_pending);
    refuse_requeue = queue_full = false;
    standin()->refuse_writes = 0;
    standin_pass(1000);
    app_on_alarm(NOTICE_ALARM);
    app_on_terminate();
    update();
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
    return 0;
}
