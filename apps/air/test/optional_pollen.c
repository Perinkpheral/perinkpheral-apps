// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../air.c"
#include "standin.h"

int main(void)
{
    const char *fields[] = { "alder", "birch", "grass", "mugwort", "ragweed", "olive" };
    const char *invalid[] = { "null", "9223372036854775807", "\"invalid\"" };
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    app_on_load();
    for (int field = 0; field < 6; field++) {
        for (int variant = 0; variant < 3; variant++) {
            char body[1024];
            int length = snprintf(body, sizeof body,
                "{\"current\":{\"european_aqi\":20,\"us_aqi\":40,\"pm2_5\":5,\"pm10\":8,"
                "\"uv_index\":1,\"ozone\":2,\"nitrogen_dioxide\":3,\"sulphur_dioxide\":4,"
                "\"carbon_monoxide\":5,\"dust\":6");
            for (int pollen = 0; pollen < 6; pollen++) {
                length += snprintf(body + length, sizeof body - length, ",\"%s_pollen\":%s", fields[pollen],
                                   pollen == field ? invalid[variant] : "182");
            }
            length += snprintf(body + length, sizeof body - length, "}}");
            standin_picked(0, -1, 200, body, length);
            assert(state.has_data && state.aqi[0] == 20 && state.irritants[AIR_ALDER + field] == AIR_MISSING);
            for (int pollen = 0; pollen < 6; pollen++) {
                if (pollen != field) { assert(state.irritants[AIR_ALDER + pollen] == 1820); }
            }
            state = (state_t){0}; app_on_load();
            assert(state.has_data && state.irritants[AIR_ALDER + field] == AIR_MISSING);
        }
    }
}
