// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
/* Stand in the documented scheduler: only a changed resolved interval
 * moves its deadline. standin_pass() alone never delivers scheduled polls. */
static int scheduler_interval = 60;
static long long scheduler_due;
int scheduled_interval(int poll, int seconds);
#define poll_set_interval scheduled_interval
#include "../iss.c"
#undef poll_set_interval
int32_t poll_set_interval(int32_t poll, int32_t seconds);
#include "../orbit.c"
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
    int resolved = seconds ? seconds : 60;
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
    app_on_button(BTN_OK_PRESSED);
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    standin_handlers.on_picked = app_on_picked;
    app_on_load(); app_on_focus();
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
        app_on_blur(); app_on_load(); app_on_focus();
        triggers();
        assert(requests() == before);
        standin_pass((int64_t)seconds[round] * 1000 - 1);
        app_on_blur(); app_on_load(); app_on_focus();
        triggers();
        assert(requests() == before);
        standin_pass(1);
        app_on_button(BTN_OK_PRESSED);
        assert(requests() == before + 1);
    }

    /* Positive Retry-After also applies to a service-unavailable response. */
    reply(503, "120");
    int before = requests();
    app_on_blur(); app_on_load(); app_on_focus();
    triggers(); assert(requests() == before);
    standin_pass(120000);
    app_on_button(BTN_OK_PRESSED); assert(requests() == before + 1);

    /* Uptime deadlines from a previous boot must never block a new boot. */
    reply(429, "300");
    standin()->boot_id++;
    app_on_blur(); app_on_load(); app_on_focus();
    before = requests();
    app_on_button(BTN_OK_PRESSED); assert(requests() == before + 1);
    puts("iss: Retry-After across refresh, lifecycle, chained refusals and clock changes passed");
    return 0;
}
