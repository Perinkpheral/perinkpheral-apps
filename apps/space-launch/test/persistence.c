// SPDX-License-Identifier: MIT
/* Torn-bank recovery, refused background writes and notification reservations. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define file_write write_with_failure
#define notification_post post_with_failure
#include "../launch.c"
#undef file_write
#undef notification_post
#include "standin.h"
#include "fixtures.h"

int32_t file_write(const char *, int32_t, int32_t, const void *, int32_t);
int32_t notification_post(const host_notice_t *);
static bool torn, refuse_cache, fail_after_post;
int32_t write_with_failure(const char *name, int32_t namesz, int32_t offset, const void *bytes, int32_t length)
{
    if (length == sizeof(bank_t)) {
        if (refuse_cache) { return HOST_ERR_NO_SPACE; }
        if (torn) {
            assert(file_write(name, namesz, offset, bytes, length / 2) == HOST_OK);
            return HOST_ERR_NO_SPACE;
        }
    }
    return file_write(name, namesz, offset, bytes, length);
}
int32_t post_with_failure(const host_notice_t *notice)
{
    state_t stored;
    assert(kv_get(STATE_KEY, sizeof STATE_KEY - 1, &stored, sizeof stored) == sizeof stored);
    assert(stored.reservation && was_told(stored.reservation));
    assert(stored.told[(stored.told_next + TOLD_MAX - 1) % TOLD_MAX] == stored.reservation);
    int32_t result = notification_post(notice);
    if (fail_after_post) { standin()->refuse_writes = HOST_ERR_NO_SPACE; }
    return result;
}

static void banks(void)
{
    standin_reset(); app_on_load();
    answer_file(0, -1, "upcoming.json");
    uint32_t generation = cache_generation;
    int32_t bank = cache_bank;
    int64_t net = cache.lists[0][0].net;
    cache.lists[0][0].net += HOUR;
    cache.lists[1][9].net = 123; /* a changed byte in the unwritten half */
    cache_dirty = torn = true;
    save_cache();
    assert(cache_dirty && cache_generation == generation && cache_bank == bank);
    torn = false;
    app_on_load();
    assert(cache_generation == generation && cache.lists[0][0].net == net);
    /* A full-sized mixed bank must also lose to the previous valid generation. */
    cache.lists[0][0].net += HOUR; cache.seen += MINUTE; cache_dirty = true; save_cache();
    int32_t size;
    const uint8_t *bytes = standin_file(BANKS[cache_bank], &size);
    assert(bytes && size == sizeof(bank_t));
    uint8_t damaged[sizeof(bank_t)];
    memcpy(damaged, bytes, sizeof damaged); damaged[sizeof damaged / 2] ^= 0x40;
    standin_file_put(BANKS[cache_bank], damaged, sizeof damaged);
    app_on_load();
    assert(cache_generation == generation && cache.lists[0][0].net == net);
    assert(seen_at == AT);
    cache.lists[0][0].net += 2 * HOUR; cache_dirty = true; refuse_cache = true;
    save_cache(); assert(cache_dirty && standin_alarm_left(SAVE_ALARM) == MINUTE);
    refuse_cache = false; app_on_alarm(SAVE_ALARM);
    assert(!cache_dirty);
    app_on_load(); assert(cache.lists[0][0].net == net + 2 * HOUR);
}

static void refused_background(void)
{
    standin_reset(); app_on_load();
    refuse_cache = true;
    answer_file(0, -1, "upcoming.json");
    assert(cache_dirty && state.cache_pending);
    refuse_cache = false;
    app_on_load(); /* RAM was lost; retry the uncommitted delivery. */
    assert(state.due[0] == 0 && state.status[0] == HOST_FAILED_NO_ANSWER);
    answer_file(0, -1, "upcoming.json");
    app_on_load(); assert(cache.count[0] == 10);
}

static void reservations(void)
{
    standin_reset(); standin_set("remind", "15 min"); app_on_load();
    standin_clock(1795516200); /* Ariane reminder due */
    answer_file(0, -1, "upcoming.json");
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 0 && state_dirty);
    app_on_load(); app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 0);
    standin()->refuse_writes = HOST_OK;
    fail_after_post = true;
    app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1 && state_dirty);
    app_on_load(); app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1);
    standin()->refuse_writes = HOST_OK; fail_after_post = false;
    app_on_alarm(SAVE_ALARM); assert(!state_dirty);
    app_on_load(); app_on_alarm(REMIND_ALARM); assert(standin()->notices == 1);
}
static void migration(void)
{
    standin_reset(); app_on_load();
    answer_file(0, -1, "upcoming.json");
    legacy_cache_t old = { .version = 1, .count = { 10, 0 }, .year = 2026, .total = 999, .pages_have = 1 };
    memcpy(old.lists, cache.lists, sizeof old.lists);
    standin_reset();
    standin_file_put("cache", &old, sizeof old);
    int64_t seen = AT - HOUR;
    assert(kv_set("seen", 4, &seen, sizeof seen) == HOST_OK);
    app_on_load();
    assert(cache.count[0] == 10 && seen_at == seen && !cache.pages_have && !cache.countries_have);
    assert(file_size("cache", 5) == HOST_ERR_NOT_FOUND);
    app_on_load();
    assert(cache.count[0] == 10 && seen_at == seen);
}

int main(void)
{
    banks(); refused_background(); reservations(); migration();
    puts("space-launch: bank checksums, fallback, write retries and durable reservations passed");
    return 0;
}
