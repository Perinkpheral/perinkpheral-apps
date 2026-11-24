// SPDX-License-Identifier: MIT
#include <assert.h>
#define answer_info retry_info
#include "../weather.c"
#undef answer_info
#include "standin.h"
static int retry_after = 600;
int32_t retry_info(host_answer_info_t *info, int32_t capacity)
{
    assert(capacity == sizeof *info);
    *info = (host_answer_info_t){ .retry_after = retry_after };
    return sizeof *info;
}
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    int polls = standin()->polls_run[WEATHER_POLL];
    standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_picked(WEATHER_POLL, 429);
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[WEATHER_POLL] == polls);
    app_on_blur(); app_on_terminate(); app_on_load(); app_on_focus();
    standin_pass(599000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[WEATHER_POLL] == polls);
    standin()->refuse_writes = HOST_OK;
    app_on_terminate(); standin_forget_session(); app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[WEATHER_POLL] == polls);
    standin_pass(1000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[WEATHER_POLL] == polls + 1);
    retry_after = 0; app_on_picked(WEATHER_POLL, 429);
    app_on_button(BTN_OK_PRESSED); assert(standin()->polls_run[WEATHER_POLL] == polls + 1);
    standin_pass(60000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[WEATHER_POLL] == polls + 2);
}
