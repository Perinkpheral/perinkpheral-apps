// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../air.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    app_on_load();
    standin()->refuse_writes = HOST_ERR_FAILED;
    const standin_pair_t headers[] = { { "Retry-After", "600" } };
    standin_answer_headers(headers, 1);
    standin_picked(0, -1, 429, "{}", 2);
    app_on_button(BTN_OK_PRESSED);
    assert(!standin_poll_take(0));
    app_on_terminate(); standin_forget_session(); app_on_load(); app_on_focus();
    standin_pass(599999);
    assert(!standin_poll_take(0));
    standin()->refuse_writes = HOST_OK;
    standin_pass(1);
    assert(standin_poll_take(0));
    int32_t length;
    assert(standin_value("cooldown", &length) == NULL);
    return 0;
}
