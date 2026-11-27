// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../stocks.c"
#include "standin.h"
static void headers_and_wrap(void)
{
    standin_reset();
    standin_set("tickers", "AAPL\nIBM\nMSFT");
    app_on_load(); app_on_focus();
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3"));
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3 Auto"));
    app_on_button(BTN_OK_PRESSED);
    const int up[] = { BTN_UP_PRESSED, BTN_UP_LONG_PRESSED };
    const int down[] = { BTN_DOWN_PRESSED, BTN_DOWN_LONG_PRESSED };
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]);
        assert(state.index == 2 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "3/3"));
        app_on_button(down[held]);
        assert(state.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/3"));
    }
    app_on_button(BTN_DOWN_PRESSED);
    standin_set("tickers", "AAPL\nIBM"); app_on_settings();
    assert(state.index == 1 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], "2/2"));
    standin_set("tickers", "AAPL"); app_on_settings();
    assert(state.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]); app_on_button(down[held]);
        assert(state.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    }
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], app_string(STR_AUTO)));
    assert(!standin_timer_left(ROTATE_TIMER));
    app_on_button(BTN_OK_PRESSED);
    standin_set("tickers", ""); app_on_settings();
    assert(state.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    for (int held = 0; held < 2; held++) {
        app_on_button(up[held]); app_on_button(down[held]);
        assert(state.index == 0 && !strcmp(standin()->header_text[HOST_HEADER_LEFT], " "));
    }
    app_on_button(BTN_OK_PRESSED);
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], app_string(STR_AUTO)));
    assert(!standin_timer_left(ROTATE_TIMER));
}

int main(void)
{
    headers_and_wrap();
    standin_reset(); standin()->is_session_writable = true;
    standin_set("tickers", "AAPL\nIBM");
    app_on_load(); app_on_focus();
    int writes = standin()->store_writes;
    app_on_button(BTN_DOWN_PRESSED);
    app_on_blur(); app_on_terminate();
    assert(standin()->store_writes == writes);
    app_on_load(); app_on_focus(); assert(state.index == 1);
    app_on_button(BTN_OK_PRESSED);
    app_on_blur(); app_on_terminate();
    app_on_load(); app_on_focus(); assert(state.index == 1 && state.rotating);
    standin_forget_session(); app_on_load(); app_on_focus();
    assert(state.index == 0 && state.rotating);
    /* A pre-update record's selected ticker must not survive reopening either. */
    state_t legacy = { .version = STATE_VERSION, .index = 1, .rotating = 1 };
    assert(app_state_save(STATE_KEY, &legacy, sizeof legacy));
    app_on_load(); app_on_focus(); assert(state.index == 0 && state.rotating);
}
