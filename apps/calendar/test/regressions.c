// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define notification_post queued_notice
#include "../calendar.c"
#include "../ics.c"
#undef notification_post
#include "standin.h"

int32_t notification_post(const host_notice_t *notice);
static int capacity;
int32_t queued_notice(const host_notice_t *notice)
{
    if (capacity == 0) { return HOST_ERR_NO_SPACE; }
    capacity--;
    return notification_post(notice);
}

static const char empty[] = "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n";
static const char instant[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:instant\r\nDTSTART:20260115T140000Z\r\nSUMMARY:Instant\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static const char brief[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:brief\r\nDTSTART:20260115T140000Z\r\nDURATION:PT30S\r\nSUMMARY:Brief\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static const char soon[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:soon\r\nDTSTART:20260115T140500Z\r\nDURATION:PT1H\r\nSUMMARY:Soon\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
static const int64_t start = 1768485600;

static void reload(void)
{
    memset(&cache, 0, sizeof cache);
    memset(&state, 0, sizeof state);
    memset(&stored, 0, sizeof stored);
    events_dirty = false;
    is_reading = false;
    app_on_load();
}
static void setup(void)
{
    standin_reset();
    standin_clock(start - 60);
    standin()->zone_offset = 0;
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_draw = app_on_draw;
    standin_set("feed", "https://calendar.invalid/one");
    standin_set("feed_2", "https://calendar.invalid/two");
    standin_set("remind", "At start");
    capacity = 100;
    reload();
}
static void answer(int source, const char *body)
{
    standin_data(source, -1, HTTP_OK, body, (int32_t)strlen(body), 7, STANDIN_WHOLE, 0);
}
static void failure(int source, int status)
{
    app_on_data(source, NULL, 0, status > 0 ? HOST_DATA_FIRST | HOST_DATA_LAST : HOST_DATA_FAILED, status);
}

static void ended_reminders(void)
{
    /* Real DTSTART-only input, loaded off screen at its alarm, and once more late. */
    for (int delay = 0; delay <= LATE_SECONDS + 1; delay += 1) {
        if (delay != 0 && delay != 1 && delay != LATE_SECONDS && delay != LATE_SECONDS + 1) { continue; }
        setup(); answer(0, instant);
        assert(stored.count == 1 && stored.events[0].start == stored.events[0].end);
        standin_clock(start + delay); reload(); app_on_intent(HOST_INTENT_STARTED);
        assert(count_visible_events(time_now()) == 0);
        assert(standin_alarm_left(0) == (delay <= LATE_SECONDS ? 1 : 0));
        app_on_alarm(0);
        assert(standin()->notices == (delay <= LATE_SECONDS ? 1 : 0));
        reload(); app_on_alarm(0);
        assert(standin()->notices == (delay <= LATE_SECONDS ? 1 : 0));
    }
    /* A poll at start must not erase a DTSTART-only reminder either. */
    setup(); standin_clock(start); answer(0, instant); reload(); app_on_alarm(0);
    assert(standin()->notices == 1);

    /* Partial queue acceptance survives unload, an ended event and a refresh. */
    setup(); answer(0, instant); answer(1, brief);
    standin_clock(start); reload(); capacity = 1; app_on_alarm(0);
    assert(standin()->notices == 1 && state.delivered_count == 1 && standin_alarm_left(0) == 60);
    standin_clock(start + 60); reload(); answer(1, brief);
    assert(count_visible_events(time_now()) == 0);
    capacity = 1; app_on_alarm(0);
    assert(standin()->notices == 2 && !strcmp(standin()->last_notice.detail, "Brief"));
    reload(); app_on_alarm(0); assert(standin()->notices == 2);
}

static void source_statuses(void)
{
    for (int failing = 0; failing < 2; failing++) {
        setup();
        answer(1 - failing, empty); failure(failing, 404);
        assert(state.status == 404);
        int writes = standin()->store_writes, files = standin()->file_writes;
        uint64_t frame = standin_draw();
        /* The host creates a fresh instance for each off-screen delivery. */
        for (int round = 0; round < 12; round++) {
            reload(); answer(1 - failing, empty);
            assert(state.status == 404 && standin_draw() == frame);
            reload(); failure(failing, 404);
        }
        assert(standin()->store_writes == writes && standin()->file_writes == files);
        failure(1 - failing, HOST_FAILED_NO_NETWORK);
        assert(state.status == HOST_FAILED_NO_NETWORK);
        failure(failing, 403); assert(state.status == HOST_FAILED_NO_NETWORK);
        reload(); failure(1 - failing, HOST_FAILED_TIMEOUT); assert(state.status == 403);
        reload(); answer(failing, empty); assert(state.status == HOST_FAILED_TIMEOUT);
        failure(1 - failing, HOST_FAILED_NOT_READY); assert(state.status == HOST_FAILED_NO_ANSWER);
        answer(1 - failing, empty); assert(state.status == HTTP_OK);
        /* Events remain the screen's first choice even with another failed source. */
        answer(0, soon); uint64_t events = standin_draw();
        writes = standin()->store_writes;
        failure(1, HOST_FAILED_NO_NETWORK);
        assert(state.status == HTTP_OK && standin_draw() == events);
        assert(standin()->store_writes == writes);
        reload();
        assert(state.status == HTTP_OK && standin()->store_writes == writes);
        standin_clock(start + 4000); app_on_timer(0);
        assert(state.status == HOST_FAILED_NO_NETWORK && standin()->store_writes == writes + 1);
        standin_set("feed", ""); standin_set("feed_2", ""); app_on_settings();
        assert(configured == 0 && state.status == HOST_FAILED_NOT_READY);
    }
    /* Changing a lower-priority error persists that source, without rewriting KV. */
    setup(); failure(0, HOST_FAILED_NO_NETWORK); failure(1, 404);
    int writes = standin()->store_writes;
    failure(1, 403); assert(standin()->store_writes == writes);
    reload(); answer(0, empty); assert(state.status == 403);
}

static void source_identity(void)
{
    setup(); standin_set("remind", "10 min"); app_on_settings();
    answer(0, soon); app_on_alarm(0);
    assert(standin()->notices == 1);
    int64_t watermark = state.reminded[0];
    uint32_t identity = state.source_hash[0];
    for (int i = 1; i < SOURCES_MAX; i++) {
        standin_set(FEEDS[i], "https://calendar.invalid/added"); app_on_settings(); reload();
        assert(sources[0].data.count == 1 && state.source_hash[0] == identity && state.reminded[0] == watermark);
        answer(0, soon); app_on_alarm(0); assert(standin()->notices == 1);
        /* A new source has its own progress, even for an already-due reminder. */
        answer(i, brief); app_on_alarm(0); assert(standin()->notices == 2);
        standin_set(FEEDS[i], "https://calendar.invalid/replaced"); app_on_settings(); reload();
        assert(sources[i].data.count == 0 && state.reminded[i] == 0);
        assert(sources[0].data.count == 1 && state.reminded[0] == watermark);
        standin_set(FEEDS[i], ""); app_on_settings(); reload();
        answer(0, soon); app_on_alarm(0); assert(standin()->notices == 2);
        standin()->notices = 1; /* next independent addition */
    }
    int writes = standin()->store_writes, files = standin()->file_writes;
    app_on_settings(); reload();
    assert(standin()->store_writes == writes && standin()->file_writes == files);

    /* An event already announced through one calendar is not announced again when a calendar in
     * an earlier slot that has it too is added. */
    setup(); standin_set("remind", "10 min"); standin_set("feed", ""); app_on_settings();
    answer(1, soon); app_on_alarm(0); assert(standin()->notices == 1);
    standin_set("feed", "https://calendar.invalid/same"); app_on_settings(); reload();
    answer(0, soon); app_on_alarm(0); assert(standin()->notices == 1);

    /* Preserve accepted identities while another calendar's queue delivery is pending. */
    setup(); answer(0, instant); answer(1, brief);
    standin_clock(start); capacity = 1; app_on_alarm(0);
    standin_set("feed_4", "https://calendar.invalid/added"); app_on_settings(); reload();
    standin_clock(start + 60); capacity = 1; app_on_alarm(0);
    assert(standin()->notices == 2 && !strcmp(standin()->last_notice.detail, "Brief"));
}

static void reminder_choices(void)
{
    setup(); answer(0, soon);
    static const char *const choices[] = { "Off", "At start", "5 min", "10 min", "15 min", "30 min", "" };
    static const int32_t seconds[] = { -1, 0, 300, 600, 900, 1800, 600 };
    for (int i = 0; i < 7; i++) {
        standin_set("remind", choices[i]);
        app_on_settings();
        int64_t wait = stored.events[0].start - time_now() - seconds[i];
        assert(standin_alarm_left(0) == (seconds[i] < 0 ? 0 : wait > 0 ? wait : 1));
    }
}

static void previous_state(void)
{
    setup(); answer(0, instant); answer(1, brief);
    standin_clock(start); capacity = 1; app_on_alarm(0);
    assert(state.delivered_count == 1);
    uint32_t hashes[SOURCES_MAX];
    state.feed_hash = feed_hash(hashes);
    state.reminded_to = start - 1;
    state.version = 4;
    assert(kv_set(STATE_KEY, sizeof STATE_KEY - 1, &state, offsetof(state_t, source_hash)) == HOST_OK);
    assert(file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, NULL, 0) == HOST_OK);
    assert(file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, sources, sizeof sources) == HOST_OK);
    reload();
    assert(state.version == STATE_VERSION && state.delivered_count == 1);
    assert(state.source_hash[0] == hashes[0] && state.source_hash[1] == hashes[1]);
    assert(state.reminded[0] == start - 1 && state.reminded[1] == start - 1);
    capacity = 1; app_on_alarm(0);
    assert(standin()->notices == 2 && !strcmp(standin()->last_notice.detail, "Brief"));
}

/* An installed 0.4.20: one feed, its 408-byte version-4 state kept, its version-3 events file discarded. */
static void upgrade_from_0_4(void)
{
    static const char feed[] = "https://calendar.invalid/one";
    standin_reset();
    standin_clock(start - 3600);
    standin()->zone_offset = 0;
    standin_set("feed", feed);
    standin_set("remind", "10 min");
    capacity = 100;
    uint32_t feed_identity = 2166136261u; /* 0.4.20's FNV-1a of the address */
    for (const char *at = feed; *at != '\0'; at++) { feed_identity = (feed_identity ^ (uint8_t)*at) * 16777619u; }
    _Static_assert(offsetof(state_t, source_hash) == 408, "0.4.20 state length");
    state_t old = { .version = 4, .status = HTTP_OK, .reminded_to = start - 7200, .feed_hash = feed_identity };
    assert(kv_set(STATE_KEY, sizeof STATE_KEY - 1, &old, 408) == HOST_OK);
    struct { int32_t version, count; uint8_t events[3840]; } file = { .version = 3, .count = 2 };
    assert(file_write(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, &file, (int32_t)sizeof file) == HOST_OK);
    int writes = standin()->store_writes, files = standin()->file_writes;
    reload();
    assert(sources[0].data.version == EVENTS_VERSION && sources[0].data.count == 0 && stored.count == 0);
    assert(state.source_hash[0] == feed_identity && state.reminded[0] == start - 7200 && state.status == HTTP_OK);
    assert(standin()->store_writes == writes && standin()->file_writes == files);
}

int main(void)
{
    ended_reminders();
    source_statuses();
    source_identity();
    previous_state();
    upgrade_from_0_4();
    reminder_choices();
    puts("calendar: ended/reminder reloads, per-source status/write stability, isolated link changes passed");
}
