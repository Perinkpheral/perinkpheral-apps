// SPDX-License-Identifier: MIT
/* The library's answers: the lists, the year's pages and the countries taken from real answers;
 * columns that do not line up, answers that are not lists, a 429 and its Retry-After, a launch
 * whose time moved, and the turn of the year. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* The stand-in gives no headers: a Retry-After is put in here. */
#define answer_info answer_info_with_retry
#include "../launch.c"
#undef answer_info
#include "standin.h"
#include "fixtures.h"

int32_t answer_info(host_answer_info_t *response, int32_t capacity);
static int32_t retry_after;
int32_t answer_info_with_retry(host_answer_info_t *response, int32_t capacity)
{
    int32_t length = answer_info(response, capacity);
    if (length == (int32_t)sizeof *response) { response->retry_after = retry_after; }
    return length;
}

static const char TWO[] =
    "{\"count\":2,\"results\":["
    "{\"id\":\"a\",\"name\":\"Electron | One\",\"net\":\"2026-11-25T10:00:00Z\",\"status\":{\"id\":1},"
    "\"net_precision\":{\"id\":1},\"launch_service_provider\":{\"name\":\"Rocket Lab\"},"
    "\"pad\":{\"location\":{\"name\":\"Mahia\"}}},"
    "{\"id\":\"b\",\"name\":\"Kairos | Two\",\"net\":\"2026-12-31T00:00:00Z\",\"status\":{\"id\":2},"
    "\"net_precision\":null,\"launch_service_provider\":{\"name\":\"Space One\"},"
    "\"pad\":{\"location\":{\"name\":\"Kii\"}}}]}";

static void lists(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    app_on_load();
    assert(cache.year == 2026);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    const launch_t *first = &cache.lists[0][0];
    assert(cache.count[0] == 10 && state.status[POLL_UPCOMING] == 200 && seen_at == AT);
    assert(!strcmp(first->name, "Soyuz 2.1b | Progress MS-36 (97P)") && first->net == 1795515180);
    assert(first->status == ST_TBC && first->precision == 1 && first->slipped_at == 0);
    assert(!strcmp(first->provider, "Russian Federal Space Agency (ROSCOSMOS)"));
    assert(!strcmp(first->site, "Baikonur Cosmodrome, Republic of Kazakhstan"));
    assert(cache.lists[0][4].precision == 2 && cache.lists[0][9].status == ST_GO);
    answer_file(POLL_PREVIOUS, -1, "previous.json");
    assert(cache.count[1] == 10 && cache.lists[1][4].status == ST_FAILURE);

    /* The rows: the previous launches from the oldest, then the upcoming ones. */
    build_rows();
    assert(row_count == 20 && !strcmp(rows[0]->name, "Long March 2D | CAS Pathfinder-1"));
    assert(rows[10] == first && next_row() == 10);

    /* The same answers again write nothing. */
    int32_t writes = flash_writes();
    for (int32_t i = 0; i < 20; i++) {
        answer_file(POLL_UPCOMING, -1, "upcoming.json");
        answer_file(POLL_PREVIOUS, -1, "previous.json");
    }
    assert(flash_writes() == writes);

    /* A launch the upcoming list keeps from the last day is one row, not two. */
    launch_t kept = cache.lists[0][0];
    cache.lists[1][0].id = kept.id;
    build_rows();
    assert(row_count == 19);
}

static void columns(void)
{
    /* A null precision leaves that column out for every launch, not shifted onto another. */
    answer_text(POLL_UPCOMING, -1, 200, TWO);
    assert(cache.count[0] == 2 && state.status[POLL_UPCOMING] == 200);
    assert(cache.lists[0][0].precision == PRECISION_UNKNOWN && cache.lists[0][1].precision == PRECISION_UNKNOWN);
    assert(cache.lists[0][0].status == ST_GO && cache.lists[0][1].status == ST_TBD);
    assert(!strcmp(cache.lists[0][1].site, "Kii"));

    /* What is not a list of launches leaves the list as it was. */
    static const char *const BROKEN[] = {
        "{\"count\":1,\"results\":[{\"id\":\"c\",\"name\":\"No time\"}]}",
        "{\"count\":1,\"results\":[{\"id\":\"c\",\"name\":\"Bad time\",\"net\":\"soon\"}]}",
        "{\"results\":[]}",
        "{\"count\":2,\"results\":[{\"id\":\"a\",\"name\":\"Cut",
        "<html>Busy</html>",
    };
    for (size_t i = 0; i < sizeof BROKEN / sizeof BROKEN[0]; i++) {
        answer_text(POLL_UPCOMING, -1, 200, BROKEN[i]);
        assert(cache.count[0] == 2 && state.status[POLL_UPCOMING] == HOST_FAILED_NO_ANSWER);
    }
    char eleven[4096] = "{\"count\":11,\"results\":[";
    for (int32_t i = 0; i < 11; i++) {
        char one[160];
        snprintf(one, sizeof one, "%s{\"id\":\"%d\",\"name\":\"L%d\",\"net\":\"2026-11-25T10:00:00Z\"}", i ? "," : "", i, i);
        strcat(eleven, one);
    }
    strcat(eleven, "]}");
    answer_text(POLL_UPCOMING, -1, 200, eleven);
    assert(cache.count[0] == 2 && state.status[POLL_UPCOMING] == HOST_FAILED_NO_ANSWER);

    /* No launches is an answer too. */
    answer_text(POLL_UPCOMING, -1, 200, "{\"count\":0,\"next\":null,\"results\":[]}");
    assert(cache.count[0] == 0 && state.status[POLL_UPCOMING] == 200);
}

