// SPDX-License-Identifier: MIT
#include <assert.h>
#define alarm_set admit_alarm
#include "../pomodoro.c"
#undef alarm_set
#include "standin.h"

int32_t alarm_set(int32_t id, int32_t seconds);
static bool refused;
int32_t admit_alarm(int32_t id, int32_t seconds)
{
    return refused && id == END_ALARM && seconds > 0 ? HOST_ERR_NO_SPACE : alarm_set(id, seconds);
}
int main(void)
{
    standin_reset();
    app_on_load();
    refused = true;
    app_on_button(BTN_OK_PRESSED);
    assert(!state.ends_at && standin_alarms() == 0 && standin()->store_writes == 0);
    refused = false;
    app_on_timer(1);
    assert(state.ends_at > 0 && standin_alarm_left(END_ALARM) == 1500);
    return 0;
}
