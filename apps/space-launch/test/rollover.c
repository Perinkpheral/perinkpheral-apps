// SPDX-License-Identifier: MIT
/* A December snapshot remains provisional after rollover, including after an unload. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ui_draw_text record_text
#include "../launch.c"
#undef ui_draw_text
#include "standin.h"
#include "fixtures.h"

static int notes;
int32_t ui_draw_text(int32_t, int32_t, int32_t, const char *, int32_t, int32_t);
int32_t record_text(int32_t x, int32_t y, int32_t style, const char *text, int32_t length, int32_t width)
{
    if (length == 16 && !memcmp(text, "2026: Incomplete", 16)) { notes++; }
    return ui_draw_text(x, y, style, text, length, width);
}

int main(void)
{
    standin_reset(); standin_handlers.on_draw = app_on_draw; app_on_load(); answer_all();
    standin_clock(parse_moment("2027-01-01T00:00:00Z"));
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    year_t year;
    assert(cache.year == 2027 && year_of(2026, &year) && year_total(&year) == 271);
    for (int away = 0; away < 2; away++) {
        if (away) { app_on_terminate(); app_on_load(); }
        for (int screen = SCREEN_MONTHS; screen <= SCREEN_HISTORY; screen++) {
            place.nav.screen = screen; place.chosen_year[screen == SCREEN_HISTORY] = 2026;
            notes = 0; standin_draw(); assert(notes == 1);
            place.chosen_year[screen == SCREEN_HISTORY] = 2025;
            notes = 0; standin_draw(); assert(notes == (screen == SCREEN_HISTORY));
        }
    }
    /* A later package can supply the final count; a provisional cache cannot override it. */
    cache.last_year = 2025; cache.last = (year_t){ .months = { 1 } };
    assert(year_of(2025, &year) && year_total(&year) > 1);
    place.nav.screen = SCREEN_MONTHS; place.chosen_year[0] = 2025;
    notes = 0; standin_draw(); assert(notes == 0);
    puts("space-launch: rollover totals are qualified and packaged history wins");
    return 0;
}
