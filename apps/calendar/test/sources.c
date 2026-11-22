// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ui_draw_text record_text
#include "../calendar.c"
#include "../ics.c"
#undef ui_draw_text
#include "standin.h"

int32_t ui_draw_text(int32_t, int32_t, int32_t, const char *, int32_t, int32_t);
static char caption[ICS_NAME_MAX + 1];
static int caption_count;
int32_t record_text(int32_t x, int32_t y, int32_t style, const char *text, int32_t length, int32_t width)
{
    if (style == HOST_UI_CAPTION) {
        app_text_copy(caption, sizeof caption, text);
        caption_count++;
    }
    return ui_draw_text(x, y, style, text, length, width);
}

static const char first[] =
    "BEGIN:VCALENDAR\r\nX-WR-CALNAME:Work\\, team\r\n"
    "BEGIN:VEVENT\r\nUID:shared\r\nDTSTART:20260115T140000Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Meeting\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:late\r\nDTSTART:20260115T160000Z\r\nDTEND:20260115T170000Z\r\nSUMMARY:Review\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static const char second[] =
    "BEGIN:VCALENDAR\r\n"
    "BEGIN:VEVENT\r\nUID:early\r\nDTSTART:20260115T130000Z\r\nDTEND:20260115T133000Z\r\nSUMMARY:Lunch\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:shared\r\nDTSTART:20260115T140000Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Duplicate\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:shared\r\nDTSTART:20260115T143000Z\r\nDTEND:20260115T150000Z\r\nSUMMARY:Another occurrence\r\nEND:VEVENT\r\n"
    "X-WR-CALNAME:Семейный\r\n календарь\\nДом\r\nEND:VCALENDAR\r\n";
static const char empty[] = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
static const char unnamed[] =
    "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:fourth\r\nDTSTART:20260115T150000Z\r\nDTEND:20260115T153000Z\r\nSUMMARY:Walk\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static void answer(int source, const char *data, int chunk)
{
    standin_data(source, -1, 200, data, (int32_t)strlen(data), chunk, STANDIN_WHOLE, 0);
}
static void reload(void)
{
    memset(sources, 0, sizeof sources);
    memset(&stored, 0, sizeof stored);
    memset(&state, 0, sizeof state);
    app_on_load();
}
static void detail(int row)
{
    is_reading_one = true;
    read_uid = stored.events[row].uid;
    read_start = stored.events[row].start;
    caption_count = 0;
    standin_draw();
}
static void setup(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_draw = app_on_draw;
    standin_clock(1768478400); /* 2026-01-15 12:00 UTC */
    standin()->zone_offset = 0;
    standin_set("feed", "https://calendar.invalid/work");
    standin_set("feed_2", "https://calendar.invalid/home");
    standin_set("remind", "At start");
    reload();
}

