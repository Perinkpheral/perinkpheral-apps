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

    /* Reported failure after a committed write must match the reloaded picture. */
    standin_reset();
    app_on_load();
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin()->write_failure_mode = STANDIN_WRITE_COMMIT;
    app_on_button(BTN_OK_PRESSED);
    assert(state.is_running && !save_pending && !change_pending);
    app_on_terminate();
    standin()->refuse_writes = HOST_OK;
    app_on_load();
    assert(state.is_running);

    /* If confirmation cannot be read, later retries must preserve the unknown value. */
    standin_reset();
    app_on_load();
    assert(save());
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin()->write_failure_mode = STANDIN_WRITE_COMMIT;
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_button(BTN_OK_PRESSED);
    assert(restore_failed && !state.is_running);
    int32_t writes = standin()->store_writes;
    standin()->refuse_writes = standin()->refuse_reads = HOST_OK;
    app_on_button(BTN_OK_PRESSED);
    app_on_timer(RETRY_TIMER);
    app_on_terminate();
    assert(standin()->store_writes == writes);
    app_on_load();
    assert(!restore_failed && !state.is_running);
    return 0;
}
