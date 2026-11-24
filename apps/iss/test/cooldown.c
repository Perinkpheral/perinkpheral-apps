// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../iss.c"
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
