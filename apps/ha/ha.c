/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * A screen that is described in Home Assistant and drawn here. The
 * perINKpheral integration of Home Assistant (perinkpheral-hacs) sets it up:
 * it gives the application its server and a secret of its own (hidden
 * settings, written by the integration and not on the device's page) and
 * answers with the screen - a line a place, the parts of it divided by '|':
 *     hero|21°|Cloudy
 *     detail|54|%|drop
 *     column|Room|thermometer|22.5°
 * `hero` is the one thing the screen is for: a value of a few characters
 * and a large picture. `detail` - two of them - is a smaller value with
 * its unit and a small picture, beside the hero. `column` - up to four -
 * is a name, a picture and a value, in a row under the rule. A layout of
 * cells or a list is chosen in the integration too.
 * The application asks for the screen every so often - one request, plain
 * text back - and keeps the last answer in its store. It knows nothing of
 * what the places say: what is shown and how it is worked out is all Home
 * Assistant's.
 */
#include <stdbool.h>
#include <stddef.h>

#include "app_draw.h"
#include "app_nav.h"
#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/perinkpheral */

/* Defaults may be shown after a failed read, but must not replace durable state.
 * Only a later load that can read the store permits writes again. */
static bool restore_failed;

static int32_t read_store(const char *key, void *value, int32_t capacity)
{
    int32_t result = kv_get(key, app_text_length(key), value, capacity);
    if (result < 0 && result != HOST_ERR_NOT_FOUND) { restore_failed = true; }
    return result;
}

/* Service-wide policy: manifest and raw replies both pace raw actions.
 * Uptime survives instance replacement and cannot be waived by a wall-clock change. */
typedef struct { uint32_t boot; int64_t retry_at; } cooldown_t;
static cooldown_t cooldown, saved_cooldown;
static void restore_cooldown(void);

static void save_cooldown(void)
{
    if (!restore_failed &&
        (cooldown.boot != saved_cooldown.boot || cooldown.retry_at != saved_cooldown.retry_at) &&
        app_state_save("cooldown", &cooldown, sizeof cooldown)) { saved_cooldown = cooldown; }
}

static void load_cooldown(void)
{
    if (read_store("cooldown", &cooldown, sizeof cooldown) != sizeof cooldown ||
        cooldown.boot != time_boot_id()) { cooldown = (cooldown_t){ .boot = time_boot_id() }; }
    saved_cooldown = cooldown;
}

static void receive_cooldown(int32_t status)
{
    restore_cooldown();
    host_answer_info_t info = {0};
    answer_info(&info, sizeof info);
    int32_t delay = info.retry_after > 0 ? info.retry_after : status == 429 ? 60 : 0;
    if (delay > 0) {
        int64_t retry_at = time_uptime_ms() + (int64_t)delay * 1000;
        if (retry_at > cooldown.retry_at) { cooldown.retry_at = retry_at; }
    }
    save_cooldown();
}

/* The request tag follows the pairing even across an unload. */
static uint32_t pairing(void)
{
    static const char *const keys[] = { "server", "token" };
    uint32_t hash = 2166136261u;
    char value[256];
    for (int32_t key = 0; key < 2; key++) {
        int32_t length = read_store(keys[key], value, sizeof value - 1);
        value[length >= 0 ? length : 0] = '\0';
        for (int32_t i = 0; value[i] != '\0'; i++) { hash = (hash ^ (uint8_t)value[i]) * 16777619u; }
        hash = (hash ^ 0xFFu) * 16777619u;
    }
    return hash;
}

typedef struct { uint32_t pairing; int32_t tag, page; } request_source_t;
static request_source_t source;
static bool source_ready, focused;
static int32_t current_page = 1, page_count = 1, shown_page = 1;
static void restore_page(void);
static void read_page(void);
static bool valid_screen(const char *text, int32_t length);

/* Persist a changed tag before asking, so a late answer retains its old identity. */
static void follow_pairing(void)
{
    uint32_t current = pairing();
    char page = (char)('0' + current_page);
    char saved_page[2];
    int32_t length = read_store("page", saved_page, sizeof saved_page);
    if (restore_failed) { source_ready = false; return; }
    if ((length != 1 || saved_page[0] != page) &&
        kv_set("page", 4, &page, 1) != HOST_OK) { source_ready = false; return; }
    if (!source_ready || source.pairing != current || source.page != current_page) {
        request_source_t next = { .pairing = current, .page = current_page,
            .tag = source.tag >= HOST_REQUEST_FIRST && source.tag < HOST_REQUEST_LAST
                       ? source.tag + 1 : HOST_REQUEST_FIRST };
        source_ready = kv_set("source", 6, &next, sizeof next) == HOST_OK;
        if (source_ready) { source = next; }
    }
}

static int32_t request_screen(void) { return source.tag; }
#define REFRESH_ALARM 0

/* The screen as Home Assistant rendered it, kept as it came. */
#define SCREEN_KEY "screen"
/* Seven lines of the dashboard are some 350 bytes; a list has room for
 * about fifteen. What comes beyond it is cut. */
#define SCREEN_MAX 512
static char    screen[SCREEN_MAX + 1];
static int32_t screen_length;
#define SCREEN_VERSION 1
typedef struct {
    int32_t version;
    uint32_t pairing;
    char text[SCREEN_MAX];
} cached_screen_t;
static uint32_t screen_pairing;

/* The answer being taken, piece by piece: it becomes the screen only when
 * it has come whole. */
static char    coming[SCREEN_MAX + 1];
static int32_t coming_length;
static int32_t coming_status;
static bool receiving, screen_dirty, coming_poll;

static void save_screen(void)
{
    if (restore_failed) { screen_dirty = true; return; }
    cached_screen_t cache = { .version = SCREEN_VERSION, .pairing = screen_pairing };
    for (int32_t i = 0; i < screen_length; i++) { cache.text[i] = screen[i]; }
    screen_dirty = kv_set(SCREEN_KEY, sizeof SCREEN_KEY - 1, &cache,
                          offsetof(cached_screen_t, text) + screen_length) != HOST_OK;
}

/* Why there is nothing to show, when there is nothing: the status of the
 * last answer, an HTTP one or a HOST_FAILED_*. 0: nothing was asked yet. */
#define STATUS_KEY "status"
#define HTTP_OK    200
static int32_t last_status, saved_status;

static void save_status(void)
{
    if (!restore_failed && last_status != saved_status &&
        kv_set(STATUS_KEY, sizeof STATUS_KEY - 1, &last_status, sizeof last_status) == HOST_OK) {
        saved_status = last_status;
    }
}

/* The list layout: Perinkpheral OS's list of facts, moved with UP and DOWN. */
static host_list_t list = { .selected = -1 };

/* ---- asking ---- */

/* How often: the "every" setting, "5 min" as the manifest has it when
 * nothing is chosen. */
#define REFRESH_DEFAULT_S (5 * 60)

/* One request is enough for an instance that has just asked. */
static bool has_asked;
static int64_t last_ask_ms = -1;
/* The answer awaited was asked for by a press: it is shown at once, not at the data's pace. */
static bool asked_by_press;

/* One request: the screen as Home Assistant renders it. The server and the
 * token are Perinkpheral OS's to put into the request. */
static void ask(void)
{
    save_cooldown();
    if (time_uptime_ms() < cooldown.retry_at) {
        asked_by_press = false;
        current_page = shown_page;
        return;
    }
    if (!focused) { restore_page(); }
    last_ask_ms = time_uptime_ms();
    follow_pairing();
    if (!source_ready) {
        asked_by_press = false;
        has_asked = false;
        last_status = HOST_FAILED_NOT_READY;
        save_status();
        screen_redraw(HOST_REDRAW_NORMAL);
        return;
    }
    /* The screen is the integration's, which knows the device by the secret it gave it and
     * answers with the screen. */
    char url[] = "{{server}}/api/perinkpheral/screen?page=1";
    url[sizeof url - 2] = (char)('0' + current_page);
    static const char HEADERS[] = "Authorization: Bearer {{token}}";
    int32_t result = http_send(request_screen(), HOST_HTTP_GET, url, sizeof url - 1,
                               HEADERS, sizeof HEADERS - 1, "", 0);
    has_asked = result == HOST_OK;
    if (!has_asked) {
        asked_by_press = false;
        last_status = result == HOST_ERR_NO_SPACE ? HOST_FAILED_NO_MEMORY : HOST_FAILED_NOT_READY;
        save_status();
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}

/* Ordinary refreshes belong to the OS poller: reconnect recovery, replay of
 * interrupted GETs and the power-saving floor all apply there. Raw requests
 * are only for explicit page changes, whose tags protect against late replies. */
static void keep_fresh(void)
{
    alarm_set(REFRESH_ALARM, 0); /* Remove the periodic raw GET from older versions. */
}

/* ---- the answer ---- */

static void accept_answer(void)
{
    last_status = coming_status;
    save_status();
    /* Validate before touching the last good screen or its cache. */
    bool is_screen_payload = coming_status == HTTP_OK && valid_screen(coming, coming_length);
    if (!is_screen_payload) {
        current_page = shown_page; /* what was shown before stays, and so does its number */
        return;
    }
    /* A poll prepared before a page press can finish after it. Its page header
     * identifies that old screen; an integration removing pages may clamp down. */
    if (coming_poll && coming_length >= 9 && app_text_starts(coming, "page|") &&
        coming[5] != '0' + source.page && coming[7] >= '0' + source.page) { return; }
    bool changed = coming_length != screen_length;
    for (int32_t i = 0; i < coming_length; i++) {
        changed |= screen[i] != coming[i];
        screen[i] = coming[i];
    }
    screen_length = coming_length;
    screen[screen_length] = '\0';
    int32_t previous_page = shown_page;
    read_page();
    if (previous_page != shown_page) { list.first = list.count = 0; }
    current_page = shown_page;
    follow_pairing();
    if (changed || screen_dirty) {
        screen_pairing = pairing();
        save_screen();
    }
}

void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags,
                 int32_t http_status)
{
    /* A superseded page's refusal still paces the application's requests. */
    if (flags & (HOST_DATA_FIRST | HOST_DATA_FAILED)) { receive_cooldown(http_status); }
    /* The OS discards manifest-poll replies invalidated by a settings change. */
    if (request == 0 && (!source_ready || source.pairing != pairing())) { follow_pairing(); }
    if (!source_ready || source.pairing != pairing() || (request != 0 && request != request_screen())) {
        return;
    }
    if (flags & HOST_DATA_FAILED) {
        receiving = false;
        coming_status = http_status;
        coming_length = 0;
        accept_answer();
        screen_redraw(asked_by_press ? HOST_REDRAW_SOON : HOST_REDRAW_NORMAL);
        asked_by_press = false;
        return;
    }
    if (flags & HOST_DATA_FIRST) {
        coming_poll = request == 0;
        coming_status = http_status;
        coming_length = 0;
        receiving = true;
    }
    if (!receiving) { return; }
    for (int32_t i = 0; i < length && coming_length <= SCREEN_MAX; i++) {
        if (coming_length < SCREEN_MAX) { coming[coming_length] = (char)data[i]; }
        coming_length++;
    }
    if (flags & HOST_DATA_LAST) {
        receiving = false;
        accept_answer();
        screen_redraw(asked_by_press ? HOST_REDRAW_SOON : HOST_REDRAW_NORMAL);
        asked_by_press = false;
    }
}

/* ---- when to ask ---- */

void app_on_load(void)
{
    restore_failed = false;
    load_cooldown();
    focused = false;
    current_page = page_count = shown_page = 1;
    last_ask_ms = -1;
    source_ready = read_store("source", &source, sizeof source) == sizeof source &&
                   source.tag >= HOST_REQUEST_FIRST && source.tag <= HOST_REQUEST_LAST;
    if (source_ready) { current_page = source.page; }
    else { source = (request_source_t){0}; }
    cached_screen_t cache;
    int32_t length = read_store(SCREEN_KEY, &cache, sizeof cache);
    screen_pairing = pairing();
    screen_dirty = false;
    screen_length = length > (int32_t)offsetof(cached_screen_t, text) &&
                    cache.version == SCREEN_VERSION && cache.pairing == screen_pairing
                        ? length - (int32_t)offsetof(cached_screen_t, text) : 0;
    for (int32_t i = 0; i < screen_length; i++) { screen[i] = cache.text[i]; }
    if (!valid_screen(screen, screen_length)) { screen_length = 0; }
    screen[screen_length] = '\0';
    read_page();
    bool loaded_status = read_store(STATUS_KEY, &last_status, sizeof last_status) ==
                         sizeof last_status;
    saved_status = loaded_status ? last_status : 0;
    /* Read every state key before any migration or pairing invalidation writes. */
    follow_pairing(); /* Also initialise the scheduled page when upgrading an existing source. */
    bool other_pairing = length >= (int32_t)offsetof(cached_screen_t, text) &&
                         cache.pairing != screen_pairing;
    if (!loaded_status || other_pairing) {
        last_status = 0;
        /* Bind the empty cache too, so later failures belong to this pairing. */
        if (other_pairing) { save_screen(); }
        save_status();
    }
}

void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED) {
        keep_fresh(); /* a restart forgot the alarm */
    }
}

