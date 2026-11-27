// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../stopwatch.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    standin_clock(1800000000);
    stopwatch_t original = { .version = STATE_VERSION, .is_running = 1, .banked_ms = 37000,
        .started_uptime_ms = time_uptime_ms(), .boot = time_boot_id(), .started_at = time_now() };
    assert(kv_set("state", 5, &original, sizeof original) == HOST_OK);
    int32_t writes = standin()->store_writes;
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load();
    assert(restore_failed && !state.is_running && elapsed_ms() == 0);
    assert(standin()->refused_reads == 1);
    app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_OK_LONG_PRESSED);
    app_on_intent(HOST_INTENT_STARTED);
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    app_on_updated();
    app_on_timer(RETRY_TIMER);
    app_on_alarm(MINUTE_ALARM);
    app_on_terminate();
    assert(standin()->store_writes == writes && standin()->refused_writes == 0);
    int32_t length;
    const uint8_t *bytes = standin_value("state", &length);
    assert(bytes && length == sizeof original && memcmp(bytes, &original, sizeof original) == 0);
    assert(!change_pending && !save_pending && standin_alarms() == 0);

    /* Clearing a fault alone does not authorize replacing the unread state. */
    standin()->refuse_reads = HOST_OK;
    app_on_button(BTN_OK_PRESSED);
    app_on_terminate();
    assert(standin()->store_writes == writes);
    standin_pass(5000);
    app_on_load();
    assert(!restore_failed && state.is_running && elapsed_ms() == 42000);
    app_on_button(BTN_OK_PRESSED);
    assert(!state.is_running && state.banked_ms == 42000 && standin()->store_writes == writes + 1);

    standin_reset();
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    app_on_terminate();
    assert(restore_failed && standin_value("state", NULL) == NULL);
    standin()->refuse_reads = HOST_OK;
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    assert(!restore_failed && state.is_running && standin()->store_writes == 1);
    return 0;
}
