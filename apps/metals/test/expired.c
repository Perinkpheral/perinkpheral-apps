// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../metals.c"
#include "standin.h"
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    int sends = standin()->sends;
    assert(sends == 1);
    app_on_blur(); app_on_terminate();
    standin()->request_count = 0; /* An interrupted raw response is dropped. */
    standin_pass(3600000);
    app_on_load(); app_on_focus();
    assert(standin()->sends == sends + 1);
}
