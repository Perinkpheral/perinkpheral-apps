// SPDX-License-Identifier: MIT
/* Torn-bank recovery, refused writes, retryable posts and delivery receipts. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define file_write write_with_failure
#define kv_set kv_with_failure
#define notification_post post_with_failure
#include "../launch.c"
#undef file_write
#undef kv_set
#undef notification_post
#include "standin.h"
#include "fixtures.h"

int32_t file_write(const char *, int32_t, int32_t, const void *, int32_t);
int32_t kv_set(const char *, int32_t, const void *, int32_t);
int32_t notification_post(const host_notice_t *);
static bool torn, refuse_cache, refuse_kv, fail_after_post, queue_full;
int32_t kv_with_failure(const char *key, int32_t namesz, const void *value, int32_t length)
{
    return refuse_kv ? HOST_ERR_NO_SPACE : kv_set(key, namesz, value, length);
}
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
    if (queue_full) { return HOST_ERR_NO_SPACE; }
    int32_t result = notification_post(notice);
    if (fail_after_post) { refuse_kv = true; }
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
    cache.lists[1][9].net = 123;
    cache_dirty = torn = true;
    save_cache();
    assert(cache_dirty && cache_generation == generation && cache_bank == bank);
    torn = false; app_on_load();
    assert(cache_generation == generation && cache.lists[0][0].net == net);
    cache.lists[0][0].net += HOUR; cache_dirty = true; save_cache();
    int32_t size;
    const uint8_t *bytes = standin_file(BANKS[cache_bank], &size);
    assert(bytes && size == sizeof(bank_t));
    uint8_t damaged[sizeof(bank_t)];
    memcpy(damaged, bytes, sizeof damaged); damaged[sizeof damaged / 2] ^= 0x40;
    standin_file_put(BANKS[cache_bank], damaged, sizeof damaged);
    app_on_load();
    assert(cache_generation == generation && cache.lists[0][0].net == net && seen_at == AT);
    cache.lists[0][0].net += 2 * HOUR; cache_dirty = true; refuse_cache = true;
    save_cache(); assert(cache_dirty && standin_alarm_left(SAVE_ALARM) == MINUTE);
    refuse_cache = false; app_on_alarm(SAVE_ALARM);
    assert(!cache_dirty);
    app_on_load(); assert(cache.lists[0][0].net == net + 2 * HOUR);
}
static void refused_background(void)
{
    standin_reset(); app_on_load(); refuse_cache = true;
    answer_file(0, -1, "upcoming.json");
    assert(cache_dirty && state.cache_pending);
    refuse_cache = false; app_on_load();
    assert(state.due[0] == 0 && state.status[0] == HOST_FAILED_NO_ANSWER && !seen_at);
    answer_file(0, -1, "upcoming.json"); app_on_load(); assert(cache.count[0] == 10);
}
static void reminders(void)
{
    standin_reset(); standin_set("remind", "15 min"); app_on_load();
    standin_clock(1795516200); answer_file(0, -1, "upcoming.json");
    refuse_kv = true;
    app_on_alarm(REMIND_ALARM); assert(!standin()->notices && state_dirty);
    app_on_load(); app_on_alarm(REMIND_ALARM); assert(!standin()->notices);
    refuse_kv = false; queue_full = true;
    app_on_alarm(REMIND_ALARM); app_on_terminate(); app_on_load();
    assert(!standin()->notices); /* rejected delivery never consumed an ID */
    queue_full = false; fail_after_post = true;
    app_on_alarm(REMIND_ALARM); assert(standin()->notices == 1 && state_dirty);
    app_on_load(); app_on_alarm(REMIND_ALARM); assert(standin()->notices == 1);
    refuse_kv = fail_after_post = false;
    app_on_alarm(SAVE_ALARM); app_on_load(); app_on_alarm(REMIND_ALARM);
    assert(standin()->notices == 1 && !state_dirty);
}
int main(void)
{
    banks(); refused_background(); reminders();
    puts("space-launch: bank fallback, write retries, rejected posts and delivery receipts passed");
    return 0;
}
