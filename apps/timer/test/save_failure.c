// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../timer.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    app_on_load();
    app_on_button(BTN_UP_PRESSED);
    assert(state.minutes == 26);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_DOWN_PRESSED);
    assert(state.minutes == 26); /* Do not display an edit that was not saved. */
    assert(standin_timer_left(1) == 1000);
    standin()->refuse_writes = 0;
    app_on_timer(1);
    app_on_load();
    assert(state.minutes == 25);
    app_on_button(BTN_OK_PRESSED);
    int32_t end = state.ends_at;
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_PRESSED);
    assert(state.ends_at == end && standin_alarm_left(COUNTDOWN_ALARM) > 0);
    /* Also retry just before unloading, even though this was a stop. */
    standin()->refuse_writes = 0;
    app_on_terminate();
    app_on_load();
    assert(state.ends_at == 0 && standin_alarms() == 0);

    /* Upgrade an existing version-3 countdown without changing its deadline. */
    int32_t legacy[] = {3, 17, uptime_seconds() + 1020, (int32_t)time_boot_id()};
    assert(kv_set("state", 5, legacy, sizeof legacy) == HOST_OK);
    app_on_load();
    assert(state.minutes == 17 && state.ends_at == legacy[2]);
    app_on_updated();
    assert(standin_alarm_left(COUNTDOWN_ALARM) == 1020);
    return 0;
}
