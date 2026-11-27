// SPDX-License-Identifier: MIT
/* Preserve unreadable snapshots and committed identity history, even after partial copies. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define file_read read_with_failure
#include "../earthquakes.c"
#undef file_read
#include "standin.h"

int32_t file_read(const char *, int32_t, int32_t, void *, int32_t);
static const char *failed_file;
int32_t read_with_failure(const char *name, int32_t namesz, int32_t offset, void *out, int32_t size)
{
    if (failed_file && !strncmp(name, failed_file, (size_t)namesz)) {
        memset(out, 0xa5, (size_t)size / 2);
        return HOST_ERR_FAILED;
    }
    return file_read(name, namesz, offset, out, size);
}

static void snapshot_failure(void)
{
    standin_reset(); app_on_load();
    state.seed = 0; cache.count[0] = 1;
    copy_text(cache.events[0][0].id, sizeof cache.events[0][0].id, "retained");
    assert(save_cache());
    cache_t before = cache;
    failed_file = "cache"; app_on_load(); assert(cache_read_failed && cache_unsaved);
    failed_file = NULL;
    int files = standin()->file_writes;
    app_on_settings(); app_on_intent(HOST_INTENT_STARTED);
    app_on_alarm(0); app_on_terminate();
    assert(!save_cache() && standin()->file_writes == files);
    int32_t size;
    const uint8_t *bytes = standin_file("cache", &size);
    assert(size == sizeof before && !memcmp(bytes, &before, sizeof before));
    app_on_load(); assert(!cache_read_failed && cache.count[0] == 1 && !state.seed);
    assert(app_text_equal(cache.events[0][0].id, "retained"));
}

static void history_failure(void)
{
    standin_reset(); app_on_load();
    cache.boundary_stored = 1; cache.boundary_count = 1;
    cache.browse_end = time_now() * 1000;
    copy_text(cache.boundary[0].id, sizeof cache.boundary[0].id, "new-id");
    boundary_record_t record = { .id = { .id = "old-id", .until = cache.browse_end } };
    record.checksum = hash_bytes(&record.id, sizeof record.id);
    assert(file_write("history", 7, 0, &record, sizeof record) == HOST_OK && save_cache());
    failed_file = "history"; app_on_load(); assert(history_read_failed && cache_unsaved);
    failed_file = NULL;
    int files = standin()->file_writes;
    assert(!store_boundary());
    app_on_settings(); app_on_terminate();
    assert(standin()->file_writes == files);
    int32_t size;
    const uint8_t *bytes = standin_file("history", &size);
    assert(size == sizeof record && !memcmp(bytes, &record, sizeof record));
    app_on_load(); assert(!history_read_failed && cache.boundary_stored == 1);
    assert(store_boundary());
    assert(file_size("history", 7) == 2 * sizeof record);
}

int main(void)
{
    snapshot_failure(); history_failure();
    puts("earthquakes: failed snapshot/history reads preserve durable data and recover after restore");
    return 0;
}
