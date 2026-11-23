// SPDX-License-Identifier: MIT
/* Regression checks for validated records, complete statistics and the service gate. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define answer_info answer_info_with_retry
#define answer_header header_with_retry
#include "../launch.c"
#undef answer_info
#undef answer_header
#include "standin.h"
#include "fixtures.h"

int32_t answer_info(host_answer_info_t *, int32_t);
int32_t answer_header(const char *, int32_t, int32_t, char *, int32_t);
static const char *retry_header;
static int32_t retry_after;
int32_t header_with_retry(const char *name, int32_t length, int32_t occurrence, char *out, int32_t capacity)
{
    if (!retry_header) { return answer_header(name, length, occurrence, out, capacity); }
    int32_t size = (int32_t)strlen(retry_header);
    assert(size <= capacity); memcpy(out, retry_header, (size_t)size); return size;
}
int32_t answer_info_with_retry(host_answer_info_t *response, int32_t capacity)
{
    int32_t length = answer_info(response, capacity);
    if (length == sizeof *response) { response->retry_after = retry_after; }
    return length;
}

static void records(void)
{
    standin_reset();
    app_on_load();
    answer_all();
    assert(cache.count[0] == 10 && cache.count[1] == 10 && cache.total == 271);
    assert(cache.lists[0][0].precision == 1 && cache.lists[0][4].precision == 2);
    assert(!strcmp(cache.lists[0][0].name, "Soyuz 2.1b | Progress MS-36 (97P)"));
    year_t year;
    assert(year_of(2026, &year) && year_total(&year) == 271 && year.failures == 8);
    assert(cache.countries_have == 63 && cache.countries[0] == 157);
    static const char *const BAD[] = {
        "{\"count\":1,\"results\":[{\"id\":null,\"name\":null,\"net\":\"2026-11-25T10:00:00Z\"}]}",
        "{\"count\":1,\"results\":[{\"id\":\"a\",\"name\":\"Mission\",\"net\":\"2026-11-25T10:00:00Z\"}]}",
        "{\"count\":1,\"results\":[{\"id\":\"00000000-0000-0000-0000-000000000001\",\"name\":\"   \",\"net\":\"2026-11-25T10:00:00Z\"}]}",
        "{\"count\":1,\"results\":[{\"id\":\"00000000-0000-0000-0000-000000000001\",\"name\":\"Mission\",\"net\":\"2026-02-30T10:00:00Z\"}]}",
        "{\"results\":[]}", "<html>Busy</html>", "{\"count\":1,\"results\":["
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        answer_text(0, -1, 200, BAD[i]);
        assert(cache.count[0] == 10 && state.status[0] == HOST_FAILED_NO_ANSWER);
    }
    const char *optional = "{\"count\":2,\"results\":["
        "{\"id\":\"00000000-0000-0000-0000-000000000001\",\"name\":\"One\",\"net\":\"2026-11-25T10:00:00Z\",\"status\":{\"id\":1},\"net_precision\":{\"id\":2}},"
        "{\"id\":\"00000000-0000-0000-0000-000000000002\",\"name\":\"Two\",\"net\":\"2026-11-26T10:00:00Z\",\"status\":{\"id\":1},\"net_precision\":null,\"launch_service_provider\":{\"name\":null}}]}";
    answer_text(0, -1, 200, optional);
    assert(cache.count[0] == 2 && cache.lists[0][0].precision == PRECISION_UNKNOWN);
    for (int32_t i = 0; i < 2; i++) {
        assert(!needs_reminder(&cache.lists[0][i], AT, HOUR));
        assert(!strcmp(cache.lists[0][i].provider, "?"));
    }
    char long_name[512];
    snprintf(long_name, sizeof long_name,
        "{\"count\":1,\"results\":[{\"id\":\"00000000-0000-0000-0000-000000000003\",\"name\":\"Electron | %080d\",\"net\":\"2026-11-25T10:00:00Z\"}]}", 1);
    answer_text(0, -1, 200, long_name);
    assert(cache.count[0] == 1 && !strcmp(cache.lists[0][0].name + 60, "..."));
    answer_text(0, -1, 200, "{\"count\":0,\"results\":[]}");
    assert(cache.count[0] == 0);
}

static void throttle(void)
{
    standin_reset();
    app_on_load();
    app_on_intent(HOST_INTENT_STARTED);
    assert(standin()->sends == 1 && state.active_tag);
    int32_t tag = state.active_tag;
    retry_after = 7 * DAY;
    standin_handlers.on_data = app_on_data;
    standin_data(tag, -1, 429, "{}", 2, 1, STANDIN_CUT, 0);
    int64_t until = state.cooldown;
    assert(until == AT + 7 * DAY);
    for (int32_t i = 0; i < 4; i++) {
        app_on_settings(); app_on_focus(); app_on_alarm(FETCH_ALARM);
        app_on_intent(HOST_INTENT_NETWORK_UP); app_on_intent(HOST_INTENT_TIME_CHANGED);
        app_on_updated(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
        assert(standin()->sends == 1 && state.cooldown == until);
    }
    standin_clock(until - 1); fetch();
    assert(standin()->sends == 1);
    standin_clock(until); fetch();
    assert(standin()->sends == 1); /* moving the wall clock cannot shorten the wait */
    standin_clock(AT);
    standin_pass(7LL * DAY * 1000); fetch();
    assert(standin()->sends == 2);
    retry_after = 0;
    standin_data(state.active_tag, -1, 429, "{}", 2, 2, STANDIN_WHOLE, 0);
    assert(state.cooldown == until + HOUR);
    /* Date-form Retry-After, and a 429 for an old-year request, close the same gate. */
    standin_reset(); app_on_load();
    retry_header = "Tue, 24 Nov 2026 18:00:00 GMT";
    answer_text(POLL_UPCOMING, -1, 429, "{}");
    assert(state.cooldown == AT + DAY);
    retry_header = NULL;
    state.request = POLL_COUNTRIES; state.item = 0; state.year = 2026; state.active_tag = 77;
    standin_clock(1798762200); retry_after = 600;
    standin_data(77, -1, 429, "{}", 2, 2, STANDIN_WHOLE, 0);
    assert(cache.year == 2027 && state.cooldown == time_now() + 600);
    int32_t sends = standin()->sends;
    app_on_settings(); app_on_intent(HOST_INTENT_NETWORK_UP); app_on_alarm(PACE_ALARM);
    assert(standin()->sends == sends);
    retry_after = 0;
}

