// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

static const char body[] = "{\"latitude\":51.23,\"longitude\":-4.56,\"altitude\":421.123,"
    "\"velocity\":27581,\"timestamp\":1794955920}";

static void picked(int32_t status, const char *answer)
{
    standin_picked(0, -1, status, answer, (int32_t)strlen(answer));
}

int main(void)
{
    standin_handlers = (standin_handlers_t){ .on_picked = app_on_picked, .on_draw = app_on_draw };
    standin_reset();
    app_on_load();
    picked(200, body);
    state_t good = state;
    uint64_t current = standin_draw();
    const int32_t failures[] = { 503, HOST_FAILED_NO_NETWORK, 200 };
    for (int32_t i = 0; i < (int32_t)(sizeof failures / sizeof failures[0]); i++) {
        const char *answer = failures[i] == 200 ? "{}" : body;
        picked(failures[i], answer);
        uint64_t failed = standin_draw();
        assert(failed != current && standin()->refused_draws == 0);
        state_t retained = state;
        retained.status = good.status;
        assert(memcmp(&retained, &good, sizeof good) == 0);
        int32_t writes = standin()->store_writes;
        picked(failures[i], answer);
        assert(standin_draw() == failed && standin()->store_writes == writes);

        /* Only the observation time changes: it must be visible in the failed frame. */
        state.minute -= 60;
        assert(standin_draw() != failed);
        state.minute += 60;
        app_on_load();
        assert(standin_draw() == failed);
        picked(200, body);
        assert(standin_draw() == current);
    }
    assert(standin()->stray_draws == 0);
    return 0;
}
