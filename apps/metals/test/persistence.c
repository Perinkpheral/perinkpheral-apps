// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define answer_info retry_info
#include "../metals.c"
#undef answer_info
#include "standin.h"

int32_t retry_info(host_answer_info_t *info, int32_t capacity)
{
    assert(capacity == sizeof *info);
    *info = (host_answer_info_t){ .retry_after = 600 };
    return sizeof *info;
}
int main(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load, .on_focus = app_on_focus };
    standin_load(); standin_focus();
    int sends = standin()->sends;
    app_on_blur(); pending = false; app_on_load(); app_on_focus();
    assert(standin()->sends == sends); /* The outstanding request belongs to the app, not this instance. */
    app_on_button(BTN_OK_PRESSED); assert(standin()->sends == sends);
    standin_forget_session(); standin_load(); standin_focus();
    assert(standin()->sends == sends); /* It also survives reopening without a UI session. */
    app_on_data(REQUEST, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 429);
    app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends);
    app_on_blur(); app_on_load(); app_on_focus(); app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends);
    standin_pass(60000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends);
    standin_pass(540000); app_on_button(BTN_OK_PRESSED);
    assert(standin()->sends == sends + 1);

    const char *body = "<Metall><Record Date=\"17.11.2026\" Code=\"1\"><Buy>100</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"2\"><Buy>200</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"3\"><Buy>300</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"4\"><Buy>400</Buy></Record></Metall>";
    standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_data(REQUEST, (const uint8_t *)body, (int)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(month.count > 0 && month_unsaved);
    month_t shown = month;
    app_on_blur(); month = (month_t){0}; month_unsaved = false;
    app_on_load(); app_on_focus();
    assert(!memcmp(&shown, &month, sizeof month));
    standin()->refuse_writes = HOST_OK;
    app_on_terminate(); app_on_load();
    assert(!memcmp(&shown, &month, sizeof month));
    return 0;
}