/* So did the version before this one, for this one. */
void app_on_updated(void)
{
    keep_fresh();
}

/* Opened, it shows what is true now and not what was true at the last
 * alarm. */
/* How far down the list the user was, kept while HA waits under another scene or is unloaded
 * for another application's answer. */
typedef struct {
    int32_t first, count, page, shown;
    int64_t last_ask_ms;
    cooldown_t cooldown;
} place_t;
APP_SESSION_FITS(place_t);

static void restore_cooldown(void)
{
    place_t place;
    if (app_session_load(&place, sizeof place) && place.cooldown.boot == cooldown.boot &&
        place.cooldown.retry_at > cooldown.retry_at) {
        cooldown = place.cooldown;
        save_cooldown();
    }
}

void app_on_terminate(void)
{
    save_cooldown();
    if (screen_dirty) { save_screen(); }
    save_status();
}

/* The screen to ask for: the one of the session, or, opened anew, the one the kept picture is of -
 * the screen the user left it on. Starting at the first while the second is drawn made it show
 * the second, then jump to the first (the owner, 2026-11-23: these are static gauges; the user
 * wanted that screen). */
static void restore_page(void)
{
    place_t place;
    current_page = app_session_load(&place, sizeof place) && place.page >= 1 && place.page <= 4
                       ? place.page : shown_page;
    if (current_page > page_count) { current_page = page_count; }
}

