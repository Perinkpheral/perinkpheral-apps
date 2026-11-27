// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../metals.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    app_on_load(); app_on_focus();
    const int up[] = { BTN_UP_PRESSED, BTN_UP_LONG_PRESSED };
    const int down[] = { BTN_DOWN_PRESSED, BTN_DOWN_LONG_PRESSED };
    /* Downloaded row counts do not change the four metal screens. */
    const int counts[] = { 0, 1, 10 };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        month.count = counts[i];
        for (int held = 0; held < 2; held++) {
            assert(session.metal == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/4"));
            app_on_button(up[held]);
            assert(session.metal == 3 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "4/4"));
            app_on_button(down[held]);
            assert(session.metal == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/4"));
        }
    }
    standin_set("metal", "Silver"); app_on_settings();
    assert(session.metal == 1 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "2/4"));
}
