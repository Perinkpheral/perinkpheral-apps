// SPDX-License-Identifier: MIT
/* The screens: held UP and DOWN switch them round, short ones move in the list or choose a
 * year, OK opens a launch and BACK closes it; the place survives an unload; what a launch's
 * time and status are called; every screen draws, with and without data and a clock. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define time_local_at local_for_test
#define app_time_text text_for_test
#include "../launch.c"
#undef time_local_at
#undef app_time_text
#include "standin.h"
#include "fixtures.h"

int32_t time_local_at(int64_t, host_local_time_t *);
int32_t app_time_text(int64_t, int32_t, char *, int32_t);
static bool honolulu;
int32_t local_for_test(int64_t moment, host_local_time_t *parts)
{
    return time_local_at(honolulu ? moment - 10 * HOUR - time_zone_offset() : moment, parts);
}
int32_t text_for_test(int64_t moment, int32_t style, char *text, int32_t size)
{
    return app_time_text(honolulu ? moment - 10 * HOUR - time_zone_offset() : moment, style, text, size);
}

static void draws(void)
{
    standin()->refused_draws = 0;
    assert(standin_draw() != 0 && standin()->refused_draws == 0 && standin()->stray_draws == 0);
}

static void reload(void)
{
    app_on_blur();
    place = (place_t){ .nav = { .screens = SCREENS } };
    app_on_load();
    app_on_focus();
}

static void navigation(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin()->is_session_writable = true;
    app_on_load();
    app_on_focus();
    for (int32_t screen = 0; screen < SCREENS; screen++) { /* nothing yet: each says why */
        place.nav.screen = screen;
        draws();
    }
    place.nav.screen = 0;
    answer_all();
    fit_rows();
    assert(selected == 10 && place.first == 10 && !strcmp(rows[selected]->name, "Soyuz 2.1b | Progress MS-36 (97P)"));
    draws();

    /* UP goes into the past, DOWN to later launches; the ends hold. */
    app_on_button(BTN_UP_PRESSED);
    assert(selected == 9 && place.first == 9 && !strcmp(rows[9]->name, "Falcon 9 Block 5 | NROL-136"));
    for (int32_t i = 0; i < 30; i++) { app_on_button(BTN_DOWN_PRESSED); }
    assert(selected == 19 && place.first == 16);
    for (int32_t i = 0; i < 30; i++) { app_on_button(BTN_UP_PRESSED); }
    assert(selected == 0 && place.first == 0);
    draws();

    /* OK opens the launch, UP and DOWN browse, BACK closes; BACK on the list leaves. */
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    assert(app_nav_view(&place.nav) == VIEW_LAUNCH && selected == 1);
    draws();
    app_on_button(BTN_DOWN_PRESSED);
    assert(selected == 2 && app_nav_view(&place.nav) == VIEW_LAUNCH);
    app_on_button(BTN_DOWN_LONG_PRESSED); /* held in a view: nothing */
    assert(place.nav.screen == SCREEN_LAUNCHES && app_nav_view(&place.nav) == VIEW_LAUNCH);
    app_on_back();
    assert(app_nav_view(&place.nav) == APP_NAV_ON_SCREEN && !standin()->is_leaving);

    /* Held presses switch the screens round. */
    app_on_button(BTN_UP_LONG_PRESSED);
    assert(place.nav.screen == SCREEN_HISTORY);
    draws();
    app_on_button(BTN_DOWN_LONG_PRESSED);
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(place.nav.screen == SCREEN_COUNTRIES);
    draws();
    app_on_button(BTN_UP_PRESSED); /* Countries has nothing to choose */
    assert(place.nav.screen == SCREEN_COUNTRIES);

    /* Months and History choose their years, within 1957 and this one. */
    app_on_button(BTN_DOWN_LONG_PRESSED);
    assert(place.nav.screen == SCREEN_MONTHS && year_shown(0) == 2026);
    app_on_button(BTN_DOWN_PRESSED);
    assert(year_shown(0) == 2026);
    app_on_button(BTN_UP_PRESSED);
    assert(year_shown(0) == 2025);
    draws();
    app_on_button(BTN_DOWN_LONG_PRESSED);
    for (int32_t i = 0; i < 3; i++) { app_on_button(BTN_UP_PRESSED); }
    assert(place.nav.screen == SCREEN_HISTORY && year_shown(1) == 2023 && year_shown(0) == 2025);
    for (int32_t i = 0; i < 100; i++) { app_on_button(BTN_UP_PRESSED); }
    assert(year_shown(1) == 1957);
    draws();

    /* An unload keeps the screen, the years and the launch. */
    place.selected_id = rows[3]->id;
    reload();
    assert(place.nav.screen == SCREEN_HISTORY && year_shown(1) == 1957 && year_shown(0) == 2025);
    fit_rows();
    assert(selected == 3);

    /* Opened anew: the list at the next launch. */
    standin_forget_session();
    place = (place_t){ .nav = { .screens = SCREENS } };
    app_on_focus();
    assert(place.nav.screen == SCREEN_LAUNCHES && selected == 10);
    standin_handlers.on_back = app_on_back;
    standin()->is_on_screen = true;
    standin_press(BTN_BACK);
    assert(standin()->is_leaving);
}

