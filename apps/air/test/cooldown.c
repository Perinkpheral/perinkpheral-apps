// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
/* Stand in the documented scheduler: only a changed resolved interval
 * moves its deadline. standin_pass() alone never delivers scheduled polls. */
static int scheduler_interval = 900;
static long long scheduler_due;
int scheduled_interval(int poll, int seconds);
#define poll_set_interval scheduled_interval
#include "../air.c"
#undef poll_set_interval
int32_t poll_set_interval(int32_t poll, int32_t seconds);
#include "standin.h"

static int requests(void) { return standin()->sends + standin()->polls_run[0]; }

static void reply(int status, const char *delay)
{
    const standin_pair_t headers[] = { { "Retry-After", delay } };
    const char body[] = "{}";
    standin_answer_headers(headers, 1);
    standin_picked(0, -1, status, body, sizeof body - 1);
}

int scheduled_interval(int poll, int seconds)
{
    int result = poll_set_interval(poll, seconds);
    int resolved = seconds ? seconds : 900;
    if (result == HOST_OK && resolved != scheduler_interval) {
        scheduler_interval = resolved;
        scheduler_due = time_uptime_ms() + (int64_t)resolved * 1000;
    }
    return result;
}

static int scheduled_deliveries;

static void scheduler_advance(int64_t milliseconds)
{
    int64_t until = time_uptime_ms() + milliseconds;
    while (scheduler_due <= until) {
        standin_pass(scheduler_due - time_uptime_ms());
        scheduler_due += (int64_t)scheduler_interval * 1000;
        scheduled_deliveries++;
        reply(200, "0");
    }
    standin_pass(until - time_uptime_ms());
}

static void scheduled_polls(void)
{
    /* A manual response may arrive just before the next scheduled poll. */
    scheduler_due = time_uptime_ms() + 1000;
    reply(429, "300");
    int before = scheduled_deliveries;
    scheduler_advance(60000);
    assert(scheduled_deliveries == before);
    /* Another identical refusal must start a new full cooldown. */
    reply(429, "300");
    scheduler_advance(299999);
    assert(scheduled_deliveries == before);
    scheduler_advance(scheduler_due - time_uptime_ms());
    assert(scheduled_deliveries == before + 1);
    assert(standin()->poll_interval[0] == 0); /* Resume the configured pace. */
}

static void triggers(void)
{
    app_on_settings();
    app_on_button(BTN_OK_PRESSED);
}

static void refused_cooldown(void)
{
    const int errors[] = { HOST_ERR_FAILED, HOST_ERR_NO_SPACE };
    for (int i = 0; i < 2; i++) {
        /* Retry the refused save when storage recovers before termination. */
        standin()->refuse_writes = errors[i];
        reply(429, "300");
        int before = requests();
        standin()->refuse_writes = HOST_OK;
        app_on_terminate();
        standin_forget_session();
        app_on_load(); app_on_focus();
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before);
        standin_pass(300000);

        /* If storage stays unavailable through unload, keep the deadline in
         * the scene, then persist it once storage returns. */
        standin()->refuse_writes = errors[i];
        reply(429, "300");
        before = requests();
        app_on_blur(); app_on_terminate();
        app_on_load(); app_on_focus();
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before);
        standin()->refuse_writes = HOST_OK;
        app_on_terminate();
        standin_forget_session();
        app_on_load(); app_on_focus();
        standin_pass(299999);
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before);
        standin_pass(1);
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before + 1);
    }
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    standin_handlers.on_picked = app_on_picked;
    standin_set("every", "15 min");
    standin_set("lat", "52.52"); standin_set("lon", "13.41");
    app_on_load(); app_on_focus();
    refused_cooldown();
    scheduled_polls();
    const char *delays[] = { "120", "300" };
    const int seconds[] = { 120, 300 };
    for (int round = 0; round < 2; round++) {
        reply(429, delays[round]);
        int before = requests();
        triggers();
        assert(requests() == before);
        /* A shorter later refusal must not shorten the original deadline. */
        reply(429, "1");
        standin_clock(standin()->now + 86400); /* wall-clock jumps do not waive it */
        app_on_load(); app_on_focus();
        triggers();
        assert(requests() == before);
        standin_pass((int64_t)seconds[round] * 1000 - 1);
        app_on_load(); app_on_focus();
        triggers();
        assert(requests() == before);
        standin_pass(1);
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before + 1);
    }

    /* Positive Retry-After also applies to a service-unavailable response. */
    reply(503, "120");
    int before = requests();
    app_on_load(); app_on_focus();
    triggers(); assert(requests() == before);
    standin_pass(120000);
    app_on_button(BTN_OK_PRESSED); assert(requests() == before + 1);

    /* Uptime deadlines from a previous boot must never block a new boot. */
    reply(429, "300");
    standin()->boot_id++;
    app_on_load(); app_on_focus();
    before = requests();
    app_on_button(BTN_OK_PRESSED); assert(requests() == before + 1);
    puts("air: Retry-After across refresh, lifecycle, chained refusals and clock changes passed");
    return 0;
}
