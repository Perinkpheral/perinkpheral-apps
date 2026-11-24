// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../metals.c"
#include "standin.h"
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    int sends = standin()->sends;
    app_on_alarm(ALARM);
    assert(standin()->sends == sends);
    app_on_blur(); app_on_terminate(); app_on_load();
    app_on_alarm(ALARM); /* Also coalesce an off-screen alarm. */
    assert(standin()->sends == sends);
    standin()->request_count = 0;
    standin_pass(330000);
    app_on_alarm(ALARM);
    assert(standin()->sends == sends + 1);
}
