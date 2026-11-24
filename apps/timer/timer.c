/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A countdown that ends wherever the user has gone: set the minutes,
 * start it, switch to the clock or the weather - when the time is up the
 * timer takes the screen back and says so.
 *
 * It is the sample of an application that works while it is not loaded.
 * Nothing counts down in here: the end is a moment in the store, Perinkpheral OS
 * is asked for one alarm at that moment, and whatever the screen shows is
 * worked out from the two whenever the application is drawn.
 */
#include <stdbool.h>

#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

#define MINUTE_TIMER       0
#define SECONDS_PER_MINUTE 60
#define MS_PER_SECOND      1000
#define MINUTES_DEFAULT    25
#define MINUTES_MIN        1
#define MINUTES_MAX        180
#define MINUTES_STEP_LONG  5

/* What survives being unloaded. `ends_at` is uptime in seconds: the wall
 * clock may not be known, and it may jump when it is set. */
#define STATE_KEY     "state"
#define STATE_VERSION 4
#define RETRY_TIMER 1
#define NOTICE_ALARM 2
#define NOTICE_LIFETIME 60
#define RETRY_SECONDS 1
typedef struct {
    int32_t version;
    int32_t minutes; /* what the user set */
    int32_t ends_at; /* 0: not running; else in seconds since the device started */
    uint32_t boot;   /* the run of the device `ends_at` is of */
    int32_t notice_until;
} timer_state_t;

static timer_state_t state, pending;
static bool save_pending, receipt_pending;
static void retry_save(void);
static void restore_running(void);
static void say_if_unset(int32_t result, const char *what);

static int32_t uptime_seconds(void)
{
    return (int32_t)(time_uptime_ms() / MS_PER_SECOND);
}

static bool save(void)
{
    if (!app_state_save(STATE_KEY, &state, sizeof state)) { return false; }
    receipt_pending = false;
    return true;
}

void app_on_load(void)
{
    bool is_stored = app_state_load(STATE_KEY, &state, sizeof state, STATE_VERSION);
    if (!is_stored) {
        /* Version 3 has the same prefix, without the notice deadline. */
        is_stored = kv_get(STATE_KEY, sizeof STATE_KEY - 1, &state, sizeof state) == 16 && state.version == 3;
        state.version = STATE_VERSION;
        state.notice_until = 0;
    }
    save_pending = receipt_pending = false;
    /* A countdown from before a restart is over: uptime began again. */
    bool is_stale = is_stored && state.ends_at != 0 && state.boot != time_boot_id();
    if (state.boot != time_boot_id()) { state.notice_until = 0; }
    if (!is_stored || is_stale) {
        state = (timer_state_t){ .version = STATE_VERSION, .minutes = MINUTES_DEFAULT };
    }
}

static int32_t seconds_left(void)
{
    int32_t left = state.ends_at != 0 ? state.ends_at - uptime_seconds() : 0;
    return left > 0 ? left : 0;
}

/* The picture shows whole minutes: it changes when one of them is over. */
static void schedule_minute_tick(void)
{
    int32_t left = seconds_left();
    if (left > 0) {
        int32_t into_minute = left % SECONDS_PER_MINUTE;
        timer_set(MINUTE_TIMER,
                  (into_minute > 0 ? into_minute : SECONDS_PER_MINUTE) * MS_PER_SECOND);
    }
}

void app_on_focus(void)
{
    retry_save();
    schedule_minute_tick();
}

void app_on_timer(int32_t id)
{
    if (id == RETRY_TIMER) { retry_save(); return; }
    if (id != MINUTE_TIMER) { return; }
    screen_redraw(HOST_REDRAW_NORMAL);
    schedule_minute_tick();
}

/* The application has a few alarms: the countdown's end, and one that
 * comes every minute to keep the minutes in the header right. */
#define COUNTDOWN_ALARM 0
#define MINUTE_ALARM    1

static int32_t minutes_left(void)
{
    /* The minute in hand counts: 61 seconds left reads 2, the last second 1. */
    return (seconds_left() + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE;
}

/* While it counts, the header says so wherever the user has gone: the
 * minutes left and the hourglass, on the right where the date was. */
static void show_in_header(void)
{
    /* "{0} min", the number where the language wants it. */
    char line[HOST_STATUS_TEXT_MAX + 1];
    const char *pattern = app_string(STR_IN_HEADER);
    const host_text_argument_t minutes = { .number = minutes_left() };
    int32_t length = text_fill(pattern, app_text_length(pattern), &minutes, 1, line, sizeof line - 1);
    line[length > 0 ? length : 0] = '\0';
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_HOURGLASS, line, app_text_length(line));
}

