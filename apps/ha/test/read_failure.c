// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define kv_get failing_read
#include "../ha.c"
#undef kv_get
#include "standin.h"

int32_t kv_get(const char *, int32_t, void *, int32_t);
static const char *failed_key;
static int32_t read_error;

int32_t failing_read(const char *key, int32_t length, void *out, int32_t capacity)
{
    int32_t result = kv_get(key, length, out, capacity);
    if (failed_key != NULL && (int32_t)strlen(failed_key) == length &&
        !memcmp(key, failed_key, length)) {
        /* A failed read may have copied only part of a value. */
        memset(out, 0x5a, capacity / 2);
        return read_error;
    }
    return result;
}

static const char *const keys[] = { "cooldown", "source", "screen", "status", "server", "token", "page" };
typedef struct { int32_t length; uint8_t bytes[HOST_KV_VALUE_MAX]; } snapshot_t;

static void snapshot(snapshot_t *values)
{
    for (int i = 0; i < 7; i++) {
        values[i].length = kv_get(keys[i], (int32_t)strlen(keys[i]), values[i].bytes, sizeof values[i].bytes);
        assert(values[i].length > 0);
    }
}

static void seed(void)
{
    standin_reset();
    failed_key = NULL;
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_draw = app_on_draw;
    standin()->is_session_writable = true;
    standin_set("server", "http://homeassistant.example.test:8123");
    standin_set("token", "pairing-secret");
    standin_set("page", "2");
    uint32_t identity = pairing();
    request_source_t original = { .pairing = identity, .tag = HOST_REQUEST_FIRST + 10, .page = 2 };
    cached_screen_t cache = { .version = SCREEN_VERSION, .pairing = identity,
                             .text = "page|2|3\nhero|21|sunny" };
    cooldown_t delay = { .boot = time_boot_id(), .retry_at = time_uptime_ms() + 60000 };
    int32_t status = 503;
    assert(app_state_save("source", &original, sizeof original));
    assert(app_state_save("screen", &cache, offsetof(cached_screen_t, text) + (int32_t)strlen(cache.text)));
    assert(app_state_save("cooldown", &delay, sizeof delay));
    assert(app_state_save("status", &status, sizeof status));
}

static void failed_restore(const char *key, int32_t error)
{
    seed();
    snapshot_t before[7], after[7];
    snapshot(before);
    int writes = standin()->store_writes + standin()->refused_writes;
    failed_key = key;
    read_error = error;
    app_on_load();
    assert(restore_failed);
    /* Reads recover before focus, responses and termination, without another load. */
    failed_key = NULL;
    app_on_focus();
    standin_draw();
    assert(strstr(standin()->draw_text, "Storage error."));
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_DOWN_LONG_PRESSED);
    const char body[] = "page|1|3\nhero|99|sunny";
    const standin_pair_t headers[] = { { "Retry-After", "300" } };
    standin_answer_headers(headers, 1);
    standin_data(0, -1, 429, body, sizeof body - 1, 1, STANDIN_WHOLE, 0);
    standin_data(0, -1, 200, body, sizeof body - 1, 1, STANDIN_WHOLE, 0);
    app_on_settings();
    app_on_blur();
    app_on_terminate();
    assert(standin()->store_writes + standin()->refused_writes == writes);
    snapshot(after);
    for (int i = 0; i < 7; i++) {
        assert(after[i].length == before[i].length);
        assert(!memcmp(after[i].bytes, before[i].bytes, before[i].length));
    }
    standin_forget_session();
    app_on_load(); app_on_focus();
    assert(!restore_failed && source_ready && current_page == 2);
    assert(!strcmp(screen, "page|2|3\nhero|21|sunny") && last_status == 503);
    assert(cooldown.retry_at == time_uptime_ms() + 60000);
    standin_draw();
    assert(!strstr(standin()->draw_text, "Storage error."));
    /* A successful restore permits ordinary saves again. */
    const char replacement[] = "page|2|3\nhero|22|sunny";
    standin_data(0, -1, 200, replacement, sizeof replacement - 1, 1, STANDIN_WHOLE, 0);
    assert(standin()->store_writes > writes && !screen_dirty);
    app_on_load();
    assert(!strcmp(screen, replacement));
}

int main(void)
{
    const int32_t errors[] = { HOST_ERR_FAILED, HOST_ERR_NO_SPACE };
    for (int e = 0; e < 2; e++) {
        for (int i = 0; i < 7; i++) { failed_restore(keys[i], errors[e]); }
    }
    /* Missing state on a fresh installation still permits initialization. */
    standin_reset();
    app_on_load();
    assert(!restore_failed && source_ready && standin()->store_writes > 0);
    puts("ha: failed and partial restores preserve all durable state until a successful load");
}
