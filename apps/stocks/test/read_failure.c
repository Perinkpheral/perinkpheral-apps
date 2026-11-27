// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define file_read failing_read
#define file_size failing_size
#include "../stocks.c"
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
int main(void)
{
    standin_reset(); standin_set("tickers", "AAPL\nMOEX:SBER");
    standin_handlers.on_picked = app_on_picked;
    state_t original = { .version = STATE_VERSION, .rotating = 1 };
    assert(app_state_save(STATE_KEY, &original, sizeof original));
    standin()->refuse_reads = HOST_ERR_FAILED;
    app_on_load(); assert(!preferences_ready);
    standin()->refuse_reads = HOST_OK;
    app_on_button(BTN_OK_PRESSED); app_on_terminate();
    state_t actual;
    assert(app_state_load(STATE_KEY, &actual, sizeof actual, STATE_VERSION) == HOST_OK);
    assert(!memcmp(&actual, &original, sizeof actual));
    app_on_load(); assert(preferences_ready && state.rotating == 1);
    quote_t first = { .version = QUOTE_VERSION, .has_data = 1, .price = 12345, .item = "AAPL" };
    assert(write_quote(0, &first));
    failed_read = true;
    int writes = standin()->file_writes;
    const char body[] = "{\"chart\":{\"result\":[{\"meta\":{\"regularMarketPrice\":200}}]}}";
    standin_picked(POLL_YAHOO, 0, 200, body, sizeof body - 1);
    const char candles[] = "{\"candles\":{\"data\":[[200]]}}";
    standin_picked(POLL_MOEX_DAY, 1, 200, candles, sizeof candles - 1);
    assert(standin()->file_writes == writes);
    failed_read = false; failed_size = true;
    assert(!write_quote(1, &first) && standin()->file_writes == writes);
    failed_size = false;
    quote_t record;
    assert(read_quote(0, &record) && !memcmp(&record, &first, sizeof record));
    assert(write_quote(1, &first));
    assert(read_quote(1, &record) && !memcmp(&record, &first, sizeof record));
}
