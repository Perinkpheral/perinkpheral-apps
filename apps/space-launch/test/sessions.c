// SPDX-License-Identifier: MIT
/* A refused stack snapshot must be diagnosed; successful snapshots retain the view. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../launch.c"
#include "standin.h"
#include "fixtures.h"

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    app_on_button(BTN_DOWN_LONG_PRESSED);
    place_t before = place;
    app_on_blur();
    app_on_load(); app_on_focus();
    assert(!memcmp(&place, &before, sizeof place));
    uint64_t diagnostics = standin()->diagnostics;
    standin_refuse_session(HOST_ERR_NO_SPACE);
    app_on_blur();
    assert(standin_refused_sessions() == 1);
    assert(standin()->diagnostics != diagnostics);
    puts("space-launch: saved views resume and refused session allocation is diagnosed");
    return 0;
}
