// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define notification_post queued_notice
#include "../pomodoro.c"
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
static void reload(void) { state = saved = (state_t){0}; app_on_load(); }
static void start_work(void)
{
    standin_reset();
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(1500000);
}
int main(void)
{
    queue_full = true;
    start_work();
    app_on_alarm(END_ALARM);
    assert(state.phase == BREAK && standin()->notices == 0 && standin_alarm_left(2) == 1);
    reload();
    standin_pass(1000);
    queue_full = false;
    fail_receipt = true;
    app_on_alarm(2);
    assert(standin()->notices == 1 && !strcmp(standin()->last_notice.first, "Break"));
    assert(standin_alarm_left(2) == 0);
    /* While this instance survives, retry only the refused receipt. */
    app_on_timer(RETRY_TIMER);
    assert(standin()->notices == 1);
    app_on_terminate();
    assert(standin()->refuse_writes == HOST_ERR_NO_SPACE);
    reload();
    app_on_alarm(END_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
    /* Update recovery of an ambiguous receipt is covered in notification_update.c. */

    fail_receipt = false;
    queue_full = true;
    start_work();
    app_on_alarm(END_ALARM);
    reload();
    standin_pass(61000);
    queue_full = false;
    standin_alarm(2);
    assert(standin()->notices == 0 && standin_alarm_left(2) == 0);

    /* A refused phase write must not announce or advance an unrecorded phase. */
    start_work();
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_alarm(END_ALARM);
    assert(state.phase == WORK && standin()->notices == 0);
    assert(standin_alarm_left(END_ALARM) == 1);
    reload();
    standin_pass(1000);
    standin()->refuse_writes = 0;
    app_on_alarm(END_ALARM);
    assert(state.phase == BREAK && standin()->notices == 1);
    reload();
    app_on_alarm(END_ALARM);
    assert(standin()->notices == 1);

    /* The foreground retry also announces once, without waiting for another alarm. */
    start_work();
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_alarm(END_ALARM);
    standin()->refuse_writes = 0;
    app_on_timer(1);
    assert(state.phase == BREAK && standin()->notices == 1);
    return 0;
}
