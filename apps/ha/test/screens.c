// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ha.c"
#include "standin.h"

static void answer(const char *body)
{
    standin_data(request_screen(), -1, 200, body, (int32_t)strlen(body), 1, STANDIN_WHOLE, 0);
}

static void asked(int page)
{
    char wanted[] = "/api/perinkpheral/screen?page=1";
    wanted[sizeof wanted - 2] = (char)('0' + page);
    assert(current_page == page && has_asked);
    assert(standin()->request_count > 0);
    assert(strstr(standin()->requests[standin()->request_count - 1].url, wanted));
    /* Manual deliveries below stand in for the queue draining between handlers. */
    standin()->request_count = standin()->handler_requests = standin()->handler_request_bytes = 0;
}

int main(void)
{
    standin_reset();
    standin()->is_session_writable = true;
    standin_handlers.on_data = app_on_data;
    app_on_load(); app_on_focus(); asked(1);
    answer("page|1|3\nhero|21|sunny");
    assert(page_count == 3 && shown_page == 1);
    int32_t old_request = request_screen();
    app_on_button(BTN_DOWN_PRESSED); asked(2);
    assert(shown_page == 1 && !strcmp(screen, "page|1|3\nhero|21|sunny"));
    standin_data(old_request, -1, 200, "page|1|3\nhero|99|", 17, 1, STANDIN_WHOLE, 0);
    assert(current_page == 2 && shown_page == 1);
    answer("page|2|3\nlayout|tiles\nitem|Room|thermometer|22.5°");
    assert(shown_page == 2 && layout_asked() == LAYOUT_TILES);
    item_t items[ITEMS_MAX];
    assert(read_items(items) == 1);
    app_on_button(BTN_OK_PRESSED); asked(2);
    app_on_alarm(0); asked(2);

    /* Scene restore and background refresh use the selected page without writing the session. */
    app_on_blur();
    place_t before, after;
    assert(app_session_load(&before, sizeof before) && before.page == 2);
    standin()->is_session_writable = false;
    app_on_load(); app_on_alarm(0); asked(2);
    assert(app_session_load(&after, sizeof after));
    assert(before.page == after.page && before.first == after.first);
    app_on_load();
    answer("page|2|3\nlayout|single\nitem|CO2|co2|650");
    standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    assert(current_page == 2 && shown_page == 2 && layout_asked() == LAYOUT_SINGLE);
    app_on_button(BTN_UP_PRESSED); asked(1);
    answer("page|1|3\nhero|21|sunny");
    app_on_button(BTN_UP_PRESSED); asked(3);
    answer("page|3|3\nhero|23|sunny");
    app_on_button(BTN_DOWN_PRESSED); asked(1);
    answer("page|1|3\nhero|21|sunny");

    /* A page switch keeps a scrolled list intact until a complete replacement arrives. */
    answer("page|1|3\nlayout|list\nitem|A||1");
    list.first = 4; list.count = 12;
    app_on_button(BTN_DOWN_PRESSED); asked(2);
    assert(list.first == 4 && shown_page == 1);
    app_on_blur();
    standin()->is_session_writable = false;
    app_on_load();
    answer("page|2|3\nlayout|list\nitem|B||2");
    standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    assert(current_page == 2 && shown_page == 2 && list.first == 0);
    app_on_button(BTN_UP_PRESSED); asked(1);
    answer("page|1|3\nhero|21|sunny");

    /* An interrupted reply cannot change either the picture or its page metadata. */
    app_on_data(request_screen(), (const uint8_t *)"page|1|1\n", 9, HOST_DATA_FIRST, 200);
    assert(page_count == 3);
    app_on_data(request_screen(), NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
    assert(page_count == 3);

    /* Removal at HA clamps the selection in both foreground and background deliveries. */
    app_on_button(BTN_UP_PRESSED); asked(3);
    answer("page|2|2\nhero|22|sunny");
    assert(current_page == 2 && page_count == 2);
    app_on_blur();
    standin()->is_session_writable = false;
    app_on_load();
    answer("page|1|1\nhero|21|sunny");
    standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    assert(current_page == 1 && page_count == 1);

    answer("page|1|4\nhero|21|sunny");
    app_on_button(BTN_UP_PRESSED); asked(4);
    answer("page|4|4\nhero|24|sunny");
    assert(shown_page == 4 && page_count == 4);
    app_on_button(BTN_DOWN_PRESSED); asked(1);

    /* An older integration keeps list navigation and always gets page 1. */
    answer("layout|list\nitem|A||1\nitem|B||2\nitem|C||3");
    assert(page_count == 1 && shown_page == 1);
    list.count = 12;
    int sends = standin()->sends;
    app_on_button(BTN_DOWN_PRESSED);
    assert(list.first > 0 && standin()->sends == sends);
    app_on_alarm(0); asked(1);
    for (int i = 0; i < 5; i++) {
        const char *bad[] = {"page|0|3\nhero|1|", "page|2|1\nhero|1|", "page|1|5\nhero|1|",
                             "page|1|33\nhero|1|", "page|x|3\nhero|1|"};
        answer(bad[i]);
        assert(page_count == 1 && current_page == 1);
    }

    answer("page|1|3\nlayout|list\nitem|A||1");
    list.count = 12; list.first = 0;
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(list.first > 0);
    app_on_button(BTN_DOWN_PRESSED); asked(2);
    answer("page|2|3\nhero|22|sunny");
    app_on_blur();
    standin_forget_session();
    has_asked = false;
    /* Opened anew it asks for the screen it was left on, the one its kept picture shows: no
     * picture of screen 2 followed by a jump to screen 1 (the owner, 2026-11-23). */
    app_on_load(); app_on_focus(); asked(2);
    answer("page|2|3\nhero|22|sunny");
    app_on_button(BTN_DOWN_PRESSED); asked(3);
    answer("page|3|3\nhero|23|sunny");
    app_on_blur();
    standin_restart();
    has_asked = false;
    app_on_load(); app_on_intent(HOST_INTENT_STARTED); asked(3);
    puts("ha: page queries, wrap, refresh, legacy lists, stale replies, sessions, background delivery, removal and restart passed");
}
