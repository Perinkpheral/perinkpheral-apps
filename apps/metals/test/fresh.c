// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../metals.c"
#include "standin.h"

static void answer(const char *gold)
{
    char body[512];
    snprintf(body, sizeof body, "<Metall><Record Date=\"17.11.2026\" Code=\"1\"><Buy>%s</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"2\"><Buy>2</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"3\"><Buy>3</Buy></Record>"
        "<Record Date=\"17.11.2026\" Code=\"4\"><Buy>4</Buy></Record></Metall>", gold);
    app_on_data(REQUEST, (const uint8_t *)body, (int32_t)strlen(body), HOST_DATA_FIRST | HOST_DATA_LAST, 200);
}
int main(void)
{
    standin_reset(); standin()->is_session_writable = true;
    app_on_load(); app_on_focus();
    standin()->refuse_writes = HOST_ERR_FAILED;
    answer("100"); app_on_blur(); app_on_terminate();
    app_on_load(); app_on_focus();
    answer("200");
    assert(month.days[0].prices[0] == 20000);
    app_on_data(REQUEST, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_NETWORK);
    assert(month.days[0].prices[0] == 20000);
    app_on_blur(); app_on_terminate(); app_on_load(); app_on_focus();
    assert(month.days[0].prices[0] == 20000);
    standin()->refuse_writes = HOST_OK;
    app_on_terminate(); app_on_load();
    assert(month.days[0].prices[0] == 20000);
}
