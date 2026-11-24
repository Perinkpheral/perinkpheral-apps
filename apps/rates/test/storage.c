// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../rates.c"
#include "../history.c"
#include "standin.h"

static void daily(int value)
{
    char body[256];
    snprintf(body, sizeof body, "{\"Date\":\"2026-11-18T11:30:00+03:00\",\"Valute\":{\"USD\":{\"ID\":\"R01235\",\"Nominal\":1,\"Value\":%d}}}", value);
    standin_data(0, -1, 200, body, strlen(body), 7, STANDIN_WHOLE, 0);
}

int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_draw = app_on_draw;
    standin_set("codes", "USD");
    app_on_load();
    const int errors[] = { HOST_ERR_NO_SPACE, HOST_ERR_FAILED };
    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; ++i) {
        standin()->refuse_writes = errors[i];
        daily(82 + i);
        standin_draw();
        assert(strstr(standin()->draw_text, "Could not save."));
        state_t pending = state;
        app_on_terminate();
        app_on_load();
        assert(memcmp(&state, &pending, sizeof state));
        standin()->refuse_writes = HOST_OK;
        daily(82 + i);
        standin_draw();
        assert(!strstr(standin()->draw_text, "Could not save."));
        state_t expected = state;
        state = (state_t){0};
        app_on_load();
        assert(!memcmp(&state, &expected, sizeof state));
    }
    return 0;
}
