// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../stopwatch.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    app_on_load();
    app_on_button(BTN_OK_PRESSED);
    standin_pass(1000);
    standin()->redraw_flags = 0;
    app_on_timer(TURN_TIMER);
    assert(standin()->redraw_flags == HOST_REDRAW_NO_BLINK);
    standin()->redraw_flags = 0;
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->redraw_flags == HOST_REDRAW_CLEAN);

    app_on_button(BTN_OK_PRESSED);
    app_on_timer(TURN_TIMER);
    standin_pass(3600000);
    standin()->redraw_flags = 0;
    app_on_timer(TURN_TIMER);
    assert(standin()->redraw_flags == HOST_REDRAW_CLEAN);
    standin()->redraw_flags = 0;
    app_on_timer(TURN_TIMER);
    assert(standin()->redraw_flags == HOST_REDRAW_NORMAL);
    return 0;
}
