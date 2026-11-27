// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../kit.c"
#include "standin.h"

int main(void)
{
    assert(sizeof(session_t) <= HOST_SESSION_MAX);
    standin_reset();
    standin()->is_session_writable = true;
    standin_clock(1794902400);
    contents.selected = PAGE_DATE;
    app_on_button(BTN_OK_PRESSED);
    assert(page == PAGE_DATE);
    app_on_button(BTN_UP_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    app_date_t edited = when;
    app_on_blur();
    page = PAGE_CONTENTS;
    when = (app_date_t){0};
    contents.selected = 0;
    app_on_focus();
    assert(page == PAGE_DATE && contents.selected == PAGE_DATE);
    assert(!memcmp(&when, &edited, sizeof when));
    int writes = standin()->store_writes;
    app_on_button(BTN_UP_PRESSED);
    assert(memcmp(&when, &edited, sizeof when));
    app_on_blur();
    assert(standin()->store_writes == writes);
    return 0;
}
