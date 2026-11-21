// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ha.c"
#include "standin.h"

/* Every kv_set the application tried, refused or not. */
static int writes(void) { return standin()->store_writes + standin()->refused_writes; }

/* A whole answer in one piece. */
static void answer(const char *payload)
{
    standin_data(request_screen(), -1, 200, payload, (int32_t)strlen(payload), HOST_DATA_CHUNK_MAX, STANDIN_WHOLE, 0);
}
int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_set("every", "5 min");
    standin()->refuse_requests = HOST_ERR_NO_SPACE;
    app_on_load(); app_on_focus();
    assert(!has_asked && last_status == HOST_FAILED_NO_MEMORY && standin()->redraws);
    standin()->refuse_requests = HOST_OK; app_on_focus();
    assert(has_asked && standin()->sends == 2);
    answer("hero|21|sunny");
    int first_writes = writes();
    for (int poll = 0; poll < 288; poll++) answer("hero|21|sunny");
    assert(writes() == first_writes);
    /* The first piece, and then the request fails. */
    standin_data(request_screen(), -1, 200, "partial", 7, 7, STANDIN_FAILED, HOST_FAILED_NO_NETWORK);
    assert(!strcmp(screen, "hero|21|sunny"));
    /* A last piece of an answer that never began: the host cuts none like it, so it is handed over as it is. */
    app_on_data(request_screen(), (const uint8_t *)"orphan", 6, HOST_DATA_LAST, 200);
    assert(!strcmp(screen, "hero|21|sunny"));
    standin()->refuse_writes = HOST_ERR_NO_SPACE; answer("hero|22|sunny"); assert(screen_dirty);
    standin()->refuse_writes = HOST_OK; answer("hero|22|sunny"); assert(!screen_dirty);
    screen[0] = 0; app_on_load(); assert(!strcmp(screen, "hero|22|sunny"));
    standin()->refuse_requests = HOST_ERR_ARGUMENT; ask();
    assert(last_status == HOST_FAILED_NOT_READY && !has_asked);
    /* A background unload restores the session without asking again. */
    standin()->is_session_writable = true;
    standin()->refuse_requests = HOST_OK;
    standin_set("every", "1 h");
    app_on_settings();
    list.first = 3; list.count = 12;
    app_on_blur();
    int sends = standin()->sends;
    has_asked = false; list.first = list.count = 0;
    app_on_load(); app_on_focus();
    assert(standin()->sends == sends && list.first == 3);
    assert(standin()->alarms[0].every == 3600);
    standin_pass(59000);
    app_on_blur(); last_ask_ms = -1; has_asked = false;
    app_on_load(); app_on_focus();
    assert(standin()->sends == sends);
    standin_pass(1000);
    app_on_blur(); last_ask_ms = -1; has_asked = false;
    app_on_load(); app_on_focus();
    assert(standin()->sends == sends + 1 && list.first == 3);
    assert(standin()->alarms[0].every == 3600);
    app_on_blur(); last_ask_ms = -1; has_asked = false;
    app_on_load(); app_on_focus();
    assert(standin()->sends == sends + 1);
    app_on_alarm(0); assert(standin()->sends == sends + 2);

    int32_t old_request = request_screen();
    app_on_data(old_request, (const uint8_t *)"hero|99", 7, HOST_DATA_FIRST, 200);
    standin_set("server", "https://new.invalid");
    standin_set("token", "new-pairing");
    standin()->refuse_writes = HOST_ERR_NO_SPACE;
    app_on_settings();
    assert(screen_length == 0 && !receiving);
    app_on_data(old_request, (const uint8_t *)"|sunny", 6, HOST_DATA_LAST, 200);
    assert(screen_length == 0);
    /* Even a whole late response from the old pairing is ignored after a reload. */
    app_on_load();
    standin_data(old_request, -1, 200, "hero|99|sunny", 13, 1, STANDIN_WHOLE, 0);
    assert(screen_length == 0);
    standin()->refuse_writes = HOST_OK;
    app_on_alarm(0);
    answer("hero|23|sunny"); assert(screen_length > 0);
    standin_set("token", "another-pairing"); app_on_settings();
    assert(screen_length == 0);
    app_on_load(); assert(screen_length == 0);

    /* Re-pairing off screen must discard both the cache and its error. */
    answer("hero|24|sunny");
    last_status = 401; save_status();
    standin_set("server", "https://other.invalid");
    app_on_load();
    assert(screen_length == 0 && last_status == 0 && saved_status == 0);
    int32_t status_bytes;
    const uint8_t *status_value = standin_value("status", &status_bytes);
    int32_t stored_status; memcpy(&stored_status, status_value, sizeof stored_status);
    assert(status_bytes == sizeof stored_status && stored_status == 0);
    app_on_settings();
    assert(last_status == 0);
    app_on_data(request_screen(), NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
    app_on_load();
    assert(screen_length == 0 && last_status == HOST_FAILED_NO_NETWORK);

    /* A long UTF-8 name must leave the picture and numeric value whole. */
    char payload[512] = "layout|list\nitem|";
    for (int i = 0; i < 35; i++) { strcat(payload, "\xD0\x96"); }
    strcat(payload, "|thermometer|12345.67");
    answer(payload);
    item_t items[ITEMS_MAX];
    assert(read_items(items) == 1 && !strcmp(items[0].value, "12345.67"));
    assert(items[0].picture != NULL && items[0].picture->icon == HOST_ICON_THERMOMETER);
    assert(strlen(items[0].name) == 46);
    puts("ha: pairing invalidation and late replies, restored refresh pace, long UTF-8 labels, refused enqueue, focus retry, 288 unchanged answers, interrupted stream and failed save retry passed");
}
