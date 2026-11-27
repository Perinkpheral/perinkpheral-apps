// SPDX-License-Identifier: MIT
/* Failed restores, including partial copies, must never replace durable records. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define file_read read_with_failure
#include "../launch.c"
#undef file_read
#include "standin.h"
#include "fixtures.h"

int32_t file_read(const char *, int32_t, int32_t, void *, int32_t);
static const char *failed_file;
static int32_t failed_offset;
int32_t read_with_failure(const char *name, int32_t namesz, int32_t offset, void *out, int32_t size)
{
    if (failed_file && !strncmp(name, failed_file, (size_t)namesz) && offset == failed_offset) {
        memset(out, 0xa5, (size_t)size / 2);
        return HOST_ERR_FAILED;
    }
    return file_read(name, namesz, offset, out, size);
}

static void state_failure(void)
{
    standin_reset(); app_on_load(); answer_file(0, -1, "upcoming.json");
    state.told[0] = 123; save_state();
    state_t stored = state;
    standin()->refuse_reads = HOST_ERR_FAILED;
    standin()->failed_read_bytes = 19;
    app_on_load(); assert(state_read_failed && cache_read_failed && receipt_read_failed);
    standin()->refuse_reads = HOST_OK;
    int writes = flash_writes();
    app_on_settings(); app_on_intent(HOST_INTENT_STARTED);
    app_on_alarm(SAVE_ALARM); app_on_alarm(REMIND_ALARM); app_on_terminate();
    assert(flash_writes() == writes && !standin()->notices);
    int32_t length;
    const uint8_t *value = standin_value(STATE_KEY, &length);
    assert(length == sizeof stored && !memcmp(value, &stored, sizeof stored));
    app_on_load();
    assert(!state_read_failed && !cache_read_failed && state.told[0] == 123 && cache.count[0] == 10);
}

static void bank_failures(void)
{
    for (int32_t bank = 0; bank < 2; bank++) {
        standin_reset(); app_on_load(); answer_file(0, -1, "upcoming.json");
        answer_file(1, -1, "previous.json");
        int32_t size;
        bank_t before;
        const uint8_t *bytes = standin_file(BANKS[bank], &size);
        assert(size == sizeof before); memcpy(&before, bytes, sizeof before);
        failed_file = BANKS[bank]; app_on_load(); assert(cache_read_failed);
        failed_file = NULL;
        int writes = standin()->file_writes;
        answer_file(0, -1, "upcoming.json");
        app_on_alarm(SAVE_ALARM); app_on_settings(); app_on_terminate();
        assert(standin()->file_writes == writes);
        bytes = standin_file(BANKS[bank], &size);
        assert(!memcmp(bytes, &before, sizeof before));
        app_on_load(); assert(!cache_read_failed && cache.count[0] == 10 && cache.count[1] == 10);
    }
}

static void receipt_failure(void)
{
    standin_reset(); standin_set("remind", "15 min"); app_on_load();
    standin_clock(1795516200); answer_file(0, -1, "upcoming.json");
    uint32_t receipt[] = { 123, ~123u };
    assert(file_write("told", 4, 0, receipt, sizeof receipt) == HOST_OK);
    failed_file = "told"; app_on_load(); assert(receipt_read_failed);
    failed_file = NULL;
    int writes = standin()->file_writes;
    app_on_alarm(REMIND_ALARM); app_on_terminate();
    assert(!standin()->notices && standin()->file_writes == writes);
    uint32_t restored[2];
    assert(file_read("told", 4, 0, restored, sizeof restored) == sizeof restored);
    assert(!memcmp(receipt, restored, sizeof receipt));
    app_on_load(); assert(!receipt_read_failed && was_told(123));
    app_on_alarm(REMIND_ALARM); assert(standin()->notices == 1);
}

static void history_failure(void)
{
    for (int32_t part = 0; part < 2; part++) {
        standin_reset(); app_on_load();
        failed_file = HISTORY_FILE; failed_offset = part ? 4 : 0;
        read_history(); assert(history_count == -1);
        failed_file = NULL; failed_offset = 0;
        read_history(); assert(history_count == 2025 - FIRST_YEAR + 1);
        year_t year; assert(year_of(2025, &year) && year_total(&year) > 1);
    }
}

int main(void)
{
    state_failure(); bank_failures(); receipt_failure(); history_failure();
    puts("space-launch: unreadable state, both cache banks, history and receipts survive until restored");
    return 0;
}