static void release_header_status(void)
{
    header_set_status(HOST_HEADER_RIGHT, HOST_ICON_NONE, "", 0);
    alarm_set(MINUTE_ALARM, 0);
}

static void post_completion(void)
{
    if (state.notice_until == 0 || uptime_seconds() > state.notice_until) { return; }
    /* Keep the durable deadline until the queue accepts the notice. A full
     * queue must remain retryable even if storage refuses every later write. */
    host_notice_t time_is_up = { .icon = HOST_ICON_BELL };
    app_text_copy(time_is_up.first, sizeof time_is_up.first, app_string(STR_TIME));
    app_text_copy(time_is_up.second, sizeof time_is_up.second, app_string(STR_IS_UP));
    if (notification_post(&time_is_up) == HOST_OK) {
        state.notice_until = 0;
        if (save_pending) { pending.notice_until = 0; }
        alarm_set(NOTICE_ALARM, 0);
        /* Receipt writes are best effort: posting and storage are separate
         * host operations. Retry in this instance without posting again. */
        if (!save()) {
            receipt_pending = true;
            timer_set(RETRY_TIMER, RETRY_SECONDS * MS_PER_SECOND);
        }
    } else {
        say_if_unset(alarm_set(NOTICE_ALARM, RETRY_SECONDS), "no room for the notice's alarm");
    }
}