static void annual(void)
{
    standin_reset(); app_on_load();
    answer_file(2, 0, "year-0.json");
    year_t year;
    assert(!year_of(2026, &year) && cache.countries_have == 0 && state.status[POLL_YEAR] == 0);
    answer_text(2, 100, 429, "{}");
    assert(!year_of(2026, &year) && cache.staging.year == 0);
    answer_all();
    assert(year_of(2026, &year) && year_total(&year) == 271);
    answer_file(2, 0, "year-0.json");
    answer_text(2, 100, 500, "{}");
    assert(year_of(2026, &year) && year_total(&year) == 271 && cache.countries[0] == 157);
    /* A moving count cannot combine generations. */
    answer_file(2, 0, "year-0.json");
    answer_text(2, 100, 200, "{\"count\":401,\"results\":[]}");
    assert(cache.staging.year == 0 && cache.total == 271);
    /* 401 needs five pages. Check the actual queued offset, and no publication before
     * all five pages plus every country have completed. */
    for (int32_t offset = 0; offset < 401; offset += 100) {
        char body[12000] = "{\"count\":401,\"results\":[";
        int32_t count = 401 - offset < 100 ? 401 - offset : 100;
        for (int32_t i = 0; i < count; i++) {
            strcat(body, i ? "," : "");
            strcat(body, "{\"net\":\"2026-01-01T10:00:00Z\",\"status\":{\"id\":3}}");
        }
        strcat(body, "]}");
        answer_text(2, offset, 200, body);
        assert(cache.total == 271 && cache.staging.offset == offset + count);
        if (offset == 300) {
            state.due[0] = state.due[1] = time_now() + DAY;
            state.due[2] = 0;
            state.next_send = state.cooldown = state.cooldown_uptime = 0;
            request_pending = false;
            fetch();
            assert(standin()->request_count > 0);
            assert(strstr(standin()->requests[standin()->request_count - 1].url, "offset=400&"));
            state.active_tag = 0;
        }
    }
    for (int32_t i = 0; i < COUNTRIES; i++) { answer_text(3, i, 200, "{\"count\":1}"); }
    assert(cache.total == 271);
    answer_text(2, -1, 200, "{\"count\":401,\"results\":[{\"net\":\"2026-01-01T10:00:00Z\",\"status\":{\"id\":3}}]}");
    assert(cache.total == 401 && year_of(2026, &year) && year_total(&year) == 401);
    /* Both empty pages and country counts are bound to the requested year. */
    state.request = POLL_COUNTRIES; state.item = 0; state.year = 2026;
    state.active_tag = 77; save_state();
    standin_clock(1798762200);
    app_on_load();
    standin_data(77, -1, 200, "{\"count\":99}", 12, 1, STANDIN_WHOLE, 0);
    assert(cache.year == 2027 && cache.countries_have == 0 && cache.staging.year == 0);
    assert(year_of(2026, &year) && year_total(&year) == 401);
    state.request = POLL_YEAR; state.item = 0; state.year = 2027; state.active_tag = 78;
    standin_clock(1830297600); /* 2028 */
    standin_data(78, -1, 200, "{\"count\":0,\"results\":[]}", 24, 7, STANDIN_WHOLE, 0);
    assert(cache.year == 2028 && !cache.pages_have);
}

int main(void)
{
    records(); throttle(); annual();
    puts("space-launch: validation, global cooldown, complete paging and year bindings passed");
    return 0;
}
