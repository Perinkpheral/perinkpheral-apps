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
    if (length >= 12) { memcpy((uint8_t *)response + offsetof(host_answer_info_t, retry_after), &retry_after, sizeof retry_after); }
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
        assert(!strcmp(cache.lists[0][i].provider, "?") && !strcmp(cache.lists[0][i].site, "?"));
    }
    char long_name[512];
    snprintf(long_name, sizeof long_name,
        "{\"count\":1,\"results\":[{\"id\":\"00000000-0000-0000-0000-000000000003\",\"status\":{\"id\":1},\"name\":\"Electron | %080d\",\"net\":\"2026-11-25T10:00:00Z\"}]}", 1);
    answer_text(0, -1, 200, long_name);
    assert(cache.count[0] == 1 && !strcmp(cache.lists[0][0].name + 60, "..."));
    answer_text(0, -1, 200, "{\"count\":0,\"results\":[]}");
    assert(cache.count[0] == 0);
}

static int sends(void) { return standin()->polls_run[0] + standin()->polls_run[1] + standin()->polls_run[2]; }
static void throttle(void)
{
    standin_reset(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert(sends() == 1 && state.active_poll);
    int32_t binding = state.active_poll;
    standin_pass(20 * MINUTE * 1000);
    app_on_settings(); app_on_focus(); app_on_alarm(FETCH_ALARM);
    assert(sends() == 2 && state.active_poll == binding); /* requeue the same coalesced binding, never expire it */
    int32_t asked = sends();
    app_on_load(); fetch(); assert(sends() == asked); /* queued binding survives unload */
    retry_after = 7 * DAY;
    answer_text(POLL_UPCOMING, -1, 429, "{}");
    assert(!standin_poll_ready(0, -1) && !standin_poll_ready(1, -1) && !standin_poll_ready(2, -1));
    int64_t until = state.cooldown;
    assert(until == time_now() + 7 * DAY);
    for (int i = 0; i < 4; i++) {
        app_on_settings(); app_on_alarm(FETCH_ALARM); app_on_intent(HOST_INTENT_NETWORK_UP);
        app_on_load(); fetch(); assert(sends() == asked);
    }
    standin_clock(AT + DAY); fetch(); assert(sends() == asked); /* forward jump cannot waive wait */
    standin_clock(AT); app_on_intent(HOST_INTENT_TIME_CHANGED); assert(sends() == asked);
    standin_pass(7LL * DAY * 1000); fetch(); assert(sends() == asked + 1);
    retry_after = 0;
    answer_text(state.request, state.item, 429, "{}");
    assert(state.cooldown == time_now() + HOUR);
    standin_reset(); app_on_load();
    retry_header = "Tue, 24 Nov 2026 18:00:00 GMT";
    answer_text(POLL_UPCOMING, -1, 429, "{}");
    assert(state.cooldown == AT + DAY); retry_header = NULL;
}

static void install_then_start(void)
{
    standin_reset(); app_on_load(); app_on_installed();
    int32_t binding = state.active_poll, before = sends();
    assert(binding == 1 && standin_poll_ready(0, -1));
    app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert(state.active_poll == binding && sends() == before && standin_poll_ready(0, -1));
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    assert(cache.count[0] == 10);
}

static void delayed_success(void)
{
    standin_reset(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    standin_pass(20 * MINUTE * 1000);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    int32_t before = sends();
    app_on_settings(); fetch(); assert(sends() == before);
    standin_pass(REQUEST_GAP * 1000); fetch();
    assert(sends() == before + 1 && state.request == POLL_PREVIOUS);
}

static void recovery(void)
{
    int32_t length;
    char *body = fixture("upcoming.json", &length);
    standin_reset(); app_on_load(); app_on_focus();
    standin_handlers.on_picked = app_on_picked;
    standin_picked(POLL_UPCOMING, -1, 200, body, length);
    uint64_t whole = standin_screen(standin_draw());
    static const int32_t FAILURES[] = { -1, -2, -3, -4, -5, -6, -7, -8, 503, 404, 304, 200, 429 };
    for (int away = 0; away < 2; away++) {
        for (size_t i = 0; i < sizeof FAILURES / sizeof FAILURES[0]; i++) {
            standin_reset(); app_on_load(); app_on_focus();
            standin_handlers.on_picked = app_on_picked;
            /* Truncation invalidates even the values picked before the final bracket. */
            standin_picked(POLL_UPCOMING, -1, FAILURES[i], body, length - 2);
            assert(!cache.count[0] && state.status[0] != 200);
            int32_t before = sends();
            if (away) { app_on_terminate(); app_on_load(); }
            app_on_settings(); app_on_alarm(FETCH_ALARM);
            assert(sends() == before && state.due[0] == AT + HOUR);
            assert(standin_poll_ready(0, -1) == (FAILURES[i] != 429));
            /* No helper may manufacture a new binding, open its gate or advance time. */
            standin_picked(POLL_UPCOMING, -1, 200, body, length);
            assert(cache.count[0] == 10 && state.status[0] == 200);
            assert(sends() == before && !standin_poll_ready(0, -1));
            if (away) { app_on_terminate(); app_on_load(); app_on_focus(); }
            assert(standin_screen(standin_draw()) == whole);
            if (FAILURES[i] == 429) { assert(state.cooldown == AT + HOUR); }
        }
    }
    free(body);
}

static void restarted_gate(void)
{
    standin_reset(); app_on_load();
    state.active_poll = 3; state.request = POLL_YEAR; state.cursor = 0;
    assert(kv_set("year_gate", 9, "1", 1) == HOST_OK); save_state();
    standin_restart(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert(state.request == POLL_UPCOMING && !standin_poll_ready(1, -1) && !standin_poll_ready(2, -1));
    /* Even a late refusal with another poll binding closes every service gate. */
    int32_t binding = state.active_poll;
    retry_after = 600; standin_handlers.on_picked = app_on_picked;
    standin_picked(2, -1, 429, "{}", 2);
    assert(state.active_poll == binding);
    assert(state.cooldown == time_now() + 600);
    assert(!standin_poll_ready(0, -1) && !standin_poll_ready(1, -1) && !standin_poll_ready(2, -1));
    int32_t before = sends(); fetch(); assert(sends() == before);
    standin_picked(0, -1, HOST_FAILED_NOT_READY, "{}", 2);
    assert(!state.active_poll && state.status[POLL_UPCOMING] == 429);
    fetch(); assert(sends() == before);
    retry_after = 0;
}

static void clock_and_fairness(void)
{
    standin_reset(); app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    answer_text(POLL_UPCOMING, -1, 500, "{}");
    assert(state.due[0] == AT + HOUR);
    standin_pass(REQUEST_GAP * 1000); fetch();
    assert(state.request == POLL_PREVIOUS);
    answer_file(POLL_PREVIOUS, -1, "previous.json");
    standin_pass(REQUEST_GAP * 1000); fetch(); assert(state.request == POLL_YEAR);
    answer_text(POLL_YEAR, 0, 500, "{}");
    state.due[0] = time_now() + 30 * MINUTE;
    state.due[1] = time_now() + 3 * HOUR;
    state.due[2] = time_now() + DAY;
    state.next_send = time_now() + REQUEST_GAP; save_state();
    int64_t before = time_now();
    standin_clock(HOST_TIME_MIN); app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(state.next_send == HOST_TIME_MIN + REQUEST_GAP);
    assert(state.due[0] == HOST_TIME_MIN + 30 * MINUTE);
    assert(state.due[1] == HOST_TIME_MIN + 3 * HOUR && state.due[2] == HOST_TIME_MIN + DAY);
    save_state(); app_on_load(); assert(state.due[0] == HOST_TIME_MIN + 30 * MINUTE);
    /* Also detect a backward jump first observed after an unload. */
    standin_clock(before); fetch(); save_state();
    standin_restart(); standin_clock(HOST_TIME_MIN); app_on_load();
    assert(state.due[0] <= HOST_TIME_MIN + 30 * MINUTE);
}

static void page(int32_t offset, int32_t total, int32_t duplicate, int32_t moved)
{
    char body[18000];
    int length = snprintf(body, sizeof body, "{\"count\":%d,\"results\":[", total);
    int32_t count = total - offset < PAGE_SIZE ? total - offset : PAGE_SIZE;
    for (int i = 0; i < count; i++) {
        int id = offset + i + 1;
        if (!i && duplicate) { id--; }
        length += snprintf(body + length, sizeof body - length,
            "%s{\"id\":\"00000000-0000-0000-0000-%012x\",\"net\":\"2026-%s-01T10:00:00Z\",\"status\":{\"id\":3},\"launch_service_provider\":{\"id\":121}}",
            i ? "," : "", id, moved && i == 0 ? "12" : "01");
    }
    snprintf(body + length, sizeof body - length, "]}");
    answer_text(POLL_YEAR, offset, 200, body);
}
static void annual(void)
{
    standin_reset(); app_on_load(); answer_all();
    year_t year;
    assert(cache.total == 271 && year_of(2026, &year) && year.failures == 8);
    answer_file(POLL_YEAR, 0, "year-0.json");
    answer_text(POLL_YEAR, PAGE_SIZE, 500, "{}");
    assert(cache.total == 271 && !cache.staging.year);
    page(0, 401, 0, 0);
    cache.staging.last_id[35] = 'A'; /* same UUID with a different textual case */
    page(PAGE_SIZE, 401, 1, 0);
    assert(!cache.staging.year && cache.total == 271); /* same total, repeated ID at boundary */
    for (int offset = 0; offset < 401; offset += PAGE_SIZE) {
        page(offset, 401, 0, 1); /* NET moves across old net-ordered boundaries, UUID order is stable */
        if (offset < 400) { assert(cache.total == 271); }
    }
    assert(cache.total == 401 && cache.annual.months[11] == (401 + PAGE_SIZE - 1) / PAGE_SIZE && cache.countries[0] == 401);
    /* Old-year responses never publish, including an empty annual response. */
    state.request = POLL_YEAR; state.item = 0; state.year = 2026; state.active_poll = 3; save_state();
    standin_clock(1798762200); app_on_load();
    standin_picked(2, -1, 200, "{\"count\":0,\"results\":[]}", 24);
    assert(cache.year == 2027 && !cache.pages_have && year_of(2026, &year) && year_total(&year) == 401);
}

static void unknown_provider(void)
{
    standin_reset(); app_on_load();
    answer_text(POLL_YEAR, 0, 200, "{\"count\":2,\"results\":["
        "{\"id\":\"00000000-0000-0000-0000-000000000001\",\"net\":\"2026-01-01T00:00:00Z\",\"status\":{\"id\":3},\"launch_service_provider\":null},"
        "{\"id\":\"00000000-0000-0000-0000-000000000002\",\"net\":\"2026-01-02T00:00:00Z\",\"status\":{\"id\":3},\"launch_service_provider\":{\"id\":121}}]}");
    assert(cache.total == 2 && cache.countries[0] == 1 && cache.annual.months[0] == 2);
}

static void scheduler_alarm(void)
{
    standin_reset(); standin_clock(0); app_on_load(); fetch();
    assert(standin()->alarms[FETCH_ALARM].kind == STANDIN_ALARM_EVERY);
    standin_clock(AT); app_on_intent(HOST_INTENT_TIME_CHANGED);
    assert(standin()->alarms[FETCH_ALARM].kind == STANDIN_ALARM_EVERY);
    app_on_load();
    assert(standin_alarm_left(FETCH_ALARM) == REQUEST_GAP);
}

static void details(void)
{
    standin_reset(); app_on_load(); answer_file(0, -1, "upcoming.json");
    assert(strcmp(cache.lists[0][0].provider, "?") && strcmp(cache.lists[0][0].site, "?"));
    int32_t length;
    char *body = fixture("upcoming.json", &length);
    char *net = strstr(body, "2026-11-24T10:13:00Z"); assert(net); net[12] = '1';
    standin_pass(MINUTE * 1000); answer_text(0, -1, 200, body);
    int64_t changed = time_now();
    assert(cache.lists[0][0].slipped_at == changed);
    int writes = standin()->file_writes;
    standin_pass(REQUEST_GAP * 1000); answer_text(0, -1, 200, body);
    assert(cache.lists[0][0].slipped_at == changed && standin()->file_writes == writes);
    app_on_load(); assert(cache.lists[0][0].slipped_at == changed);
    free(body);
}

static void freshness(void)
{
    standin_reset(); app_on_load(); answer_file(0, -1, "upcoming.json");
    int writes = standin()->file_writes;
    standin_pass(30 * MINUTE * 1000); answer_file(0, -1, "upcoming.json");
    assert(standin()->file_writes == writes && state.seen == time_now());
    app_on_load(); assert(seen_at == time_now());
    int32_t length;
    char *body = fixture("upcoming.json", &length);
    memcpy(strstr(body, "Soyuz"), "SOYUZ", 5);
    answer_text(0, -1, 200, body); free(body);
    int32_t size;
    const uint8_t *bytes = standin_file(BANKS[cache_bank], &size);
    uint8_t damaged[sizeof(bank_t)];
    assert(size == sizeof damaged); memcpy(damaged, bytes, sizeof damaged); damaged[size - 1] ^= 1;
    standin_file_put(BANKS[cache_bank], damaged, size);
    app_on_load(); assert(!seen_at); /* freshness from a different list never follows fallback */
}
int main(void)
{
    records(); throttle(); install_then_start(); delayed_success(); recovery(); restarted_gate(); clock_and_fairness(); annual(); details(); freshness(); scheduler_alarm(); unknown_provider();
    puts("space-launch: validation, serialization, cooldown, clocks, fairness, UUID paging and KV freshness passed");
    return 0;
}
