// SPDX-License-Identifier: MIT
/* Inspect the detail renderer's actual caption, including optional precision. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ui_draw_text record_text
#include "../launch.c"
#undef ui_draw_text
#include "standin.h"
#include "fixtures.h"

static int mentions;
int32_t ui_draw_text(int32_t, int32_t, int32_t, const char *, int32_t, int32_t);
int32_t record_text(int32_t x, int32_t y, int32_t style, const char *text, int32_t length, int32_t width)
{
    const char *needle = word(STR_TIME_UNKNOWN);
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) { mentions++; }
    return ui_draw_text(x, y, style, text, length, width);
}

int main(void)
{
    standin_reset(); standin_handlers.on_draw = app_on_draw; app_on_load();
    answer_text(POLL_UPCOMING, -1, 200, "{\"count\":1,\"results\":[{"
        "\"id\":\"00000000-0000-0000-0000-000000000001\",\"name\":\"Soyuz | Mission\","
        "\"net\":\"2026-11-25T10:00:00Z\",\"net_precision\":null}]}");
    place.nav.screen = SCREEN_LAUNCHES; place.nav.depth = 1;
    static const uint8_t PRECISIONS[] = { PRECISION_UNKNOWN, 3, PRECISION_MONTH, 14 };
    for (size_t i = 0; i < sizeof PRECISIONS / sizeof PRECISIONS[0]; i++) {
        cache.lists[0][0].precision = PRECISIONS[i]; mentions = 0;
        standin_draw(); assert(mentions == 1);
    }
    cache.lists[0][0].precision = PRECISION_HOUR; mentions = 0;
    standin_draw(); assert(mentions == 0);
    puts("space-launch: unknown time appears once in the detail caption");
    return 0;
}