void app_on_blur(void)
{
    save_cooldown();
    place_t place = { .first = list.first, .count = list.count, .page = current_page, .shown = shown_page,
                      .last_ask_ms = last_ask_ms, .cooldown = cooldown };
    if (!app_session_save(&place, sizeof place)) {
        log_write(HOST_LOG_WARN, "HA session save failed", app_text_length("HA session save failed"));
    }
    focused = false;
}

void app_on_focus(void)
{
    restore_cooldown();
    restore_page();
    focused = true;
    place_t place;
    bool restored = app_session_load(&place, sizeof place);
    if (restored) {
        /* held to the list's end when it is drawn; the count is the last one drawn, for a press
         * that comes before the next drawing */
        list.first = place.shown == shown_page ? place.first : 0;
        list.count = place.shown == shown_page ? place.count : 0;
        if (last_ask_ms < place.last_ask_ms) { last_ask_ms = place.last_ask_ms; }
    }
    keep_fresh();
}

void app_on_settings(void)
{
    restore_cooldown();
    if (screen_pairing != pairing()) {
        screen_pairing = pairing();
        screen_length = 0;
        screen[0] = '\0';
        receiving = false;
        last_status = 0;
        save_screen();
        save_status();
        list.first = list.count = 0;
        current_page = page_count = shown_page = 1;
        screen_redraw(HOST_REDRAW_NORMAL);
    }
    follow_pairing();
    keep_fresh();
}

