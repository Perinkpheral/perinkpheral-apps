// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

static void reply(int32_t status, const char *delay)
{
    const standin_pair_t headers[] = { { "Retry-After", delay } };
    standin_answer_headers(headers, 1);
    standin_picked(0, -1, status, "{}", 2);
}

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin()->is_session_writable = true;
    /* A server delay must never rewrite the user's configured interval. */
    standin()->refuse_intervals = HOST_ERR_FAILED;
    app_on_load(); app_on_focus();
    const int32_t statuses[] = { 429, 503, 429 };
    const char *delays[] = { "120", "300", "" };
    const int32_t seconds[] = { 120, 300, 60 };
    for (int32_t round = 0; round < 3; round++) {
        reply(statuses[round], delays[round]);
        int32_t requested = standin()->polls_run[0];
        app_on_button(BTN_OK_PRESSED);
        /* Accept the refresh now; Perinkpheral OS keeps it pending. */
        assert(standin()->polls_run[0] == requested + 1);
        assert(!standin_poll_take(0));
        reply(429, "1"); /* A shorter refusal cannot shorten the deadline. */
        app_on_terminate(); standin_forget_session(); app_on_load(); app_on_focus();
        standin_clock(standin()->now + 86400);
        standin_pass((int64_t)seconds[round] * 1000 - 1);
        app_on_button(BTN_OK_PRESSED);
        assert(!standin_poll_take(0));
        standin_pass(1000); /* Also let the accepted-call window elapse. */
        assert(standin_poll_take(0));
        assert(standin()->poll_interval[0] == 0 && !standin()->refused_intervals);
        int32_t length;
        assert(standin_value("cooldown", &length) == NULL);
    }
    puts("iss: firmware defers refreshes across unload and clock changes without interval overrides");
    return 0;
}