int main(void)
{
    setup();
    /* Reverse delivery order; folded/escaped UTF-8 names may follow events. */
    answer(1, second, 1);
    reload();
    answer(0, first, 7);
    assert(stored.count == 4);
    assert(!strcmp(stored.events[0].title, "Lunch"));
    assert(!strcmp(stored.events[1].title, "Meeting"));
    assert(!strcmp(stored.events[2].title, "Another occurrence"));
    assert(!strcmp(stored.events[3].title, "Review"));
    assert(stored.events[0].source == 1 && stored.events[1].source == 0);
    assert(!strcmp(sources[0].name, "Work, team"));
    assert(!strcmp(sources[1].name, "Семейныйкалендарь Дом"));
    detail(0);
    assert(caption_count == 2 && !strcmp(caption, sources[1].name));
    detail(1);
    assert(caption_count == 2 && !strcmp(caption, "Work, team"));
    uint64_t frame = standin_draw();
    int writes = standin()->file_writes;
    answer(1, second, 1000);
    answer(0, first, 1);
    assert(writes == standin()->file_writes && stored.count == 4);
    /* Empty optional polls neither overwrite success nor disturb the open event. */
    for (int source = 2; source < 4; source++) {
        app_on_data(source, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NOT_READY);
    }
    assert(state.status == 200 && frame == standin_draw());
    assert(writes == standin()->file_writes);
    app_on_button(BTN_OK_LONG_PRESSED);
    assert(standin()->polls_run[0] == 1 && standin()->polls_run[1] == 1);
    assert(standin()->polls_run[2] == 0 && standin()->polls_run[3] == 0);

    /* Restart, session selection and alarms include both calendars. */
    app_on_focus(); list.selected = 0;
    is_reading_one = false;
    standin()->is_session_writable = true; app_on_blur();
    reload(); app_on_focus(); app_on_button(BTN_OK_PRESSED);
    assert(read_uid == stored.events[0].uid);
    standin_restart(); reload(); app_on_intent(HOST_INTENT_STARTED);
    assert(standin_alarm_left(0) == 3600);
    standin_clock(1768482000); app_on_alarm(0);
    assert(standin()->notices == 1 && !strcmp(standin()->last_notice.detail, "Lunch"));
    standin_clock(1768485600); app_on_alarm(0);
    assert(standin()->notices == 2 && !strcmp(standin()->last_notice.detail, "Meeting"));

    /* A failed/partial feed keeps both snapshots; a valid empty one removes only its own. */
    standin_clock(1768478400);
    app_on_data(1, (const uint8_t *)second, 40, HOST_DATA_FIRST, 200);
    reload(); assert(stored.count == 4);
    app_on_data(1, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_TIMEOUT);
    assert(stored.count == 4);
    answer(1, empty, 1);
    assert(stored.count == 2 && stored.events[0].source == 0);
    answer(1, second, 7);
    answer(0, empty, 1);
    assert(stored.count == 3 && stored.events[1].source == 1); /* duplicate survives in source 2 */

    /* Sparse configuration: source 4 works without source 1; a single source has no caption. */
    standin_set("feed", ""); standin_set("feed_2", "");
    standin_set("feed_4", "https://calendar.invalid/four"); app_on_settings();
    answer(3, unnamed, 1);
    assert(stored.count == 1 && stored.events[0].source == 3);
    detail(0); assert(caption_count == 1); /* date only */
    app_on_data(0, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NOT_READY);
    assert(state.status == 200);
    standin_set("feed_3", "https://calendar.invalid/three"); app_on_settings();
    answer(3, unnamed, 1);
    detail(0); assert(caption_count == 2 && !strcmp(caption, "Calendar 4"));
    answer(2, first, 1); assert(stored.count == 3);
    standin_set("feed_4", ""); app_on_settings();
    answer(2, first, 1);
    assert(stored.count == 2);
    detail(0); assert(caption_count == 1);
    standin_set("feed_3", ""); app_on_settings();
    app_on_data(2, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NOT_READY);
    assert(stored.count == 0 && state.status == HOST_FAILED_NOT_READY);

    /* Third and fourth polls also contribute independent alarms at the same time. */
    setup();
    standin_set("feed_3", "https://calendar.invalid/three");
    standin_set("feed_4", "https://calendar.invalid/four"); app_on_settings();
    answer(0, first, 1); answer(1, second, 7); answer(2, unnamed, 1);
    char fourth[sizeof unnamed]; memcpy(fourth, unnamed, sizeof fourth);
    memcpy(strstr(fourth, "UID:fourth") + 4, "unique", 6);
    answer(3, fourth, 7);
    assert(stored.count == 6);
    for (int i = 0; i < SOURCES_MAX; i++) state.reminded[i] = 1768488900; /* 14:55 UTC */
    standin_clock(1768489200); /* 15:00 UTC */
    app_on_alarm(0);
    assert(standin()->notices == 2); /* one event from each of sources 3 and 4 */

    /* The existing single-calendar file and reminder state migrate without refetching. */
    setup(); answer(0, first, 1);
    stored.version = 3;
    assert(file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, NULL, 0) == HOST_OK);
    assert(file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &stored, sizeof stored) == HOST_OK);
    reload(); assert(stored.count == 2 && sources[0].name[0] == '\0');

    /* Full source snapshots retain candidates displaced by another calendar's nearest 24. */
    for (int source = 0; source < SOURCES_MAX; source++) {
        standin_set(FEEDS[source], "https://calendar.invalid/full");
    }
    app_on_settings();
    for (int source = 0; source < SOURCES_MAX; source++) {
        sources[source].data.count = ICS_EVENTS_MAX;
        for (int i = 0; i < ICS_EVENTS_MAX; i++) {
            sources[source].data.events[i] = (ics_event_t){
                .uid = (uint32_t)(source * 100 + i + 1),
                .start = time_now() + 100 + source * 1000 + i,
                .end = time_now() + 200 + source * 1000 + i
            };
        }
    }
    merge_events(); assert(stored.count == ICS_EVENTS_MAX && stored.events[23].source == 0);
    save_events(); reload(); answer(0, empty, 1);
    assert(stored.count == ICS_EVENTS_MAX && stored.events[0].source == 1 && stored.events[23].source == 1);

    /* Charge every setting at its longest, including NVS rounding and per-key overhead. */
    char address[256]; memset(address, 'a', sizeof address - 1); address[255] = '\0';
    memcpy(address, "https://", 8);
    for (int source = 0; source < SOURCES_MAX; source++) standin_set(FEEDS[source], address);
    standin_set("every", "30 min"); standin_set("remind", "At start");
    save_state();
    assert(standin_store_bytes() == 2016);
    assert(sizeof sources == 15904 && sizeof cache == 15920);
    assert(standin()->refused_what[0] == '\0');
    printf("calendar: sources, dedup, names, optional polls, reminders, migration and full snapshots passed; KV %d/2048 bytes; cache %zu bytes\n", standin_store_bytes(), sizeof cache);
}
