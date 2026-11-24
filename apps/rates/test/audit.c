// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../rates.c"
#include "../history.c"
#include "standin.h"
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load();
    for (int i = 0; i < 5; i++) { app_on_focus(); app_on_blur(); app_on_load(); }
    assert(standin()->polls_run[0] == 0); /* OS scheduling owns empty-state recovery. */
    standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_button(BTN_OK_PRESSED); assert(rotating);
    app_on_blur(); app_on_terminate(); app_on_load(); app_on_focus(); assert(rotating);
    standin()->refuse_writes = HOST_OK;
    app_on_terminate(); app_on_load(); assert(rotating);
    return 0;
}