static void too_many(void)
{
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    int64_t seen = seen_at;
    standin_pass(600 * 1000);
    retry_after = 1406;
    answer_text(POLL_UPCOMING, -1, 429, "{\"detail\":\"Request was throttled. Expected available in 1406 seconds.\"}");
    assert(standin()->poll_interval[POLL_UPCOMING] == 1406 && (state.postponed & 1));
    assert(cache.count[0] == 10 && seen_at == seen && state.status[POLL_UPCOMING] == 429);
    int32_t writes = flash_writes();
    answer_text(POLL_UPCOMING, -1, 429, "{}");
    assert(flash_writes() == writes);
    retry_after = 0;
    answer_text(POLL_PREVIOUS, -1, 429, "");
    assert(standin()->poll_interval[POLL_PREVIOUS] == RETRY_DEFAULT_S && state.postponed == 3);
    retry_after = 7 * DAY;
    answer_text(POLL_YEAR, 0, 429, "");
    assert(standin()->poll_interval[POLL_YEAR] == DAY);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    assert(standin()->poll_interval[POLL_UPCOMING] == 0 && state.postponed == 6 && seen_at > seen);
    /* Other failures leave the pace alone. */
    answer_text(POLL_UPCOMING, -1, HOST_FAILED_NO_NETWORK, "");
    assert(standin()->poll_interval[POLL_UPCOMING] == 0 && state.status[POLL_UPCOMING] == HOST_FAILED_NO_NETWORK);
}

static void year(void)
{
    answer_all();
    year_t sum;
    sum_pages(&sum);
    assert(cache.pages_have == 0xF && cache.total == 271 && year_total(&sum) == 271 && sum.failures == 8);
    assert(sum.months[0] == 24 && sum.months[10] == 17 && sum.months[11] == 0);
    assert(cache.countries_have == 0x3F && cache.countries[0] == 157 && cache.countries[5] == 4);
    /* A page of another year, or without its count, is not this year's. */
    answer_text(POLL_YEAR, 1, 200, "{\"count\":5,\"results\":[{\"net\":\"2025-12-31T23:00:00Z\",\"status\":{\"id\":3}}]}");
    answer_text(POLL_YEAR, 1, 200, "{\"results\":[{\"net\":\"2026-12-31T23:00:00Z\",\"status\":{\"id\":3}}]}");
    answer_text(POLL_YEAR, 7, 200, "{\"count\":5,\"results\":[]}");
    sum_pages(&sum);
    assert(year_total(&sum) == 271 && state.status[POLL_YEAR] == HOST_FAILED_NO_ANSWER);
    assert(year_of(2026, &sum) && year_of(1957, &sum) && year_total(&sum) == 3 && sum.failures == 1);
    assert(year_of(2025, &sum) && year_total(&sum) == 324 && !year_of(1956, &sum) && !year_of(2027, &sum));

    /* The turn of the year keeps the year before and asks for the new one. */
    standin_clock(1798762200); /* 2027-01-01 00:10 UTC */
    app_on_load();
    char stored[8] = "";
    app_setting("year", stored, sizeof stored);
    assert(cache.year == 2027 && cache.pages_have == 0 && cache.countries_have == 0 && !strcmp(stored, "2027"));
    assert(standin()->polls_run[POLL_YEAR] == 1 && standin()->polls_run[POLL_COUNTRIES] == 1);
    assert(cache.last_year == 2026 && year_of(2026, &sum) && year_total(&sum) == 271 && latest_year() == 2027);
    app_on_load();
    assert(standin()->polls_run[POLL_YEAR] == 1);
}

static void slips(void)
{
    standin_reset();
    app_on_load();
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    int32_t length;
    char *body = fixture("upcoming.json", &length);
    char *moved = strstr(body, "2026-11-24T10:45:00Z");
    assert(moved != NULL);
    memcpy(moved, "2026-11-24T11:45:00Z", 20);
    standin_pass(1800 * 1000);
    standin_picked(POLL_UPCOMING, -1, 200, body, length);
    assert(cache.lists[0][1].slipped_at == time_now() && cache.lists[0][0].slipped_at == 0);
    int64_t slipped = time_now();
    int32_t writes = standin()->file_writes;
    standin_pass(1800 * 1000);
    standin_picked(POLL_UPCOMING, -1, 200, body, length);
    assert(cache.lists[0][1].slipped_at == slipped && standin()->file_writes == writes);
    free(body);
}

int main(void)
{
    lists();
    columns();
    too_many();
    year();
    slips();
    puts("space-launch: lists, columns, broken answers, 429 with and without Retry-After, the year's pages, "
         "countries, the turn of the year and slips passed");
    return 0;
}
