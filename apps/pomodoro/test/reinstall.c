// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../pomodoro.c"
#include "standin.h"

static void reinstall(void)
{
    standin_leave();
    /* Removal clears alarms and status, but leaves the store and boot id. */
    memset(standin()->alarms, 0, sizeof standin()->alarms);
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    standin()->is_new = true;
    state = saved = (state_t){0};
    has_saved = false;
    standin_load();
}

int main(void)
{
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load,
        .on_installed = app_on_installed, .on_focus = app_on_focus, .on_alarm = app_on_alarm };
    standin_reset();
    standin_pass(1000);
    standin_load();
    assert(standin_alarms() == 0);
    standin_focus();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(61000);
    int32_t writes = standin()->store_writes;
    reinstall();
    assert(standin()->store_writes == writes);
    assert(standin_alarm_left(END_ALARM) == 1439);
    assert(standin_alarm_left(MINUTE_ALARM) == 59);
    assert(strcmp(standin()->status_text[HOST_HEADER_RIGHT], "24 min") == 0);
    standin_pass(59000);
    standin_alarm(MINUTE_ALARM);
    assert(strcmp(standin()->status_text[HOST_HEADER_RIGHT], "23 min") == 0);
    standin_pass(1380000);
    assert(standin_alarm_due(END_ALARM));
    standin_alarm(END_ALARM);
    assert(standin()->notices == 1 && state.phase == BREAK && seconds_left() == 300);
    assert(standin_alarm_left(END_ALARM) == 300);

    app_on_button(BTN_OK_PRESSED);
    reinstall();
    assert(state.paused_left == 300 && standin_alarms() == 0);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_PAUSE);
    app_on_button(BTN_UP_PRESSED);
    reinstall();
    assert(standin_alarms() == 0 && standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE);

    app_on_button(BTN_OK_PRESSED);
    standin_pass(1501000);
    reinstall();
    assert(state.ends_at == 0 && standin_alarms() == 0);
    assert(standin()->notices == 1);
    assert(standin()->status_icon[HOST_HEADER_RIGHT] == HOST_ICON_NONE);
    int32_t length;
    const uint8_t *stored = standin_value("state", &length);
    assert(stored && length == sizeof state);
    assert(!memcmp(stored, &state, sizeof state));
    reinstall();
    assert(standin_alarms() == 0 && standin()->notices == 1 && state.ends_at == 0);
    return 0;
}
