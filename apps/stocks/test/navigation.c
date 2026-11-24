// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../stocks.c"
#include "standin.h"
int main(void)
{
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
