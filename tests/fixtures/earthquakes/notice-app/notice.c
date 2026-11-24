// SPDX-License-Identifier: MIT
/* Visual fixture only. Inject a newly observed captured report after setup;
 * production deliberately never notifies from its bootstrap response. The
 * production observation, persistence and notification renderer are exercised. */
#include "host_api.h"
#define app_on_focus earthquakes_focus
#define app_on_timer earthquakes_timer
#include "production.h"
#undef app_on_focus
#undef app_on_timer
void app_on_focus(void) { earthquakes_focus(); timer_set(1, 1000); }
void app_on_timer(int32_t id)
{
    if (id != 1) { earthquakes_timer(id); return; }
    event_t e = { .id = "us6000ucv1", .aliases = ",us6000ucv1,", .place = "33 km WSW of Pánormos, Greece",
        .lat = 36860000, .lon = 26591300, .mag = 42, .depth = 15,
        .time = 1795390969074LL, .updated = 1795393022040LL };
    observe(&e, false);
    try_notice();
}
