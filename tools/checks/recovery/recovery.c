// SPDX-License-Identifier: MIT
/* A checker specimen: a closed poll gate and a two-minute failure backoff. */
#include "host_api.h"

#define RECOVERY_BROKEN 0

static struct { int64_t due; int32_t have, failed; } state;

static void fetch(void)
{
    if (time_now() < state.due || (RECOVERY_BROKEN && state.failed)) { return; }
    kv_set("gate", 4, "1", 1);
    poll_run(0);
}

void app_on_load(void)
{
    kv_get("state", 5, &state, sizeof state);
    alarm_set_every(0, 60, 60);
}

void app_on_focus(void) { fetch(); }
void app_on_installed(void) { fetch(); }
void app_on_alarm(int32_t id) { (void)id; fetch(); }

void app_on_picked(int32_t poll, int32_t status)
{
    (void)poll;
    char gate;
    if (kv_get("gate", 4, &gate, 1) != 1) { return; }
    kv_set("gate", 4, "", 0);
    if (status == 200) { state.have = 1; }
    else { state.failed = 1; }
    state.due = time_now() + (status == 200 ? 300 : 120);
    kv_set("state", 5, &state, sizeof state);
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_draw(void)
{
    const char *text = state.have ? "Recovered" : "Waiting";
    gfx_draw_text(10, 10, HOST_FONT_TEXT, text, state.have ? 9 : 7);
}
