// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define file_read failing_read
#define file_size failing_size
#include "../rates.c"
#include "../history.c"
#undef file_read
#undef file_size
#include "standin.h"

int32_t file_read(const char *, int32_t, int32_t, void *, int32_t);
int32_t file_size(const char *, int32_t);
static bool failed_read, failed_size;
int32_t failing_read(const char *name, int32_t length, int32_t offset, void *out, int32_t capacity)
{
    int32_t result = file_read(name, length, offset, out, capacity);
    if (failed_read) { memset(out, 0x5a, capacity / 2); return HOST_ERR_FAILED; }
    return result;
}
int32_t failing_size(const char *name, int32_t length)
{
    return failed_size ? HOST_ERR_FAILED : file_size(name, length);
}
static const char daily[] = "{\"Date\":\"2026-11-18T11:30:00+03:00\",\"Valute\":{\"USD\":{"
    "\"ID\":\"R01235\",\"Nominal\":1,\"Value\":82}}}";
int main(void)
{
    standin_reset(); standin_set("codes", "USD"); app_on_load();
    app_on_data(0, (const uint8_t *)daily, sizeof daily - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    state_t original = state;
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load(); assert(!state_ready && !requests_ready && !rotating_ready && !status_ready);
    standin()->refuse_reads = HOST_OK;
    int writes = standin()->store_writes;
    app_on_data(0, (const uint8_t *)daily, sizeof daily - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_on_button(BTN_OK_PRESSED); app_on_terminate();
    assert(standin()->store_writes == writes);
    state_t actual;
    assert(app_state_load("state", &actual, sizeof actual, SNAPSHOT_VERSION) == HOST_OK);
    assert(!memcmp(&actual, &original, sizeof actual));
    app_on_load(); assert(state_ready);
    cached_history_t first = { .version = HISTORY_VERSION, .code = "USD",
        .history = { .count = 1, .id = "R01235" } };
    assert(write_history(0, &first));
    failed_read = true;
    cached_history_t record;
    assert(!read_history(0, &record) && record.history.count == 0);
    const char history[] = "<ValCurs ID=\"R01235\" DateRange2=\"18.11.2026\">"
        "<Record Id=\"R01235\" Date=\"18.11.2026\"><Nominal>1</Nominal><Value>82</Value></Record></ValCurs>";
    writes = standin()->file_writes;
    history_answer((const uint8_t *)history, sizeof history - 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(standin()->file_writes == writes);
    failed_read = false; failed_size = true;
    assert(!write_history(1, &first) && standin()->file_writes == writes);
    failed_size = false;
    assert(read_history(0, &record) && !memcmp(&record, &first, sizeof record));
    assert(write_history(1, &first));
    assert(read_history(1, &record) && !memcmp(&record, &first, sizeof record));
}