void app_on_alarm(int32_t id)
{
    (void)id;
    keep_fresh();
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        asked_by_press = true;
        ask(); /* "refresh" */
    } else if (page_count > 1 && list.count > 0 &&
               (event == BTN_UP_PRESSED || event == BTN_DOWN_PRESSED)) {
        /* Held UP/DOWN switch screens; short ones are the screen's, a row of its list. */
        if (app_list_button(&list, event) == APP_UI_CHANGED) {
            screen_redraw(HOST_REDRAW_NO_BLINK);
        }
    } else if (page_count > 1 &&
               (event == BTN_UP_PRESSED || event == BTN_DOWN_PRESSED ||
                event == BTN_UP_LONG_PRESSED || event == BTN_DOWN_LONG_PRESSED)) {
        app_nav_t nav = { .screens = page_count, .screen = current_page - 1 };
        app_nav_step(&nav, event == BTN_UP_PRESSED || event == BTN_UP_LONG_PRESSED ? -1 : 1);
        current_page = nav.screen + 1;
        receiving = false;
        asked_by_press = true;
        ask();
    } else if (page_count == 1 && event != BTN_BACK_PRESSED && app_list_button(&list, event) == APP_UI_CHANGED) {
        /* UP and DOWN a row, held a page; held to the list's end when it is drawn */
        screen_redraw(HOST_REDRAW_NO_BLINK);
    }
}

/* ---- the lines of the screen ---- */

#define PARTS_MAX 4
#define LINE_MAX  SCREEN_MAX

typedef struct {
    char        text[LINE_MAX + 1];
    const char *place; /* "hero", "detail", "column" */
    const char *parts[PARTS_MAX];
    int32_t count;
} line_t;

