// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../air.c"
#include "standin.h"

static void refused_reading(int32_t failure)
{
    static const char body[] =
        "{\"current\":{\"european_aqi\":20,\"us_aqi\":40,\"pm2_5\":5,\"pm10\":8,"
        "\"uv_index\":1,\"ozone\":2,\"nitrogen_dioxide\":3,\"sulphur_dioxide\":4,"
        "\"carbon_monoxide\":5,\"dust\":6}}";
    standin()->refuse_writes = failure;
    standin_picked(0, -1, 200, body, sizeof body - 1);
    standin_draw();
    assert(strstr(standin()->draw_text, "Could not save.") != NULL);
    standin()->refuse_writes = HOST_OK;
}

int main(void)
{
    standin_handlers.on_picked = app_on_picked;
    standin_handlers.on_draw = app_on_draw;
    const int32_t failures[] = { HOST_ERR_NO_SPACE, HOST_ERR_FAILED };
    for (int32_t i = 0; i < 2; i++) {
        standin_reset();
        standin_set("lat", "52.52"); standin_set("lon", "13.41");
        app_on_load();
        refused_reading(failures[i]);
        /* An unrelated setting must keep the current reading's warning. */
        standin_set("scale", "US"); app_on_settings();
        standin_draw();
        assert(strstr(standin()->draw_text, "Could not save.") != NULL);
        standin_set("lat", NULL); standin_set("lon", NULL);
        app_on_settings(); standin_draw();
        assert(!state.has_data && !state_unsaved);
        assert(strstr(standin()->draw_text, "Could not save.") == NULL);
        assert(standin()->visible_failure);
        uint64_t unconfigured = standin()->picture;
        app_on_terminate(); app_on_load(); app_on_focus();
        assert(standin_draw() == unconfigured);
        /* Replacing the location also invalidates the failed reading. */
        standin_set("lat", "52.52"); standin_set("lon", "13.41");
        refused_reading(failures[i]);
        standin_set("lat", "59.94"); standin_set("lon", "30.31");
        app_on_settings(); standin_draw();
        assert(!state.has_data && !state_unsaved);
        assert(strstr(standin()->draw_text, "Could not save.") == NULL);
    }
}
