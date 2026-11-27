// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define file_read failing_read
#include "../calendar.c"
#include "../ics.c"
#undef file_read
#include "standin.h"

int32_t file_read(const char *, int32_t, int32_t, void *, int32_t);
static bool failed;
int32_t failing_read(const char *name, int32_t length, int32_t offset, void *out, int32_t capacity)
{
    int32_t result = file_read(name, length, offset, out, capacity);
    /* A read can fail after copying part of the cache. Discard that prefix. */
    if (failed && length == sizeof EVENTS_FILE - 1 && !memcmp(name, EVENTS_FILE, length)) {
        memset(out, 0x5a, capacity / 2);
        return HOST_ERR_FAILED;
    }
    return result;
}
static const char feed[] = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:new\r\n"
    "DTSTART:20261118T090000Z\r\nSUMMARY:New\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
int main(void)
{
    standin_reset(); standin_clock(1794955920);
    standin_set("feed", "https://calendar.invalid/feed");
    standin_set("feed_2", "https://calendar.invalid/second");
    app_on_load();
    sources[1].data.count = 1;
    sources[1].data.events[0] = (event_t){ .uid = 7, .start = time_now() + 3600,
        .end = time_now() + 7200, .title = TITLES_BYTES, .source = 1 };
    strcpy(sources[1].titles, "Keep this source");
    save_events();
    unsigned char original[sizeof cache];
    assert(file_read(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, original, sizeof original) == sizeof original);
    failed = true;
    app_on_load(); assert(!events_ready && sources[1].data.count == 0);
    int writes = standin()->file_writes, state_writes = standin()->store_writes;
    app_on_data(0, (const uint8_t *)feed, sizeof feed - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_on_settings(); app_on_intent(HOST_INTENT_STARTED);
    app_on_intent(HOST_INTENT_TIME_CHANGED); app_on_terminate(); app_on_alarm(0);
    assert(standin()->file_writes == writes && standin()->store_writes == state_writes);
    failed = false;
    unsigned char actual[sizeof cache];
    assert(file_read(EVENTS_FILE, sizeof EVENTS_FILE - 1, 0, actual, sizeof actual) == sizeof actual);
    assert(!memcmp(original, actual, sizeof original));
    app_on_load(); assert(events_ready && sources[1].data.count == 1);
    app_on_data(0, (const uint8_t *)feed, sizeof feed - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(sources[0].data.count == 1 && sources[1].data.events[0].uid == 7);
    app_on_load(); assert(sources[1].data.events[0].uid == 7);
}
