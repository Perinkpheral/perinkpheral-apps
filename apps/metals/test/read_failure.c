// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../metals.c"
#include "standin.h"

int main(void)
{
    standin_reset(); app_on_load();
    month_t original = { .version = STATE_VERSION, .count = 1,
        .days = { { .date = 20774, .prices = { 100, 200, 300, 400 } } } };
    assert(app_state_save(STATE_KEY, &original, sizeof original));
    request_t original_request = { .boot = time_boot_id(), .retry_at = 600000 };
    assert(app_state_save("request", &original_request, sizeof original_request));
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load(); assert(!month_ready && !request_ready && month.count == 0);
    standin()->refuse_reads = HOST_OK; /* Writes are healthy after the failed restore. */
    const char body[] = "<Metall>"
        "<Record Date=\"17.11.2026\" Code=\"1\"><Buy>500</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"2\"><Buy>600</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"3\"><Buy>700</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"4\"><Buy>800</Buy></Record></Metall>";
    int writes = standin()->store_writes;
    app_on_focus(); app_on_button(BTN_OK_PRESSED);
    app_on_data(REQUEST, (const uint8_t *)body, sizeof body - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_on_blur(); app_on_terminate();
    assert(standin()->store_writes == writes);
    month_t actual;
    assert(app_state_load(STATE_KEY, &actual, sizeof actual, STATE_VERSION) == HOST_OK);
    assert(!memcmp(&actual, &original, sizeof actual));
    request_t actual_request;
    assert(kv_get("request", 7, &actual_request, sizeof actual_request) == sizeof actual_request);
    assert(!memcmp(&actual_request, &original_request, sizeof actual_request));
    app_on_load(); assert(month_ready && request_ready && month.days[0].prices[0] == 100);
    app_on_data(REQUEST, (const uint8_t *)body, sizeof body - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_on_load(); assert(month.days[0].prices[0] == 50000);
}
