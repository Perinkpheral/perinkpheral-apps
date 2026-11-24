// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../rates.c"
#include "../history.c"
#include "standin.h"

static bool quote(const char *record)
{
    char body[1024];
    snprintf(body, sizeof body, "{\"Date\":\"2026-11-18T11:30:00+03:00\",\"Valute\":{\"USD\":%s}}", record);
    begin_reading();
    for (size_t i = 0; i < strlen(body); ++i)
        for (int group = 0; group < active_pickers; ++group)
            assert(json_pick_feed(&pickers[group], body + i, 1));
    return take_response();
}

int main(void)
{
    standin_reset();
    standin_set("codes", "USD");
    app_on_load();
    assert(quote("{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":82.4529,\"Name\":\"Dollar\"}"));
    state_t good = state;
    const char *invalid[] = {
        "null", "{}", "{\"ID\":null,\"Nominal\":1,\"Value\":83}",
        "{\"ID\":\"bad/id\",\"Nominal\":1,\"Value\":83}",
        "{\"ID\":\"R01235\",\"Nominal\":null,\"Value\":83}",
        "{\"ID\":\"R01235\",\"Nominal\":0,\"Value\":83}",
        "{\"ID\":\"R01235\",\"Nominal\":9223372036854775807,\"Value\":83}",
        "{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":null}",
        "{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":-1}",
        "{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":9223372036854775807}",
        "{\"ID\":\"R01235\",\"Value\":83}", "{\"ID\":\"R01235\",\"Nominal\":1}"
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        int writes = standin()->store_writes;
        assert(!quote(invalid[i]));
        assert(!memcmp(&state, &good, sizeof state));
        assert(standin()->store_writes == writes);
    }
    assert(quote("{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":82.4529,\"Name\":null}"));
    assert(state.rates[0].name[0] == 0);
    app_on_load();
    assert(state.rates[0].nominal == 1 && state.rates[0].value == 824529);
    assert(state.rates[0].name[0] == 0);
    return 0;
}
