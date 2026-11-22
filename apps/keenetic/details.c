// SPDX-License-Identifier: MIT
#include "host_api.h"
#include "details.h"
#include "app_values.h"
#include "app_text.h"
#include "strings.h"

void keenetic_hosts_read(keenetic_hosts_t *hosts)
{
    __builtin_memset(hosts, 0, sizeof *hosts);
    int count = app_value_count(0), flags = app_value_count(1);
    if (flags < count) count = flags;
    int bytes = 0;
    for (int i = 0; i < count; i++) {
        char values[4][64];
        for (int column = 0; column < 4; column++) {
            app_value_element_text(column, i, values[column], sizeof values[column]);
            bytes += app_text_length(values[column]) + 2;
        }
        /* A dropped element can be followed by shorter ones. Stay before the
         * first possible cut (a picker element occupies at most 65 bytes), so
         * shifted names/flags can never be paired with a later IP. */
        if (bytes > HOST_LIST_BYTES - (HOST_VALUE_MAX + 2)) break;
        char *ip = values[0], *name = values[2];
        if (!app_text_equal(values[1], "true") || !*ip ||
            app_text_equal(ip, "null") || app_text_length(ip) >= 40) continue;
        hosts->total++;
        if (hosts->count == KEENETIC_HOSTS_MAX) continue;
        keenetic_host_t *row = &hosts->rows[hosts->count++];
        app_text_copy(row->ip, sizeof row->ip, ip);
        if (!*name || app_text_equal(name, "null")) name = values[3];
        if (!*name || app_text_equal(name, "null")) name = (char *)app_string(STR_UNKNOWN);
        app_text_copy(row->name, sizeof row->name, name);
    }
}