/* Persist completion before attempting delivery. */
void app_on_alarm(int32_t id)
{
    if (id == NOTICE_ALARM) { post_completion(); return; }
    if (id == MINUTE_ALARM) {
        if (seconds_left() > 0) { show_in_header(); }
        return;
    }
    if (id != COUNTDOWN_ALARM || state.ends_at == 0 || seconds_left() > 0) { return; }
    timer_state_t before = state;
    state.notice_until = state.ends_at + NOTICE_LIFETIME;
    state.ends_at = 0;
    if (!save()) {
        state = before;
        alarm_set(COUNTDOWN_ALARM, RETRY_SECONDS);
        return;
    }
    alarm_set(COUNTDOWN_ALARM, 0);
    release_header_status();
    timer_set(MINUTE_TIMER, 0);
    post_completion();
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* An alarm the device has no room for: said in the log, or what depends on it is taken for a hang. */
static void say_if_unset(int32_t result, const char *what)
{
    if (result != HOST_OK) {
        log_write(HOST_LOG_WARN, what, app_text_length(what));
    }
}

/* A new version of the timer does not inherit the old one's alarms: a countdown under way
 * is set going again where it is. */
static void restore_running(void)
{
    if (state.ends_at == 0) {
        return;
    }
    int32_t left = seconds_left();
    say_if_unset(alarm_set(COUNTDOWN_ALARM, left > 0 ? left : 1), "no room for the countdown's alarm");
    int32_t first_minute = left % SECONDS_PER_MINUTE;
    say_if_unset(alarm_set_every(MINUTE_ALARM, SECONDS_PER_MINUTE,
                                first_minute > 0 ? first_minute : SECONDS_PER_MINUTE),
                 "no room for the minutes' alarm");
    show_in_header();
}

void app_on_updated(void)
{
    restore_running();
    if (state.notice_until && uptime_seconds() <= state.notice_until) {
        say_if_unset(alarm_set(NOTICE_ALARM, RETRY_SECONDS), "no room for the notice's alarm");
    }
}

/* Changes remain invisible until stored. Retry without acknowledging an unsaved edit. */
static void apply_state(void)
{
    if (state.ends_at) { restore_running(); schedule_minute_tick(); }
    else {
        alarm_set(COUNTDOWN_ALARM, 0);
        release_header_status();
        timer_set(MINUTE_TIMER, 0);
    }
    if (!state.notice_until) { alarm_set(NOTICE_ALARM, 0); }
}

static bool commit_change(timer_state_t before)
{
    /* Reserve the indispensable alarm before acknowledging a new countdown. */
    if (state.ends_at && !before.ends_at) {
        int32_t left = seconds_left();
        int32_t result = alarm_set(COUNTDOWN_ALARM, left > 0 ? left : 1);
        say_if_unset(result, "no room for the countdown's alarm");
        if (result != HOST_OK) { state = before; return false; }
    }
    if (!save()) {
        pending = state;
        state = before;
        save_pending = true;
        apply_state();
        timer_set(RETRY_TIMER, RETRY_SECONDS * MS_PER_SECOND);
        return false;
    }
    save_pending = false;
    timer_set(RETRY_TIMER, 0);
    apply_state();
    return true;
}
static void retry_save(void)
{
    if (save_pending) {
        timer_state_t before = state;
        state = pending;
        if (commit_change(before)) { screen_redraw(HOST_REDRAW_NORMAL); }
        else if (save_pending) { timer_set(RETRY_TIMER, RETRY_SECONDS * MS_PER_SECOND); }
    }
    if (receipt_pending && !save()) { timer_set(RETRY_TIMER, RETRY_SECONDS * MS_PER_SECOND); }
}
void app_on_terminate(void) { retry_save(); }

/* Removal keeps the countdown's store but drops its alarms. */
void app_on_installed(void)
{
    if (state.ends_at != 0 && seconds_left() == 0) {
        state.ends_at = 0;
        save();
    }
    app_on_updated();
}

static bool change_minutes(int32_t by)
{
    if (state.ends_at != 0) {
        return false; /* a running countdown is stopped first */
    }
    int32_t previous = state.minutes;
    state.minutes += by;
    if (state.minutes < MINUTES_MIN) {
        state.minutes = MINUTES_MIN;
    } else if (state.minutes > MINUTES_MAX) {
        state.minutes = MINUTES_MAX;
    }
    return state.minutes != previous;
}

void app_on_button(int32_t event)
{
    timer_state_t before = state;
    switch (event) {
    case BTN_UP_PRESSED:
        if (!change_minutes(1)) return;
        break;
    case BTN_DOWN_PRESSED:
        if (!change_minutes(-1)) return;
        break;
    case BTN_UP_LONG_PRESSED:
        if (!change_minutes(MINUTES_STEP_LONG)) return;
        break;
    case BTN_DOWN_LONG_PRESSED:
        if (!change_minutes(-MINUTES_STEP_LONG)) return;
        break;
    case BTN_OK_PRESSED:
        if (state.ends_at) { state.ends_at = 0; }
        else {
            state.ends_at = uptime_seconds() + state.minutes * SECONDS_PER_MINUTE;
            state.boot = time_boot_id();
        }
        state.notice_until = 0;
        break;
    default:
        return; /* the key going down and up changes nothing by itself */
    }
    commit_change(before);
    screen_redraw(HOST_REDRAW_NORMAL);
}

/* ---- the picture: Perinkpheral OS's parts ----
 *
 * The minutes are the page. Being set, they stand in the frame with the
 * arrows UP and DOWN have; counting, over a bar of how much is left. The
 * row of buttons along the bottom names what OK and BACK do. That the
 * time is up is Perinkpheral OS's to say (notification_post). */

#define FRAME_WIDTH   336
#define FRAME_PADDING 16
#define BAR_HEIGHT    14 /* Perinkpheral OS's bar */

void app_on_draw(void)
{
    bool    is_running = seconds_left() > 0;
    int32_t font = HOST_FONT_HUGE | HOST_FONT_TABULAR;
    int32_t capitals = gfx_font_cap_height(font);
    int32_t body = gfx_height() - HOST_UI_ACTIONS_HEIGHT;
    int32_t frame_height = capitals + 2 * FRAME_PADDING;
    int32_t frame_left = (gfx_width() - FRAME_WIDTH) / 2;
    int32_t frame_top = (body - frame_height) / 2;

    char       number[8];
    app_text_t text;
    app_text_start(&text, number, sizeof number);
    app_text_add_number(&text, is_running ? minutes_left() : state.minutes);
    const char *unit = app_string(is_running ? STR_MINUTES_LEFT : STR_MINUTES);
    int32_t length = app_text_length(number), unit_length = app_text_length(unit);
    ui_draw_value((gfx_width() - ui_value_width(font, number, length, unit, unit_length)) / 2,
             frame_top + FRAME_PADDING - (gfx_font_ascent(font) - capitals), font, number, length,
             unit, unit_length);

    if (is_running) {
        /* How much is left, halfway from the digits to the row of buttons. */
        int32_t digits_bottom = frame_top + FRAME_PADDING + capitals;
        ui_draw_bar(frame_left, (digits_bottom + body - BAR_HEIGHT) / 2, FRAME_WIDTH, minutes_left(),
               state.minutes);
        app_draw_actions(app_string(STR_STOP), app_string(STR_CLOSE));
    } else {
        ui_draw_frame(frame_left, frame_top, FRAME_WIDTH, frame_height,
                 (state.minutes < MINUTES_MAX ? HOST_UI_ARROW_UP : 0) |
                     (state.minutes > MINUTES_MIN ? HOST_UI_ARROW_DOWN : 0));
        app_draw_actions(app_string(STR_START), app_string(STR_CLOSE));
    }
}
