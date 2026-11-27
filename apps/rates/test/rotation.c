// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rates.c"
#include "../history.c"
#include "standin.h"

static void reload(void)
{
    app_on_blur();
    navigation = (navigation_t){0};
    timer_set(ROTATE_TIMER, 0);
    app_on_load();
    app_on_focus();
}

int main(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_load = app_on_load, .on_focus = app_on_focus };
    standin_load(); standin_focus();
    standin_set("codes", "USD\nEUR\nJPY");
    navigation_t orphan = { .index = 2 };
    assert(kv_set("navigation", 10, &orphan, sizeof orphan) == HOST_OK);
    app_on_updated();
    assert(standin_value("navigation", NULL) == NULL);
    const char *paces[] = { "2 min", "5 min", "10 min" };
    const int minutes[] = { 2, 5, 10 };
    for (int p = 0; p < 3; ++p) {
        standin_forget_session();
        standin_set("rotate", paces[p]);
        standin_load();
        standin_focus();
        int writes_before = standin()->store_writes;
        app_on_button(BTN_DOWN_PRESSED);
        app_on_button(BTN_OK_PRESSED);
        assert(navigation.index == 1 && rotating);
        int64_t due = standin()->timer_due_ms[ROTATE_TIMER];
        for (int minute = 1; minute < minutes[p]; ++minute) {
            standin_pass(60000);
            reload();
            assert(navigation.index == 1 && rotating);
            assert(standin()->timer_due_ms[ROTATE_TIMER] == due);
        }
        standin_pass(60000);
        reload();
        assert(standin_timer_left(ROTATE_TIMER) == 1);
        standin_pass(1);
        app_on_timer(ROTATE_TIMER);
        assert(navigation.index == 2);
        app_on_timer(ROTATE_TIMER);
        assert(navigation.index == 2);
        for (int toggle = 0; toggle < 20; ++toggle) app_on_button(BTN_OK_PRESSED);
        reload();
        assert(standin()->store_writes == writes_before + 21);
        standin_forget_session();
        standin_load();
        standin_focus();
        assert(navigation.index == 0 && rotating && standin_timers());
        standin_restart();
        standin_load(); standin_focus();
        assert(navigation.index == 0 && rotating && standin_timers());
        app_on_button(BTN_OK_PRESSED);
        assert(!rotating && !standin_timers());
        standin_forget_session();
        standin_load(); standin_focus();
        assert(!rotating && !standin_timers());
    }
    puts("Rates: rotation keeps its deadline through minute-by-minute unloads; only rotation toggles write flash and survive reopening/restart");
    return 0;
}
