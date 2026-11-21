// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../air.c"
#include "standin.h"

static void answer(int aqi)
{
    char body[512];
    int length = snprintf(body, sizeof body,
        "{\"current\":{\"european_aqi\":%d,\"us_aqi\":40,\"pm2_5\":5,\"pm10\":8,"
        "\"uv_index\":1,\"ozone\":2,\"nitrogen_dioxide\":3,\"sulphur_dioxide\":4,"
        "\"carbon_monoxide\":5,\"dust\":6}}", aqi);
    standin_picked(0, -1, 200, body, length);
}

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin_set("lat", "52.52"); standin_set("lon", "13.41");
    app_on_load(); answer(20);
    assert(state.has_data);
    int written = standin()->store_writes;
    for (int i = 0; i < 96; i++) { standin_pass(900000); answer(20); }
    assert(standin()->store_writes == written && !state_unsaved);
    standin()->refuse_writes = HOST_ERR_NO_SPACE; answer(21);
    assert(state_unsaved && state.aqi[0] == 21);
    standin_pass(900000);
    standin()->refuse_writes = HOST_OK; answer(21);
    assert(!state_unsaved && standin()->store_writes == written + 1);
    state = (state_t){0}; app_on_load();
    assert(state.aqi[0] == 21);
    puts("air: 96 identical readings over a day write nothing; refused changed readings retry passed");
}
