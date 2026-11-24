// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../stopwatch.c"
#include "standin.h"

static void retry_start(int64_t initial_clock, int64_t correction)
{
    standin_reset();
    standin_clock(initial_clock);
    app_on_load();
    standin_pass(2000);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_PRESSED);
    assert(!state.is_running && change_pending);
    standin_pass(5000);
    standin_clock(1800000000 + correction);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    app_on_timer(RETRY_TIMER);
    assert(!state.is_running && change_pending);
    standin_pass(3000);
    standin()->refuse_writes = 0;
    app_on_timer(RETRY_TIMER);
    assert(state.is_running && !change_pending && elapsed_ms() == 8000);
    assert(state.started_at == 1799999995 + correction);

    /* Unload, reboot, and recover using the anchor saved by the retry. */
    state = (stopwatch_t){0};
    standin()->boot_id++;
    standin()->uptime_ms = 0;
    standin_clock(1800000013 + correction);
    app_on_load();
    assert(state.is_running && elapsed_ms() == 18000);
}
int main(void)
{
    retry_start(0, 0);
    retry_start(1799999995, 3600);
    retry_start(1799999995, -3600);
    return 0;
}
