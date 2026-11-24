// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#define answer_info retry_info
#include "../rates.c"
#undef answer_info
#include "../history.c"
#include "standin.h"
int32_t retry_info(host_answer_info_t *info, int32_t capacity)
{
    assert(capacity == sizeof *info);
    *info = (host_answer_info_t){ .retry_after = 600 };
    return sizeof *info;
}
static void daily(void)
{
    const char *body = "{\"Date\":\"2026-11-17T11:30:00+03:00\",\"Valute\":{\"USD\":{\"ID\":\"R01235\",\"Value\":82.4529,\"Nominal\":1}}}";
    app_on_data(0, (const uint8_t *)body, (int32_t)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
}
static void reload(void)
{
    app_on_blur(); app_on_terminate(); app_on_load(); app_on_focus();
}
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    standin_set("codes", "USD");
    app_on_load(); app_on_focus();
    standin()->refuse_requests = HOST_ERR_NO_SPACE;
    daily(); assert(standin()->sends == 1 && standin()->request_count == 0);
    assert(standin_alarm_left(HISTORY_ALARM) == 60);
    standin()->refuse_requests = HOST_OK;
    reload(); assert(standin()->sends == 1);
    standin_pass(60000); reload(); assert(standin()->sends == 2);
    reload(); assert(standin()->sends == 2);
    /* An interrupted raw GET has no terminal callback and is not replayed by the OS. */
    standin()->request_count = 0;
    app_on_data(HOST_REQUEST_FIRST, (const uint8_t *)"<ValCurs", 8, HOST_DATA_FIRST, 200);
    reload(); assert(standin()->sends == 2);
    standin_pass(330000); reload(); assert(standin()->sends == 3);
    standin()->request_count = 0;
    app_on_data(HOST_REQUEST_FIRST, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
    standin_pass(59000); reload(); assert(standin()->sends == 3);
    standin_pass(1000); reload(); assert(standin()->sends == 4);
    standin()->request_count = 0;
    app_on_data(HOST_REQUEST_FIRST, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 429);
    standin_pass(599000); reload(); assert(standin()->sends == 4);
    standin_pass(1000); reload(); assert(standin()->sends == 5);
    /* Failed history-file writes and malformed answers recover without a daily poll. */
    const char *body = "<ValCurs ID=\"R01235\" DateRange2=\"17.11.2026\">"
        "<Record Id=\"R01235\" Date=\"17.11.2026\"><Nominal>1</Nominal><Value>82,45</Value></Record></ValCurs>";
    standin()->request_count = 0;
    standin()->refuse_writes = HOST_ERR_FAILED;
    app_on_data(HOST_REQUEST_FIRST, (const uint8_t *)body, (int32_t)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(!chart.history.count);
    standin()->refuse_writes = HOST_OK;
    app_on_terminate();
    standin_pass(60000); app_on_load(); app_on_alarm(HISTORY_ALARM);
    assert(standin()->sends == 6);
    standin()->request_count = 0;
    app_on_data(HOST_REQUEST_FIRST, (const uint8_t *)"<broken>", 8, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(standin_alarm_left(HISTORY_ALARM) == 60);
    standin_pass(60000); app_on_alarm(HISTORY_ALARM); assert(standin()->sends == 7);
    standin()->request_count = 0;
    app_on_data(HOST_REQUEST_FIRST, (const uint8_t *)body, (int32_t)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    assert(chart.history.count == 1 && standin_alarm_left(HISTORY_ALARM) == 0);
    standin_pass(3600000); reload(); assert(standin()->sends == 7);
    /* A restart restores the retry alarm, and discards deadlines from the old boot. */
    cached_history_t empty = {0};
    assert(file_write(HISTORY_FILE, HISTORY_FILE_LENGTH, 0, &empty, sizeof empty) == HOST_OK);
    standin()->boot_id++;
    app_on_load(); app_on_intent(HOST_INTENT_STARTED);
    assert(standin()->sends == 8 && standin_alarm_left(HISTORY_ALARM) == 330);
}
