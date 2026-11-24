// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define session_set save_session
#include "../iss.c"
#undef session_set
#include "../orbit.c"
#include "standin.h"

static bool refuse_session;
int32_t session_set(const void *bytes, int32_t length);
int32_t save_session(const void *bytes, int32_t length)
{
    return refuse_session ? HOST_ERR_NO_SPACE : session_set(bytes, length);
}

static void answer(int status, int second)
{
    char body[256];
    int n = __builtin_snprintf(body, sizeof body,
        "{\"latitude\":51,\"longitude\":4,\"altitude\":420,\"velocity\":27000,\"timestamp\":%lld}",
        (long long)standin_subject.at + second);
    standin_picked(0, -1, status, body, n);
}
int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin()->is_session_writable = true;
    app_on_load(); answer(200, 0);
    answer(200, 60);
    standin()->refuse_writes = HOST_ERR_FAILED;
    answer(503, 60); app_on_blur();
    app_on_load(); app_on_focus();
    assert(state.status == 503 && state.second == 0 && state.minute == (standin_subject.at + 60) / 60);
    /* A failure at the exact persisted fix time must also win over stale KV status. */
    standin()->refuse_writes = HOST_OK; app_on_terminate();
    app_on_load(); assert(state.status == 503);
    answer(200, 120); app_on_terminate();
    app_on_load(); app_on_focus();
    assert(state.status == 200 && state.minute == (standin_subject.at + 120) / 60);
    /* A refused session allocation falls back to persistence before unload. */
    refuse_session = true;
    answer(200, 150); app_on_blur(); app_on_load();
    assert(state.minute == (standin_subject.at + 150) / 60 && state.second == 30);
    refuse_session = false;
    /* An off-screen fix between checkpoints survives termination too. */
    standin()->is_session_writable = false;
    answer(200, 180); app_on_terminate(); app_on_load();
    assert(state.minute == (standin_subject.at + 180) / 60);
    return 0;
}