static void words(void)
{
    char text[64];
    int64_t now = time_now();
    launch_t launch = { .status = ST_GO, .precision = 1, .net = now + 30 * MINUTE };
    seen_at = now;
    say_in(&launch, now, text, sizeof text);
    assert(!strcmp(text, "Within 1 h"));
    say_day(&launch, HOST_TIME_NEAR_DAY, text, sizeof text);
    assert(!strcmp(text, "20:30"));
    launch.net = now + 17 * HOUR + 10 * MINUTE;
    say_in(&launch, now, text, sizeof text);
    assert(!strcmp(text, "in 17 h"));
    say_day(&launch, HOST_TIME_NEAR_DAY, text, sizeof text);
    assert(!strcmp(text, "Tmrw"));
    launch.net = now + 3 * DAY;
    say_in(&launch, now, text, sizeof text);
    assert(!strcmp(text, "in 3 d"));
    seen_at = now - 2 * DAY; /* from a list this old, no countdown */
    say_in(&launch, now, text, sizeof text);
    assert(text[0] == '\0');
    seen_at = now;
    launch.precision = 7; /* a month */
    say_day(&launch, HOST_TIME_DATE, text, sizeof text);
    assert(!strcmp(text, "Nov 2026"));
    say_in(&launch, now, text, sizeof text);
    assert(text[0] == '\0');
    launch.precision = 14; /* a year */
    say_day(&launch, HOST_TIME_DATE, text, sizeof text);
    assert(!strcmp(text, "2026"));
    launch.net = now - 10 * MINUTE;
    launch.precision = 1;
    assert(status_word(&launch, now) == STR_AWAITING);
    launch.status = ST_HOLD;
    assert(status_word(&launch, now) == STR_STATUS_HOLD);
    launch.status = 200;
    assert(status_word(&launch, now) == STR_STATUS_UNKNOWN);
    char rocket[TEXT_MAX];
    launch_t named = { .name = "Starship | Flight 12" };
    assert(!strcmp(split_name(&named, rocket), "Flight 12") && !strcmp(rocket, "Starship"));
    app_text_copy(named.name, sizeof named.name, "Mystery");
    assert(!strcmp(split_name(&named, rocket), "Mystery") && rocket[0] == '\0');
}

static void header(void)
{
    /* The list taken 40 minutes ago or more: the header says when. */
    standin()->is_on_screen = true;
    seen_at = time_now() - 39 * MINUTE;
    update_header();
    assert(standin()->header_text[HOST_HEADER_LEFT][0] == '\0');
    seen_at = time_now() - 41 * MINUTE;
    update_header();
    assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "Updated 19:19"));
}

static void no_clock(void)
{
    /* Until the clock is known: no year, the package's years, and every screen draws. */
    standin_reset();
    standin_clock(0);
    standin_handlers.on_data = app_on_data;
    app_on_load();
    assert(cache.year == 0 && latest_year() == 2025);
    answer_file(POLL_UPCOMING, -1, "upcoming.json");
    app_on_focus();
    for (int32_t screen = 0; screen < SCREENS; screen++) {
        place.nav.screen = screen;
        draws();
    }
    place.nav.screen = 0;
    app_on_button(BTN_OK_PRESSED);
    draws();
}

static void precision_and_clock(void)
{
    standin_reset(); app_on_load();
    char text[64];
    launch_t launch = { .net = parse_moment("2026-12-01T00:00:00Z"), .precision = PRECISION_MONTH };
    /* Assert UTC calendar periods even in a zone west of UTC. The local formatter
     * intentionally cannot participate in coarse date formatting. */
    honolulu = true;
    say_day(&launch, HOST_TIME_DATE, text, sizeof text);
    assert(!strcmp(text, "Dec 2026"));
    launch.net = parse_moment("2026-01-01T00:00:00Z"); launch.precision = 14;
    say_day(&launch, HOST_TIME_DATE, text, sizeof text); assert(!strcmp(text, "2026"));
    launch.precision = 3;
    say_day(&launch, HOST_TIME_NEAR_DAY, text, sizeof text); assert(!strcmp(text, "1 Jan 2026"));
    launch.precision = PRECISION_UNKNOWN;
    say_day(&launch, HOST_TIME_NEAR_DAY, text, sizeof text); assert(!strcmp(text, "Time unknown"));
    seen_at = time_now(); launch.net = time_now() + HOUR;
    say_in(&launch, time_now(), text, sizeof text); assert(!text[0]);
    launch.precision = PRECISION_HOUR;
    say_day(&launch, HOST_TIME_CLOCK, text, sizeof text);
    assert(strlen(text) == 5 && !strcmp(text + 2, ":--"));
    say_day(&launch, HOST_TIME_NEAR_DAY, text, sizeof text);
    assert(strlen(text) == 5 && !strcmp(text + 2, ":--"));
    honolulu = false;
    standin_reset(); standin_clock(0); app_on_load(); assert(cache.year == 0);
    standin_clock(1798762200); app_on_intent(HOST_INTENT_TIME_CHANGED); assert(cache.year == 2027);
    standin_clock(1830297600); app_on_focus(); assert(cache.year == 2028);
    standin_clock(3786912000LL); /* 2090, supported by the SDK */
    app_on_intent(HOST_INTENT_TIME_CHANGED);
    place.nav.screen = SCREEN_HISTORY; place.chosen_year[1] = 2090;
    assert(year_shown(1) == FIRST_YEAR + HOST_CHART_VALUES_MAX - 1);
    draws();
    app_on_button(BTN_DOWN_PRESSED);
    assert(year_shown(1) == FIRST_YEAR + HOST_CHART_VALUES_MAX - 1);
    draws();
}

int main(void)
{
    navigation();
    words();
    header();
    no_clock();
    precision_and_clock();
    puts("space-launch: screens, the list's ends, the launch's page, years, sessions, words and no clock passed");
    return 0;
}
