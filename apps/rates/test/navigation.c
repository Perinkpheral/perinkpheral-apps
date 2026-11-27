// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../rates.c"
#include "../history.c"
#include "standin.h"
static void headers_and_wrap(void)
{
    standin_reset();
    standin_set("codes", "USD\nEUR\nJPY");
    app_on_load(); app_on_focus();
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3"));
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3 Auto"));
    app_on_button(BTN_OK_PRESSED);
    const int up[] = { BTN_UP_PRESSED, BTN_UP_LONG_PRESSED };
    const int down[] = { BTN_DOWN_PRESSED, BTN_DOWN_LONG_PRESSED };
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]);
        assert(navigation.index == 2 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "3/3"));
        app_on_button(down[held]);
        assert(navigation.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3"));
    }
    app_on_button(BTN_DOWN_PRESSED);
    standin_set("codes", "USD\nEUR"); app_on_settings();
    assert(navigation.index == 1 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "2/2"));
    standin_set("codes", "USD"); app_on_settings();
    assert(navigation.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]); app_on_button(down[held]);
        assert(navigation.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    }
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], app_string(STR_AUTO)));
    assert(!standin_timer_left(ROTATE_TIMER));
    app_on_button(BTN_OK_PRESSED);
    standin_set("codes", ""); app_on_settings();
    assert(navigation.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]); app_on_button(down[held]);
        assert(navigation.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    }
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], app_string(STR_AUTO)));
    assert(!standin_timer_left(ROTATE_TIMER));
}

int main(void)
{
    headers_and_wrap();
}