/* The next line of the screen, taken apart at the '|'. False at the end. */
static bool next_line_of(const char *text, int32_t size, int32_t *at, line_t *line)
{
    while (*at < size && (text[*at] == '\n' || text[*at] == '\r')) {
        (*at)++;
    }
    if (*at >= size) {
        return false;
    }
    int32_t length = 0, parts = 0;
    line->place = line->text;
    for (; *at < size && text[*at] != '\n'; (*at)++) {
        char character = text[*at];
        if (length >= LINE_MAX || character == '\r') {
            continue;
        }
        if (character == '|' && parts < PARTS_MAX) {
            line->text[length++] = '\0';
            line->parts[parts++] = line->text + length;
        } else {
            line->text[length++] = character;
        }
    }
    line->text[length] = '\0';
    line->count = parts;
    for (int32_t i = parts; i < PARTS_MAX; i++) {
        line->parts[i] = "";
    }
    return true;
}

static bool next_line(int32_t *at, line_t *line)
{
    return next_line_of(screen, screen_length, at, line);
}

/* Only a complete, bounded first line enables navigation. Unknown lines remain harmless. */
static void read_page(void)
{
    shown_page = page_count = 1;
    if (screen_length >= 9 && app_text_starts(screen, "page|") &&
        screen[5] >= '1' && screen[5] <= '4' && screen[6] == '|' &&
        screen[7] >= screen[5] && screen[7] <= '4' &&
        (screen[8] == '\n' || screen[8] == '\r')) {
        shown_page = screen[5] - '0';
        page_count = screen[7] - '0';
    }
}

typedef enum { LAYOUT_HERO, LAYOUT_TILES, LAYOUT_SINGLE, LAYOUT_LIST } layout_t;


/* ---- text ----
 *
 * A value may have a degree sign in it, as Home Assistant gives it:
 * Perinkpheral OS draws that one, and everything else beyond ASCII as "?". */

/* Where the degree sign is in a value, or -1. */
static int32_t degree_at(const char *text)
{
    for (int32_t i = 0; text[i] != '\0'; i++) {
        if (app_text_starts(text + i, APP_TEXT_DEGREE)) {
            return i;
        }
    }
    return -1;
}

/* ---- pictures, by the names a screen may use ---- */

/* A picture is one of Perinkpheral OS's icons, which draws each at any size from
 * its finest drawing: the weather in fine lines, the things of a home. */
typedef struct {
    const char *name;
    int32_t     icon;
} picture_t;

/* The states of a weather entity are names here, so that
 * "{{ states('weather.home') }}" is a picture as it stands. */
static const picture_t PICTURES[] = {
    { "sunny", HOST_ICON_SUN },
    { "partlycloudy", HOST_ICON_PARTLY_CLOUDY },
    { "cloudy", HOST_ICON_CLOUD },
    { "fog", HOST_ICON_CLOUD },
    { "windy", HOST_ICON_CLOUD },
    { "windy-variant", HOST_ICON_CLOUD },
    { "rainy", HOST_ICON_RAIN },
    { "pouring", HOST_ICON_RAIN },
    { "snowy", HOST_ICON_SNOW },
    { "snowy-rainy", HOST_ICON_SNOW },
    { "hail", HOST_ICON_SNOW },
    { "lightning", HOST_ICON_THUNDER },
    { "lightning-rainy", HOST_ICON_THUNDER },
    { "clear-night", HOST_ICON_MOON },
    { "umbrella", HOST_ICON_UMBRELLA },
    { "wind", HOST_ICON_WIND },
    { "drop", HOST_ICON_DROP },
    { "co2", HOST_ICON_CO2 },
    { "bolt", HOST_ICON_BOLT },
    { "gauge", HOST_ICON_GAUGE },
    { "thermometer", HOST_ICON_THERMOMETER },
    { "battery", HOST_ICON_BATTERY },
    { "bulb", HOST_ICON_BULB },
    { "door", HOST_ICON_DOOR },
    { "home", HOST_ICON_HOME },
    { "moon", HOST_ICON_MOON },
    { "plug", HOST_ICON_PLUG },
    { "chip", HOST_ICON_CHIP },
    { "network", HOST_ICON_NETWORK },
    { "bell", HOST_ICON_BELL },
    { "clock", HOST_ICON_CLOCK },
    { "check", HOST_ICON_CHECK },
    { "warning", HOST_ICON_WARNING },
    { "info", HOST_ICON_INFO },
    { "play", HOST_ICON_PLAY },
    { "sunrise", HOST_ICON_SUNRISE },
    { "sunset", HOST_ICON_SUNSET },
    /* Firmware 0.1.84: an older one draws nothing in their place. */
    { "layers", HOST_ICON_LAYERS },
    { "progress", HOST_ICON_PROGRESS },
    { "cube", HOST_ICON_CUBE },
};

static const picture_t *picture_named(const char *name)
{
    for (size_t i = 0; i < sizeof PICTURES / sizeof PICTURES[0]; i++) {
        if (app_text_equal(name, PICTURES[i].name)) {
            return &PICTURES[i];
        }
    }
    return NULL;
}

