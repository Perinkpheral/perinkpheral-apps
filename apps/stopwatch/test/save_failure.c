// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../stopwatch.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(37000);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_PRESSED);
    assert(state.is_running); /* A refused stop cannot be acknowledged. */
    assert(standin_alarm_left(MINUTE_ALARM) > 0 && standin_timer_left(1) == 1000);
    standin()->refuse_writes = 0;
    app_on_timer(1);
    app_on_load();
    assert(!state.is_running && elapsed_ms() == 37000 && standin_alarms() == 0);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_LONG_PRESSED);
    assert(elapsed_ms() == 37000);
    standin()->refuse_writes = 0;
    app_on_terminate();
    app_on_load();
    assert(!state.is_running && elapsed_ms() == 0);
    return 0;
}
