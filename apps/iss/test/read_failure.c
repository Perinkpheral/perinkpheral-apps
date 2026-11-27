// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../iss.c"
#include "../orbit.c"
#include "standin.h"

int main(void)
{
    for (int version = 1; version <= VERSION; version++) {
        standin_reset();
        standin_handlers.on_picked = app_on_picked;
        state_t saved = { .version = version, .has_data = 1, .status = 200,
                          .altitude = version == 1 ? 421 : 421000,
                          .minute = (int32_t)(standin_subject.at / 60) };
        assert(app_state_save("state", &saved, sizeof saved));
        int32_t writes = standin()->store_writes;
        standin()->refuse_reads = HOST_ERR_FAILED;
        app_on_load();
        assert(!state_readable && !state.has_data && state.version == VERSION);
        assert(standin()->refused_reads == 1); /* Never try v1 after a read error. */
        /* The read fault can clear before the next delivery. Defaults and live
         * updates still cannot replace the state that this instance failed to read. */
        standin()->refuse_reads = HOST_OK;
        const char body[] = "{\"latitude\":51,\"longitude\":4,\"altitude\":422,"
                            "\"velocity\":27000,\"timestamp\":1794956280}";
        standin_picked(0, -1, 200, body, sizeof body - 1);
        assert(state.has_data && state_dirty);
        standin_refuse_session(HOST_ERR_NO_SPACE);
        app_on_blur(); app_on_terminate();
        int32_t length;
        const void *bytes = standin_value("state", &length);
        assert(length == sizeof saved && !memcmp(bytes, &saved, sizeof saved));
        assert(standin()->store_writes == writes);
        app_on_load();
        assert(state_readable && state.altitude == 421000 && state.has_data);
        /* Once restored, another good fix can be checkpointed. */
        standin_picked(0, -1, 200, body, sizeof body - 1);
        app_on_terminate();
        bytes = standin_value("state", &length);
        state_t restored;
        memcpy(&restored, bytes, sizeof restored);
        assert(restored.version == VERSION && restored.altitude == 422000);
    }
    return 0;
}