/* The three sizes a picture stands in: beside the hero, over a column,
 * beside a detail. */
#define HERO_PICTURE   88
#define COLUMN_PICTURE 44
#define DETAIL_PICTURE 22

static void draw_picture(const picture_t *picture, int32_t left, int32_t top, int32_t side)
{
    gfx_draw_icon(left, top, picture->icon, side / HOST_ICON_CELLS);
}

/* ---- the screen: the weather's own composition, its places filled ---- */

#define MARGIN       16
/* The hero stands a little inside the rule under it, on both sides. */
#define HERO_LEFT    (MARGIN + 6)
/* From the picture to the value. */
#define HERO_TEXT_GAP 22
/* Two characters are as large as there is; a longer value steps down one
 * size, or it would run into the details. */
#define HERO_CHARACTERS_MAX 2
/* The details: two lines in the middle of the hero's height, their
 * pictures in one column, a unit in the small font after the value. */
#define DETAIL_TOP      30
#define DETAIL_LINE     36
#define DETAIL_GAP      10
#define DETAILS_MAX     2
/* The rule stands as far under the hero as the hero stands under the
 * header's rule. */
#define RULE_TOP        (MARGIN + HERO_PICTURE + MARGIN)
/* A column: its name as a caption, its picture, its value. */
#define COLUMN_NAME_TOP    133
#define COLUMN_PICTURE_TOP 158
#define COLUMN_VALUE_TOP   214
#define COLUMNS_MAX        4

static void draw_hero(const line_t *line)
{
    const char *value = line->parts[0];
    const picture_t *picture = picture_named(line->parts[1]);
    int32_t text_left = HERO_LEFT;
    if (picture != NULL) {
        draw_picture(picture, HERO_LEFT, MARGIN, HERO_PICTURE);
        text_left += HERO_PICTURE + HERO_TEXT_GAP;
    }
    int32_t degree = degree_at(value);
    int32_t characters = degree >= 0 ? degree : app_text_length(value);
    int32_t font = (characters <= HERO_CHARACTERS_MAX ? HOST_FONT_DISPLAY : HOST_FONT_LARGE) |
                   HOST_FONT_TABULAR;
    /* The value stands in the middle of the picture's height. */
    int32_t cap_height = gfx_font_cap_height(font);
    int32_t cap_top = MARGIN + (HERO_PICTURE - cap_height) / 2;
    app_draw_text(text_left, cap_top - (gfx_font_ascent(font) - cap_height), font, value);
}

typedef struct {
    char value[24];
    char unit[12];
    const picture_t *picture;
} detail_line_t;

static int32_t detail_width(const detail_line_t *detail)
{
    return (detail->picture != NULL ? DETAIL_PICTURE + DETAIL_GAP : 0) +
           ui_value_width(HOST_FONT_TEXT, detail->value, app_text_length(detail->value),
                          detail->unit, app_text_length(detail->unit));
}

static void draw_detail(int32_t left, int32_t number, int32_t count, const detail_line_t *detail,
                        bool has_pictures)
{
    /* One alone stands where the middle of two would be. */
    int32_t top = DETAIL_TOP + number * DETAIL_LINE + (count == 1 ? DETAIL_LINE / 2 : 0);
    int32_t baseline = top + gfx_font_ascent(HOST_FONT_TEXT);
    if (detail->picture != NULL) {
        /* The picture stands on the text's baseline. */
        draw_picture(detail->picture, left, baseline - DETAIL_PICTURE, DETAIL_PICTURE);
    }
    /* The value and, after it, its unit: Perinkpheral OS's to set. */
    ui_draw_value(left + (has_pictures ? DETAIL_PICTURE + DETAIL_GAP : 0), top, HOST_FONT_TEXT,
             detail->value, app_text_length(detail->value), detail->unit,
             app_text_length(detail->unit));
}

static void draw_column(int32_t number, int32_t count, const line_t *line)
{
    int32_t width = gfx_width() / count;
    int32_t center = number * width + width / 2;
    const picture_t *picture = picture_named(line->parts[1]);
    app_draw_centered(center, COLUMN_NAME_TOP, HOST_FONT_SMALL, line->parts[0]);
    if (picture != NULL) {
        draw_picture(picture, center - COLUMN_PICTURE / 2, COLUMN_PICTURE_TOP, COLUMN_PICTURE);
    }
    app_draw_centered(center, COLUMN_VALUE_TOP, HOST_FONT_TEXT, line->parts[2]);
}

