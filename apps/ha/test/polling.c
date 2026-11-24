// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../ha.c"
#include "standin.h"
int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin()->is_session_writable = true;
    assert(standin_subject.poll_count == 1); /* The OS supplies reconnect, replay and the sleep floor. */
    request_source_t existing = { .pairing = pairing(), .tag = HOST_REQUEST_FIRST, .page = 2 };
    assert(app_state_save("source", &existing, sizeof existing));
    app_on_load();
    char migrated[256];
    assert(standin_poll_url(0, -1, migrated, sizeof migrated) && strstr(migrated, "page=2"));
    standin_reset();
    standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    assert(standin()->sends == 0 && standin_alarms() == 0);
    const char body[] = "page|1|3\nhero|21|sunny";
    standin_data(0, -1, 200, body, sizeof body - 1, 1, STANDIN_WHOLE, 0);
    assert(!strcmp(screen, body));
    app_on_blur();
    /* A failed old timestamp save cannot cause another focus request. */
    standin()->refuse_writes = HOST_ERR_FAILED;
    standin_pass(300000); app_on_load(); app_on_focus();
    assert(standin()->sends == 0);
    standin()->refuse_writes = HOST_OK;
    app_on_button(BTN_DOWN_PRESSED);
    assert(standin()->sends == 1 && source.page == 2);
    char url[256];
    assert(standin_poll_url(0, -1, url, sizeof url) && strstr(url, "page=2"));
    int old = request_screen();
    app_on_button(BTN_DOWN_PRESSED);
    standin_data(old, -1, 200, body, sizeof body - 1, 1, STANDIN_WHOLE, 0);
    assert(!strcmp(screen, body) && current_page == 3);
    standin_data(0, -1, 200, body, sizeof body - 1, 1, STANDIN_WHOLE, 0);
    assert(!strcmp(screen, body) && current_page == 3); /* A scheduled reply prepared before the press. */
    return 0;
}
