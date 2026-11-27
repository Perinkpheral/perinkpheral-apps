// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

int main(void)
{
    const int64_t moments[] = { HOST_CLOCK_MIN - 1, HOST_CLOCK_MIN,
                                HOST_CLOCK_MAX, HOST_CLOCK_MAX + 1 };
    for (int32_t i = 0; i < 4; i++) {
        standin_reset();
        standin_handlers.on_picked = app_on_picked;
        app_on_load();
        char body[192];
        int32_t length = snprintf(body, sizeof body,
            "{\"latitude\":51,\"longitude\":4,\"altitude\":421,"
            "\"velocity\":27000,\"timestamp\":%lld}", (long long)moments[i]);
        standin_picked(0, -1, 200, body, length);
        bool valid = i == 1 || i == 2;
        assert(state.has_data == valid);
        assert(state.status == (valid ? 200 : HOST_FAILED_NO_ANSWER));
        if (valid) {
            assert((int64_t)state.minute * SECONDS_PER_MINUTE + state.second == moments[i]);
        }
    }
    return 0;
}
