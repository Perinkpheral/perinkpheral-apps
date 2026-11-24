// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../metals.c"
#include "standin.h"

static void answer(void)
{
    const char body[] = "<Metall><Record Date=\"17.11.2026\" Code=\"1\"><Buy>100</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"2\"><Buy>200</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"3\"><Buy>300</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"4\"><Buy>400</Buy></Record></Metall>";
    standin()->request_count = 0;
    app_on_data(REQUEST, (const uint8_t *)body, sizeof body - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
}

int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus(); answer();
    month_t prices = month;
    assert(month.count == 1 && !month_unsaved);
    int writes = standin()->store_changes;
    for (int minute = 0; minute < 3; minute++) {
        int64_t previous_lease = request.pending_until;
        standin_pass(60000); app_on_button(BTN_OK_PRESSED); answer();
        assert(request.pending_until > previous_lease);
        request_t durable;
        assert(kv_get("request", 7, &durable, sizeof durable) == sizeof durable);
        assert(durable.pending_until == request.pending_until);
        assert(!memcmp(&month, &prices, sizeof prices));
        assert(standin()->store_changes == writes + minute + 1);
    }
    /* Reopening with no UI session still observes the persisted request lease. */
    int sends = standin()->sends;
    app_on_terminate(); standin_forget_session(); app_on_load(); app_on_focus();
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends);
}