/* ---- the other layouts: items in cells, one large, or in a list ----
 *
 * "layout|tiles", "layout|single" or "layout|list" as the first line, and
 * then lines of one shape whatever the layout: "item|name|picture|value".
 * The description still says what is shown and never where: a layout is
 * chosen whole, and the items fall into its places in their order. */

#define ITEMS_MAX 12

typedef struct {
    char name[48]; /* as wide as Perinkpheral OS's cell takes: half as many letters of Cyrillic */
    char value[48];
    const picture_t *picture;
} item_t;

static int32_t read_items(item_t items[ITEMS_MAX])
{
    line_t  line;
    int32_t at = 0, count = 0;
    while (count < ITEMS_MAX && next_line(&at, &line)) {
        if (app_text_equal(line.place, "item")) {
            app_text_copy(items[count].name, sizeof items[count].name, line.parts[0]);
            items[count].picture = picture_named(line.parts[1]);
            app_text_copy(items[count].value, sizeof items[count].value, line.parts[2]);
            count++;
        }
    }
    return count;
}

/* An item as a Perinkpheral OS cell - its name, its picture and its value,
 * laid out and sized by the kit - and the icon in it, or HOST_ICON_NONE. */
static int32_t cell_of(host_cell_t *cell, int32_t left, int32_t top, int32_t width, int32_t height,
                       const item_t *item)
{
    *cell = (host_cell_t){ .x = left, .y = top, .width = width, .height = height };
    app_text_copy(cell->name, sizeof cell->name, item->name);
    app_text_copy(cell->value, sizeof cell->value, item->value);
    return item->picture == NULL ? HOST_ICON_NONE : item->picture->icon;
}

/* One item is the whole screen, two are halves side by side, three and
 * four are quarters, with a rule between neighbours. */
