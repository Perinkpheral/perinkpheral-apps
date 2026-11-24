// SPDX-License-Identifier: MIT
#include <assert.h>
#define answer_info retry_info
#include "../metals.c"
#undef answer_info
#include "standin.h"
int32_t retry_info(host_answer_info_t *info, int32_t capacity)
{
    assert(capacity == sizeof *info);
    *info = (host_answer_info_t){ .retry_after = 600 };
    return sizeof *info;
}
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    int sends = standin()->sends;
    standin()->request_count = 0;
    standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_data(REQUEST, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 429);
    app_on_blur(); app_on_terminate();
    standin_pass(331000);
    app_on_load(); app_on_focus(); app_on_button(BTN_OK_PRESSED); app_on_alarm(ALARM);
    assert(standin()->sends == sends);
    standin()->refuse_writes = HOST_OK;
    app_on_terminate(); standin_forget_session(); app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends);
    standin_pass(269000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends + 1);
}
