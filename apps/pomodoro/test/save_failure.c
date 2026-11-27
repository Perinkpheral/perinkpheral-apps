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

    /* Reported failure after a committed write must match the reloaded picture. */
    standin_reset();
    app_on_load();
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin()->write_failure_mode = STANDIN_WRITE_COMMIT;
    app_on_button(BTN_OK_PRESSED);
    assert(state.ends_at && !save_pending);
    end = state.ends_at;
    app_on_terminate();
    standin()->refuse_writes = HOST_OK;
    app_on_load();
    assert(state.ends_at == end);

    /* If confirmation cannot be read, later retries must preserve the unknown value. */
    standin_reset();
    app_on_load();
    assert(save());
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin()->write_failure_mode = STANDIN_WRITE_COMMIT;
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_button(BTN_OK_PRESSED);
    assert(restore_failed && !state.ends_at);
    int32_t writes = standin()->store_writes;
    standin()->refuse_writes = standin()->refuse_reads = HOST_OK;
    app_on_button(BTN_DOWN_PRESSED);
    app_on_settings();
    app_on_timer(RETRY_TIMER);
    app_on_terminate();
    assert(standin()->store_writes == writes);
    app_on_load();
    assert(!restore_failed && !state.ends_at);
    return 0;
}
