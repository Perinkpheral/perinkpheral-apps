// SPDX-License-Identifier: MIT
#include <assert.h>
#define notification_post queued_notice
#include "../timer.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);
/* The stand-in has no queue capacity. Count the host's four slots, initially
 * occupied by other apps, and inject storage refusal at the posting boundary. */
static int queue_used;
static bool refuse_after_post;
int32_t queued_notice(const host_notice_t *notice)
{
    int32_t result = queue_used == 4 ? HOST_ERR_NO_SPACE : notification_post(notice);
    if (result == HOST_OK) { queue_used++; }
    if (refuse_after_post) { standin()->refuse_writes = HOST_ERR_NO_SPACE; }
    return result;
}
static void discard_instance(void)
{
    state = pending = (timer_state_t){0};
    save_pending = false; /* app_on_load resets the instance retry flags. */
}
static void load(void) { discard_instance(); standin_load(); }
static void unload(void)
{
    standin_terminate();
    discard_instance();
    assert(!standin()->is_loaded && standin_timers() == 0);
}
static void background_alarm(int32_t id)
{
    load();
    assert(!standin()->is_on_screen && standin_alarm_due(id));
    standin_alarm(id);
    unload();
}
static void update(void)
{
    load();
    standin_updated(); /* discard the old package's alarms, then restore them */
    unload();
}
static void start(void)
{
    standin_reset();
    queue_used = 4;
    refuse_after_post = false;
    load();
    standin_focus();
    standin_press(BTN_OK);
    standin_leave();
    discard_instance();
    standin_pass(1500000);
}
static void failed_delivery(void)
{
    start();
    refuse_after_post = true;
    background_alarm(COUNTDOWN_ALARM);
    assert(standin()->refuse_writes == HOST_ERR_NO_SPACE);
    assert(standin()->notices == 0 && standin_alarm_left(NOTICE_ALARM) == 1);
    timer_state_t stored;
    assert(kv_get("state", 5, &stored, sizeof stored) == sizeof stored);
    assert(stored.notice_until > uptime_seconds());
}
static void recover(bool replace_package)
{
    failed_delivery();
    /* Storage stays refused across termination, fresh loads, and another
     * background retry. An update must recover the same durable deadline. */
    if (replace_package) {
        update();
        assert(standin_alarm_left(NOTICE_ALARM) == 1);
    }
    standin_pass(1000);
    background_alarm(NOTICE_ALARM);
    assert(standin()->refuse_writes == HOST_ERR_NO_SPACE && standin()->notices == 0);
    assert(standin_alarm_left(NOTICE_ALARM) == 1);
    refuse_after_post = false;
    standin()->refuse_writes = 0;
    queue_used = 0;
    standin_pass(1000);
    background_alarm(NOTICE_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
    update();
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
}
int main(void)
{
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_focus = app_on_focus, .on_button = app_on_button,
        .on_alarm = app_on_alarm, .on_updated = app_on_updated,
        .on_timer = app_on_timer, .on_terminate = app_on_terminate };
    recover(false);
    recover(true);

    failed_delivery();
    standin_pass(61000);
    refuse_after_post = false;
    standin()->refuse_writes = 0;
    queue_used = 0;
    background_alarm(NOTICE_ALARM);
    update();
    assert(standin()->notices == 0 && standin_alarm_left(NOTICE_ALARM) == 0);

    /* The host cannot atomically post and persist a receipt. Refusal after
     * acceptance stays refused through unload and update: recovery may replay
     * that ambiguous delivery, but must never lose a refused delivery. */
    start();
    queue_used = 0;
    refuse_after_post = true;
    background_alarm(COUNTDOWN_ALARM);
    assert(standin()->notices == 1 && standin_alarm_left(NOTICE_ALARM) == 0);
    assert(standin()->refuse_writes == HOST_ERR_NO_SPACE);
    update();
    assert(standin_alarm_left(NOTICE_ALARM) == 1);
    standin_pass(1000);
    background_alarm(NOTICE_ALARM);
    assert(standin()->notices == 2 && standin()->refuse_writes == HOST_ERR_NO_SPACE);
    standin_pass(61000);
    update();
    assert(standin()->notices == 2 && standin_alarm_left(NOTICE_ALARM) == 0);
    return 0;
}
