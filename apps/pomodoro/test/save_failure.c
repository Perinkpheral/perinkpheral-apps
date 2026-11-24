// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../pomodoro.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    app_on_load();
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_PRESSED);
    assert(!state.ends_at && standin_alarms() == 0);
    standin()->refuse_writes = 0;
    app_on_timer(1);
    app_on_load();
    assert(seconds_left() == 1500);
    standin_pass(30000);
    int64_t end = state.ends_at;
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_OK_PRESSED);
    assert(state.ends_at == end && !state.paused_left);
    standin()->refuse_writes = 0;
    app_on_terminate();
    app_on_load();
    assert(state.paused_left == 1470 && !state.ends_at && standin_alarms() == 0);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_DOWN_PRESSED);
    assert(state.phase == WORK && state.paused_left == 1470);
    standin()->refuse_writes = 0;
    app_on_timer(1);
    app_on_load();
    assert(state.phase == BREAK && state.paused_left == 300);
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_button(BTN_UP_PRESSED);
    assert(state.phase == BREAK && state.paused_left == 300);
    standin()->refuse_writes = 0;
    app_on_terminate();
    app_on_load();
    assert(state.phase == WORK && !state.ends_at && !state.paused_left);

    /* Preserve the version-1 phase and deadline on package upgrade. */
    struct {
        int32_t version, phase, round, rounds;
        int64_t ends_at;
        uint32_t boot;
        int32_t paused_left, phase_length;
    } legacy = {1, WORK, 2, 4, uptime_seconds() + 600, time_boot_id(), 0, 1500};
    assert(kv_set("state", 5, &legacy, sizeof legacy) == HOST_OK);
    app_on_load();
    assert(state.phase == WORK && state.round == 2 && seconds_left() == 600);
    return 0;
}
