// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define kv_get read_value
#include "../pomodoro.c"
#undef kv_get
#include "standin.h"

int32_t kv_get(const char *key, int32_t key_length, void *value, int32_t capacity);
static int32_t state_reads, fail_read;
int32_t read_value(const char *key, int32_t key_length, void *value, int32_t capacity)
{
    if (key_length == 5 && memcmp(key, "state", 5) == 0 && ++state_reads == fail_read) {
        return HOST_ERR_FAILED;
    }
    return kv_get(key, key_length, value, capacity);
}

static void protect_state(bool legacy)
{
    standin_reset();
    state_t original = { .version = legacy ? 1 : STATE_VERSION, .phase = BREAK, .round = 2,
        .rounds = 4, .boot = time_boot_id(), .paused_left = 240, .phase_length = 300,
        .notice_until = 60 };
    int32_t size = legacy ? 40 : (int32_t)sizeof original;
    assert(kv_set("state", 5, &original, size) == HOST_OK);
    int32_t writes = standin()->store_writes;
    state_reads = 0;
    fail_read = legacy ? 1 : 0;
    standin()->refuse_reads = legacy ? HOST_OK : HOST_ERR_FAILED;
    app_on_load();
    assert(restore_failed && state_reads == (legacy ? 1 : 0));
    assert(state.phase == WORK && state.round == 1 && !state.ends_at && !state.paused_left);
    app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_UP_PRESSED);
    app_on_button(BTN_OK_LONG_PRESSED);
    app_on_settings();
    app_on_alarm(END_ALARM);
    app_on_alarm(NOTICE_ALARM);
    app_on_installed();
    app_on_updated();
    app_on_timer(RETRY_TIMER);
    app_on_terminate();
    assert(standin()->store_writes == writes && standin()->refused_writes == 0);
    int32_t length;
    const uint8_t *bytes = standin_value("state", &length);
    assert(bytes && length == size && memcmp(bytes, &original, (size_t)size) == 0);
    assert(!state.ends_at && !save_pending && !receipt_pending && standin_alarms() == 0);

    fail_read = state_reads = 0;
    standin()->refuse_reads = HOST_OK;
    app_on_load();
    assert(!restore_failed && state.phase == BREAK && state.paused_left == 240);
    app_on_button(BTN_OK_PRESSED);
    assert(!state.paused_left && seconds_left() == 240 && standin()->store_writes == writes + 1);
}

int main(void)
{
    protect_state(false);
    protect_state(true);
    standin_reset();
    state_reads = 0;
    fail_read = 0;
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load();
    app_on_button(BTN_DOWN_PRESSED);
    app_on_terminate();
    assert(restore_failed && standin_value("state", NULL) == NULL);
    fail_read = state_reads = 0;
    standin()->refuse_reads = HOST_OK;
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    assert(!restore_failed && seconds_left() == 1500 && standin()->store_writes == 1);
    return 0;
}
