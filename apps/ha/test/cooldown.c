// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../ha.c"
#include "standin.h"

static int requests(void) { return standin()->sends + standin()->polls_run[0]; }

static void reply(int status, const char *delay)
{
    const standin_pair_t headers[] = { { "Retry-After", delay } };
    const char body[] = "page|1|3\nhero|21|sunny";
    standin_answer_headers(headers, 1);
    standin_data(0, -1, status, body, sizeof body - 1, 3, STANDIN_WHOLE, 0);
}

static void triggers(void)
{
    app_on_settings();
    app_on_intent(HOST_INTENT_STARTED); app_on_updated(); app_on_alarm(0);
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_DOWN_LONG_PRESSED); app_on_button(BTN_UP_LONG_PRESSED);
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    standin_handlers.on_data = app_on_data;
    app_on_load(); app_on_focus();
    reply(200, "0");
    /* An in-flight page may be superseded before its server refusal arrives. */
    app_on_button(BTN_DOWN_LONG_PRESSED);
    int32_t old_page = request_screen();
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(old_page != request_screen());
    const standin_pair_t headers[] = { { "Retry-After", "120" } };
    standin_answer_headers(headers, 1);
    standin_data(old_page, -1, 429, "busy", 4, 1, STANDIN_WHOLE, 0);
    int queued = requests();
    app_on_button(BTN_OK_PRESSED);
    assert(requests() == queued);
    standin_pass(120000);

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
    puts("ha: Retry-After across refresh, lifecycle, chained refusals and clock changes passed");
    return 0;
}