static void draw_cells_of(const item_t *items, int32_t count)
{
    int32_t columns = count > 1 ? 2 : 1, rows = count > 2 ? 2 : 1;
    int32_t width = gfx_width() / columns, height = (gfx_height()) / rows;
    int32_t shown = count < columns * rows ? count : columns * rows;
    host_cell_t cells[4];
    int32_t     icons[4];
    for (int32_t i = 0; i < shown; i++) {
        icons[i] = cell_of(&cells[i], i % columns * width, i / columns * height, width, height, &items[i]);
    }
    app_draw_cells(cells, 0, icons, shown); /* one size for all of them */
    if (columns > 1) {
        gfx_draw_rect(width, MARGIN, 1, gfx_height() - 2 * MARGIN, HOST_RECT_FILLED);
    }
    if (rows > 1) {
        gfx_draw_rect(MARGIN, height, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    }
}

/* A list: Perinkpheral OS's own, of facts nobody chooses from - a name at the
 * left edge, its value at the right. UP and DOWN move it when it is longer
 * than the screen. */
static void draw_list_of(const item_t *items, int32_t count)
{
    list.top = 0;
    list.count = count;
    int32_t first = app_list_first(&list), shown = ui_list_visible(&list);
    for (int32_t i = first; i < first + shown && i < count; i++) {
        ui_draw_list_row(&list, i, items[i].name, app_text_length(items[i].name), items[i].value,
                    app_text_length(items[i].value), HOST_ICON_NONE);
    }
}


static layout_t layout_of(const char *text, int32_t length)
{
    line_t  line;
    int32_t at = 0;
    while (next_line_of(text, length, &at, &line)) {
        if (app_text_equal(line.place, "layout")) {
            return app_text_equal(line.parts[0], "tiles")    ? LAYOUT_TILES
                   : app_text_equal(line.parts[0], "single") ? LAYOUT_SINGLE
                   : app_text_equal(line.parts[0], "list")   ? LAYOUT_LIST
                                                 : LAYOUT_HERO;
        }
    }
    return LAYOUT_HERO;
}

static layout_t layout_asked(void) { return layout_of(screen, screen_length); }

/* Unknown extension lines may be ignored, but the selected layout must have
 * complete content it can draw. A layout header or an item in a hero screen
 * alone used to replace the cache with an empty application area. */
static bool valid_screen(const char *text, int32_t length)
{
    if (length <= 0 || length > SCREEN_MAX) { return false; }
    for (int32_t i = 0; i < length; i++) {
        if ((uint8_t)text[i] < ' ' && text[i] != '\n' && text[i] != '\r') { return false; }
    }
    layout_t layout = layout_of(text, length);
    bool hero_layout = layout == LAYOUT_HERO, has_content = false;
    int32_t items = 0, details = 0, columns = 0;
    line_t line;
    int32_t at = 0;
    while (next_line_of(text, length, &at, &line)) {
        bool hero = app_text_equal(line.place, "hero");
        bool detail = app_text_equal(line.place, "detail");
        bool column = app_text_equal(line.place, "column");
        bool item = app_text_equal(line.place, "item");
        if (hero || detail || column || item) {
            if (line.count < (hero ? 2 : 3)) { return false; }
            int32_t item_limit = layout == LAYOUT_SINGLE ? 1 : layout == LAYOUT_TILES ? 4 : ITEMS_MAX;
            bool drawn = hero_layout
                             ? hero || (detail && details++ < DETAILS_MAX) ||
                                   (column && columns++ < COLUMNS_MAX)
                             : item && items++ < item_limit;
            if (drawn) {
                has_content |= line.parts[0][0] != '\0' ||
                    (detail ? line.parts[1][0] != '\0' || picture_named(line.parts[2]) != NULL
                            : (!hero && line.parts[2][0] != '\0') ||
                              (layout != LAYOUT_LIST && picture_named(line.parts[1]) != NULL));
            }
        }
    }
    return has_content;
}

static void draw_hero_layout(void)
{
    line_t line;
    detail_line_t details[DETAILS_MAX];
    int32_t at = 0, detail_count = 0, column_count = 0;
    bool    has_detail_pictures = false;
    /* What there is of each place, before any of it is drawn: the details
     * are one block, and the columns share the width. */
    while (next_line(&at, &line)) {
        if (app_text_equal(line.place, "detail") && detail_count < DETAILS_MAX) {
            detail_line_t *detail = &details[detail_count++];
            app_text_copy(detail->value, sizeof detail->value, line.parts[0]);
            app_text_copy(detail->unit, sizeof detail->unit, line.parts[1]);
            detail->picture = picture_named(line.parts[2]);
            has_detail_pictures = has_detail_pictures || detail->picture != NULL;
        } else if (app_text_equal(line.place, "column") && column_count < COLUMNS_MAX) {
            column_count++;
        }
    }
    /* The details are as far from the right end of the rule as the hero
     * is from its left end; their pictures are one column. */
    int32_t widest = 0;
    for (int32_t i = 0; i < detail_count; i++) {
        int32_t width = detail_width(&details[i]) +
                        (has_detail_pictures && details[i].picture == NULL
                             ? DETAIL_PICTURE + DETAIL_GAP
                             : 0);
        widest = width > widest ? width : widest;
    }
    for (int32_t i = 0; i < detail_count; i++) {
        draw_detail(gfx_width() - HERO_LEFT - widest, i, detail_count, &details[i],
                    has_detail_pictures);
    }

    gfx_draw_rect(MARGIN, RULE_TOP, gfx_width() - 2 * MARGIN, 1, HOST_RECT_FILLED);
    int32_t column = 0;
    at = 0;
    while (next_line(&at, &line)) {
        if (app_text_equal(line.place, "hero")) {
            draw_hero(&line);
        } else if (app_text_equal(line.place, "column") && column < column_count) {
            draw_column(column++, column_count, &line);
        }
    }
}

/* Why there is no screen: as every application says it, and for an
 * answer that came with no screen in it, this one's own word. */
static void draw_reason(void)
{
    if (ui_draw_reason(last_status) == 0) {
        host_notice_t no_screen = { .icon = HOST_ICON_WARNING };
        app_text_copy(no_screen.first, sizeof no_screen.first, app_string(STR_NO_SCREEN));
        app_text_copy(no_screen.detail, sizeof no_screen.detail, app_string(STR_SET_UP_IN_HA));
        ui_draw_notice(&no_screen);
    }
}

void app_on_draw(void)
{
    if (restore_failed || screen_dirty || last_status != saved_status) {
        host_notice_t notice = { .icon = HOST_ICON_WARNING };
        app_text_copy(notice.first, sizeof notice.first, app_string(STR_STORAGE_ERROR));
        app_text_copy(notice.second, sizeof notice.second, app_string(STR_STORAGE_ERROR_SECOND));
        ui_draw_notice(&notice);
        return;
    }
    if (screen_length > 0) {
        static item_t items[ITEMS_MAX]; /* too large for the stack */
        layout_t      layout = layout_asked();
        int32_t       count = layout == LAYOUT_HERO ? 0 : read_items(items);
        if (layout != LAYOUT_LIST) { list.count = 0; }
        if (layout == LAYOUT_HERO) {
            draw_hero_layout();
        } else if (layout == LAYOUT_LIST) {
            draw_list_of(items, count);
        } else {
            draw_cells_of(items, layout == LAYOUT_SINGLE && count > 1 ? 1 : count);
        }
    } else {
        draw_reason();
    }
}
