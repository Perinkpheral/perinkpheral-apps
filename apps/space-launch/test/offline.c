// SPDX-License-Identifier: MIT
/* A known offline result explains empty statistics before their own paced request. */
#include <assert.h>
#include <stdio.h>
#define ui_draw_reason record_reason
#include "../launch.c"
#undef ui_draw_reason
#include "standin.h"
#include "fixtures.h"

static int32_t reason;
int32_t ui_draw_reason(int32_t);
int32_t record_reason(int32_t status) { reason = status; return ui_draw_reason(status); }

int main(void)
{
    standin_reset(); standin_handlers.on_draw = app_on_draw; app_on_load();
    answer_text(POLL_UPCOMING, -1, HOST_FAILED_NO_NETWORK, "{}");
    assert(state.status[POLL_YEAR] == 0);
    for (int away = 0; away < 2; away++) {
        if (away) { app_on_terminate(); app_on_load(); }
        for (int screen = SCREEN_COUNTRIES; screen <= SCREEN_MONTHS; screen++) {
            place.nav.screen = screen; reason = 123;
            int writes = flash_writes();
            standin_draw();
            assert(reason == HOST_FAILED_NO_NETWORK && flash_writes() == writes);
        }
    }
    /* Recovery must remove the inferred reason, even while annual data is pending. */
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    reason = 123; standin_draw(); assert(reason == 0 && state.status[POLL_YEAR] == 0);
    answer_text(POLL_YEAR, 0, HOST_FAILED_TIMEOUT, "{}");
    reason = 123; standin_draw(); assert(reason == HOST_FAILED_TIMEOUT);
    puts("space-launch: offline statistics and recovery use the known reason");
    return 0;
}
