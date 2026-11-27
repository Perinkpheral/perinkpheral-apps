// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ui_draw_text_lines measured_lines
#define ui_draw_text measured_text
#define ui_text_height measured_height
#include "../calendar.c"
#include "../ics.c"
#undef ui_draw_text_lines
#undef ui_draw_text
#undef ui_text_height
#include "standin.h"

/* Heights from the firmware's Departure (2x/3x/1x) and Jersey 15/25/10 faces.
 * This checks layout bounds without rasterizing or running the simulator. */
static const int heights[2][3] = { {30, 45, 15}, {28, 43, 21} };
static int family, natural_lines, title_bottom, row_bottom, rows;
int32_t measured_height(int32_t style) { return heights[family][style]; }
int32_t measured_lines(int32_t x, int32_t y, int32_t style, const char *text,
                       int32_t length, int32_t width, int32_t maximum)
{
    (void)x; (void)text; (void)length; (void)width;
    int lines = natural_lines < maximum ? natural_lines : maximum;
    assert(maximum == ((configured & (configured - 1)) ? 3 : 4));
    title_bottom = row_bottom = y + lines * measured_height(style);
    return lines;
}
int32_t measured_text(int32_t x, int32_t y, int32_t style, const char *text,
                      int32_t length, int32_t width)
{
    (void)x; (void)text; (void)length; (void)width;
    assert(y >= row_bottom + HOST_UI_MARGIN / 4);
    row_bottom = y + measured_height(style);
    assert(row_bottom <= gfx_height() - HOST_UI_ACTIONS_HEIGHT);
    rows++;
    return 0;
}

int main(void)
{
    standin_reset();
    standin_handlers.on_draw = app_on_draw;
    standin_clock(1768478400);
    app_on_load();
    stored.count = 1;
    stored.events[0] = (event_t){ .uid = 1, .start = time_now() + 3600, .end = time_now() + 7200 };
    strcpy(sources[0].titles, "A title with up to four rows");
    strcpy(sources[0].name, "Design team");
    is_reading_one = true; read_uid = 1; read_start = stored.events[0].start;
    for (family = 0; family < 2; family++) {
        for (natural_lines = 1; natural_lines <= 4; natural_lines++) {
            for (int multiple = 0; multiple < 2; multiple++) {
                configured = multiple ? 3 : 1;
                rows = 0;
                standin_draw();
                assert(rows == (multiple ? 3 : 2));
                if (natural_lines <= 3) {
                    assert(title_bottom == HOST_UI_MARGIN + natural_lines * heights[family][HOST_UI_TEXT]);
                }
            }
        }
    }
    /* Navigation keeps wrap-around and clamped page movement for a selected list. */
    is_reading_one = false;
    list = (app_list_t){ .count = 12, .selected = 0 };
    page_rows = 5;
    app_on_button(BTN_UP_PRESSED); assert(list.selected == 11);
    app_on_button(BTN_DOWN_PRESSED); assert(list.selected == 0);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(list.selected == 5 && list.first == 5);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(list.selected == 10 && list.first == 10);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(list.selected == 11 && list.first == 11);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(list.selected == 11 && list.first == 11);
    app_on_button(BTN_UP_LONG_PRESSED); assert(list.selected == 6 && list.first == 6);
    for (int action = 0; action < 16; action++) {
        if (action == BTN_PRESSED || action == BTN_LONG_PRESSED) { continue; }
        int redraws = standin()->redraws;
        app_on_button(BTN_EVENT(BTN_UP, action));
        app_on_button(BTN_EVENT(BTN_DOWN, action));
        assert(list.selected == 6 && standin()->redraws == redraws);
    }
    int writes = standin()->store_writes;
    state.status = -123;
    app_on_intent(HOST_INTENT_NETWORK_UP);
    app_on_intent(99);
    assert(state.status == -123 && standin()->store_writes == writes);
    puts("calendar: 1-4 title rows, both font metrics, calendar caption bounds and paging passed");
}
