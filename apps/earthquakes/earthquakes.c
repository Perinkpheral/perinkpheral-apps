/* SPDX-License-Identifier: MIT
 * USGS reports after an earthquake. Relevance radii are editorial filters,
 * never estimates of shaking, damage, or safety.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "host_api.h"
#include "app_days.h"
#include "app_draw.h"
#include "app_nav.h"
#include "app_state.h"
#include "app_text.h"
#include "app_ui.h"
#include "app_values.h"
#include "strings.h"

/* Share the SDK's small adapters rather than duplicating them in handlers. */
static bool equal(const char *a, const char *b) { return app_text_equal(a, b); }
static int32_t length(const char *s) { return app_text_length(s); }
#define app_text_equal equal
#define app_text_length length

/* Two top-level screens and one event view use app_nav state. */
static void nav_title(const app_nav_t *nav)
{
    const char *count = nav->screen ? "2/2" : "1/2";
    int32_t width = ui_text_width(HOST_UI_CAPTION, count, 3);
    ui_draw_text(384 - width, 16, HOST_UI_CAPTION, count, 3, width);
}
#define BATCH 8
#define LEDGER 32
#define HISTORY_DAYS 120
#define COOLDOWN 21600
#define FRESH 1800
#define UNKNOWN INT32_MIN
#define Q30 1073741824LL

enum { V_TYPE, V_ID, V_MAG, V_PLACE, V_TIME,
       V_UPDATED, V_EVENTTYPE, V_IDS, V_LON, V_LAT, V_DEPTH, V_COUNT };
typedef struct {
    char id[32], aliases[64], place[64];
    int32_t lat, lon, mag, depth;
    int64_t time, updated;
} event_t;
typedef struct {
    int64_t until;
    int32_t lat, lon, mag, escalated;
} ledger_t;
typedef struct {
    uint32_t boot;
    int32_t seed, failures[2], status;
    int64_t success[2], not_before, pause_until, observed, cache_seconds;
    int32_t pending; /* 1 retryable, 2 persisted delivery intent (never replay) */
    event_t notice;
} state_t;
typedef struct {
    uint32_t version, checksum;
    state_t control;
    int32_t count[2], browse_page;
    event_t events[2][BATCH]; /* nearby and the current Worldwide page */
    ledger_t ledger[LEDGER];
} cache_t;
typedef struct {
    app_nav_t nav;
    app_list_t list;
    int32_t scope, page;
    event_t detail; /* retains a report if it leaves the bounded pages */
} scene_t;
static cache_t cache;
#define state (cache.control)
static scene_t scene;
static bool placed, notices_on, requesting;
static int32_t requested_page;
static int32_t latitude, longitude, radius_km, minimum, refresh;
static uint32_t saved_checksum;
static int64_t success[2];

_Static_assert(sizeof(cache_t) <= 8192, "cache must occupy two blocks");
/* One checksummed snapshot holds data and notification intent. Seven settings
 * at their full byte limits plus the two query keys fit even if invalid
 * numeric settings are supplied outside the linked Place picker. */
_Static_assert(1344 + 2 * 96 <= HOST_KV_TOTAL_MAX, "KV budget");
APP_SESSION_FITS(scene_t);

__attribute__((noinline)) static const char *word(int32_t id) { return app_string(id); }
static void zero(void *out, int32_t size)
{
    uint8_t *p = out;
    while (size-- > 0) { *p++ = 0; }
}
__attribute__((noinline)) static void copy_bytes(void *out, const void *in, int32_t size)
{
    uint8_t *a = out;
    const uint8_t *b = in;
    while (size-- > 0) { *a++ = *b++; }
}
static uint32_t hash_bytes(const void *data, int32_t size)
{
    const uint8_t *p = data;
    uint32_t h = 2166136261u;
    while (size-- > 0) { h = (h ^ *p++) * 16777619u; }
    return h;
}
static uint32_t checksum(const cache_t *data)
{
    /* Version is checked separately. Hash the entire payload without mutating
     * the stored checksum while inspecting or comparing the snapshot. */
    return hash_bytes(&data->control, sizeof *data - offsetof(cache_t, control));
}
/* A failed or interrupted overwrite can discard cached data. The checksum
 * rejects it as a whole; a lost cache always bootstraps notices silently.
 * Delivery intent is written before posting. There is no fallback snapshot
 * that could replay an older notification ledger after an interrupted write. */
static bool save_cache(void)
{
    uint32_t sum = checksum(&cache);
    if (sum == saved_checksum) { return true; }
    cache.checksum = sum;
    if (file_write("cache", 5, 0, &cache, sizeof cache) != HOST_OK) { return false; }
    saved_checksum = sum;
    return true;
}
static void load_cache(void)
{
    if (file_read("cache", 5, 0, &cache, sizeof cache) != sizeof cache ||
        cache.version != 1 || cache.checksum != checksum(&cache) ||
        cache.count[0] < 0 || cache.count[0] > BATCH ||
        cache.count[1] < 0 || cache.count[1] > BATCH) {
        zero(&cache, sizeof cache);
        cache.version = 1;
    }
    saved_checksum = cache.checksum;
}

/* Sine in Q30, argument in microdegrees. The reduced Taylor series has
 * metre-scale error at the relevance boundaries. No floating point imports. */
static int64_t sine(int32_t degrees)
{
    /* Every caller supplies -180..180 degrees. */
    bool negative = degrees < 0;
    if (negative) { degrees = -degrees; }
    if (degrees > 90000000) { degrees = 180000000 - degrees; }
    int64_t x = (int64_t)degrees * 3373259426LL / 180000000;
    int64_t x2 = x * x / Q30, t = Q30;
    for (int32_t k = 7; k > 0; k--) { t = Q30 - x2 * t / Q30 / (2 * k * (2 * k + 1)); }
    return (negative ? -x : x) * t / Q30;
}
static int32_t distance_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2)
{
    int64_t a = sine((lat1 - lat2) / 2);
    int64_t b = sine((lon1 - lon2) / 2);
    int64_t h = a * a / Q30 + (sine(90000000 - lat1) *
                 sine(90000000 - lat2) / Q30) * (b * b / Q30) / Q30;
    if (h < 0) { h = 0; }
    if (h > Q30) { h = Q30; }
    int32_t low = 0, high = 180000000;
    for (int32_t i = 0; i < 28; i++) {
        int32_t mid = low + (high - low) / 2;
        int64_t s = sine(mid / 2);
        if (s * s / Q30 < h) { low = mid + 1; } else { high = mid; }
    }
    return (int32_t)((int64_t)high * 20015114 / 180000000);
}
__attribute__((noinline)) static int32_t from_place(const event_t *e)
{ return distance_m(latitude, longitude, e->lat, e->lon); }
__attribute__((noinline)) static bool cluster_near(const event_t *e, const ledger_t *l)
{ return distance_m(e->lat, e->lon, l->lat, l->lon) <= 100000; }
static int32_t relevance(int32_t mag)
{
    return mag >= 70 ? 2000 : mag >= 60 ? 1000 : mag >= 50 ? 300 : mag >= 40 ? 100 : 0;
}
static bool relevant(const event_t *e)
{
    int32_t r = relevance(e->mag);
    if (r > radius_km) { r = radius_km; }
    return placed && e->mag >= minimum && r > 0 &&
        from_place(e) <= r * 1000;
}
static bool alias_span(const char *aliases, const char *id, int32_t size)
{
    for (const char *p = aliases; *p; p++) {
        if (*p != ',') { continue; }
        int32_t n = 0;
        while (n < size && p[n + 1] == id[n]) { n++; }
        if (n == size && p[n + 1] == ',') { return true; }
    }
    return false;
}
static bool token(const char *aliases, const char *id)
{ return id[0] && alias_span(aliases, id, app_text_length(id)); }
static bool same_event(const event_t *a, const event_t *b)
{
    if (app_text_equal(a->id, b->id) || token(a->aliases, b->id) || token(b->aliases, a->id)) { return true; }
    for (const char *p = a->aliases; *p;) {
        if (*p == ',') { p++; continue; }
        const char *start = p;
        while (*p && *p != ',') { p++; }
        if (alias_span(b->aliases, start, (int32_t)(p - start))) { return true; }
    }
    return false;
}
static void fill(char *, int32_t, const char *, int32_t, const char *, const char *);
static void copy_text(char *out, int32_t size, const char *text)
{ fill(out, size, "{1}", 0, text, NULL); }
static bool setting_number(const char *, int32_t, int32_t *);
static void coordinate_text(char out[16], int32_t value)
{
    if (value < 0) { *out++ = '-'; value = -value; }
    int32_t divisor = 100000000;
    while (divisor > 1000000 && value < divisor) { divisor /= 10; }
    for (; divisor; divisor /= 10) {
        if (divisor == 100000) { *out++ = '.'; }
        *out++ = '0' + value / divisor % 10;
    }
    *out = 0;
}
static void settings(void)
{
    char place[256], text[64];
    latitude = longitude = 0;
    app_setting("place", place, sizeof place);
    for (int32_t i = 0; place[i]; i++) { if (place[i] == ',') { place[i] = 0; break; } }
    placed = place[0] &&
        setting_number("lat", 6, &latitude) && setting_number("lon", 6, &longitude) &&
        latitude >= -90000000 && latitude <= 90000000 && longitude >= -180000000 && longitude <= 180000000;
    if (placed) {
        const char *keys[] = { "lat", "lon" };
        int32_t values[] = { latitude, longitude };
        char raw[256];
        for (int32_t i = 0; i < 2; i++) if (app_setting(keys[i], raw, sizeof raw) > 16) {
            coordinate_text(raw, values[i]); kv_set(keys[i], 3, raw, app_text_length(raw));
        }
    }
    radius_km = 1000; minimum = 30;
    setting_number("radius", 0, &radius_km); setting_number("minmag", 1, &minimum);
    app_setting("every", text, sizeof text);
    refresh = text[0] == '5' ? 300 : text[0] == '1' && text[1] == '5' ? 900 : 60;
    app_setting("notify", text, sizeof text); notices_on = app_text_equal(text, "On");
    copy_text(text, HOST_HEADER_TEXT_MAX + 1, place);
    header_set_text(HOST_HEADER_LEFT, placed ? text : " ", placed ? app_text_length(text) : 1);
}
static bool stale(int32_t scope)
{
    int64_t now = time_now();
    int32_t interval = scope == 0 ? refresh : 900;
    int32_t limit = interval * 3 > 900 ? interval * 3 : 900;
    return !success[scope] || (uint64_t)(now - success[scope]) > (uint32_t)limit;
}
static void cancel_browse(void)
{
    requesting = 0; kv_set("offset", 6, NULL, 0);
}
static void reset_configuration(void)
{
    state.seed = 1; state.pending = 0; state.observed = 0; scene.page = 0; cancel_browse();
    cache.count[0] = 0;
    zero(cache.ledger, sizeof cache.ledger);
    success[0] = state.success[0] = 0;
}

static void provider_pause(void)
{
    int64_t remaining = state.pause_until - time_now();
    if (remaining > 0) {
        cancel_browse();
        kv_set("near", 4, NULL, 0);
        alarm_set(0, remaining > 86400 ? 86400 : (int32_t)remaining);
    } else {
        state.pause_until = 0;
        kv_set("near", 4, placed ? "earthquake" : NULL, placed ? 10 : 0);
        alarm_set(0, 0);
    }
}
/* Picked arrays are accepted as a single transaction. An identity at the
 * extraction limit is ambiguous and is rejected, including complete-looking
 * truncated alias lists. A missing or null magnitude remains unknown. */
static bool element(int32_t path, int32_t row, char *out, int32_t size)
{
    return app_value_element_text(path, row, out, size);
}
/* USGS fixed decimal fields: refuse unsupported/oversized numbers rather
 * than guessing. Six coordinate decimals and integer milliseconds suffice. */
static bool decimal(const char *text, int32_t places, int64_t *out)
{
    bool negative = *text == '-';
    if (negative) { text++; }
    int64_t value = 0; int32_t digits = 0;
    while (*text >= '0' && *text <= '9') {
        if (value > 5000000000000LL) { return false; }
        value = value * 10 + *text++ - '0'; digits++;
    }
    if (!digits) { return false; }
    int32_t round = 0;
    if (*text == '.') {
        text++;
        while (*text >= '0' && *text <= '9') {
            if (places > 0) { if (value > 5000000000000LL) { return false; } value = value * 10 + *text - '0'; places--; }
            else if (!round) { round = *text >= '5' ? 2 : 1; }
            text++;
        }
    }
    while (places-- > 0) { if (value > 5000000000000LL) { return false; } value *= 10; }
    if (*text || value > 5000000000000LL) { return false; }
    value += round == 2;
    *out = negative ? -value : value;
    return true;
}
static bool setting_number(const char *key, int32_t places, int32_t *out)
{
    char text[256]; int64_t value;
    if (app_setting(key, text, sizeof text) <= 0 || !decimal(text, places, &value) || value < -INT32_MAX || value > INT32_MAX) { return false; }
    *out = value; return true;
}
/* The picker already commits complete bodies. Keep equally sized arrays and
 * validate every field before replacing a page. Times stay in 64 bits. */
static bool extract(event_t *events, int32_t *count)
{
    char text[64];
    if (!element(V_TYPE, 0, text, sizeof text) || !app_text_equal(text, "FeatureCollection")) { return false; }
    int32_t n = answer_count(V_ID);
    if ((uint32_t)n > BATCH) { return false; }
    for (int32_t k = V_ID; k < V_COUNT; k++) { if (answer_count(k) != n) { return false; } }
    static const uint8_t paths[] = { V_LAT, V_LON, V_MAG, V_DEPTH, V_TIME, V_UPDATED };
    static const uint8_t decimals[] = { 6, 6, 1, 0, 0, 0 };
    static const int64_t low[] = { -90000000, -180000000, 30, -20, HOST_TIME_MIN * 1000, HOST_TIME_MIN * 1000 };
    static const int64_t high[] = { 90000000, 180000000, 100, 1000, HOST_TIME_MAX * 1000, HOST_TIME_MAX * 1000 };
    for (int32_t i = 0; i < n; i++) {
        event_t *e = &events[i];
        static const struct { uint16_t offset; uint8_t path, size; } strings[] = {
            { offsetof(event_t, id), V_ID, 32 }, { offsetof(event_t, aliases), V_IDS, 64 },
            { offsetof(event_t, place), V_PLACE, 64 }
        };
        for (int32_t k = 0; k < 3; k++) {
            char *value = (char *)e + strings[k].offset;
            if (!element(strings[k].path, i, value, strings[k].size)) { return false; }
            if (app_text_equal(value, "null")) { copy_text(value, strings[k].size, k == 2 ? "--" : ""); }
        }
        if (!e->id[0] || app_text_length(e->id) >= 31 || app_text_length(e->aliases) >= 63) { return false; }
        int64_t values[6];
        for (int32_t k = 0; k < 6; k++) {
            if (!element(paths[k], i, text, sizeof text)) { return false; }
            if ((k == 2 || k == 3) && app_text_equal(text, "null")) { values[k] = UNKNOWN; }
            else if (!decimal(text, decimals[k], &values[k]) || values[k] < low[k] || values[k] > high[k]) { return false; }
        }
        e->lat = values[0]; e->lon = values[1]; e->mag = values[2]; e->depth = values[3]; e->time = values[4]; e->updated = values[5];
        if (e->time < (time_now() / 86400 - HISTORY_DAYS) * 86400000) {
            zero(e, (BATCH - i) * sizeof *e); *count = i; return true;
        }
        if (e->updated < e->time ||
            !element(V_EVENTTYPE, i, text, sizeof text) || !app_text_equal(text, "earthquake")) { return false; }
        int32_t len = app_text_length(e->aliases);
        if (len && (e->aliases[0] != ',' || e->aliases[len - 1] != ',')) { return false; }
        /* FDSN returns unique events. An ambiguous collection is not a
         * basis for replacing good cached data or generating notices. */
        for (int32_t j = 0; j < i; j++) { if (same_event(e, &events[j])) { return false; } }
    }
    *count = n;
    return true;
}

/* Formatting is performed by Perinkpheral OS, keeping layout code small. */
__attribute__((noinline)) static void fill(char *out, int32_t size, const char *pattern,
                                        int32_t number, const char *a, const char *b)
{
    host_text_argument_t args[3] = {
        { number, NULL, 0 }, { 0, a, a ? app_text_length(a) : 0 },
        { 0, b, b ? app_text_length(b) : 0 }
    };
    int32_t n = text_fill(pattern, app_text_length(pattern), args, 3, out, size - 1);
    out[n > 0 ? n : 0] = 0;
}
#define FILL(out, pattern, n, a, b) fill(out, sizeof(out), pattern, n, a, b)
static void magnitude(char out[8], const event_t *e)
{
    if (e->mag == UNKNOWN) { copy_text(out, 8, "M--"); return; }
    /* Both queries request M3 or greater; null is handled above. */
    int32_t m = e->mag;
    *out++ = 'M';
    if (m >= 100) { *out++ = '1'; }
    *out++ = '0' + m / 10 % 10; *out++ = *word(STR_DECIMAL);
    *out++ = '0' + m % 10; *out = 0;
}
static void quantity(char out[32], int32_t value)
{
    fill(out, 32, value == UNKNOWN ? "-- {1}" : "{0} {1}", value, word(STR_KM), NULL);
}
static void summary(char out[64], const event_t *e)
{
    char mag[16], distance[32];
    magnitude(mag, e);
    quantity(distance, placed ? (from_place(e) + 500) / 1000 : UNKNOWN);
    fill(out, 64, "{1}, {2}", 0, mag, distance);
}
static void revise_notice(const event_t *e)
{
    if (state.pending == 1 && e->updated > state.notice.updated && same_event(e, &state.notice)) {
        copy_bytes(&state.notice, e, sizeof *e);
    }
}
static void when_text(char out[32], int64_t at);
static void try_notice(void)
{
    if (state.pending != 1) { return; }
    if ((uint64_t)(time_now() - state.notice.time / 1000) > FRESH || !notices_on || !relevant(&state.notice)) {
        state.pending = 0; save_cache(); return;
    }
    state.pending = 2; /* commit intent before posting: a crash may lose a notice */

    if (!save_cache()) { state.pending = 1; return; }
    host_notice_t notice; zero(&notice, sizeof notice); notice.icon = HOST_ICON_WARNING;
    copy_text(notice.first, sizeof notice.first, word(STR_EARTHQUAKE));
    summary(notice.second, &state.notice);
    char when[48];
    when_text(when, state.notice.time / 1000);
    fill(notice.detail, sizeof notice.detail, "U.S. Geological Survey  {1}", 0, when, NULL);
    state.pending = notification_post(&notice) == HOST_OK ? 0 : 1;

    save_cache();
}
/* A timestamp watermark is deliberately conservative: a late report older
 * than an already observed event is visible but never interrupts. It also
 * keeps revisions silent without a second identity/observation database. */
static void observe(const event_t *e, bool seed)
{
    int64_t now = time_now();
    if (seed || e->time <= state.observed ||
        (uint64_t)(now - e->time / 1000) > FRESH || !relevant(e) ||
        !notices_on || (state.pending && e->mag <= state.notice.mag)) { return; }
    int32_t free = -1, cluster = -1;
    bool escalated = false;
    for (int32_t i = 0; i < LEDGER; i++) {
        ledger_t *l = &cache.ledger[i];
        if (l->until <= now) { free = i; continue; }
        if (cluster_near(e, l)) {
            escalated |= l->escalated != 0;
            if (cluster < 0 || cache.ledger[cluster].mag < l->mag) { cluster = i; }
        }
    }
    if (free < 0 || (cluster >= 0 && (escalated || e->mag < cache.ledger[cluster].mag + 10))) { return; }
    ledger_t *l = &cache.ledger[free];
    l->until = cluster >= 0 ? cache.ledger[cluster].until : now + COOLDOWN;
    l->escalated = cluster >= 0;
    l->lat = e->lat; l->lon = e->lon; l->mag = e->mag;
    if (cluster >= 0) for (int32_t i = 0; i < LEDGER; i++) {
        if (cache.ledger[i].until > now && cluster_near(e, &cache.ledger[i])) { cache.ledger[i].escalated = 1; }
    }
    copy_bytes(&state.notice, e, sizeof *e); state.pending = 1;
}

/* Retry-After supports seconds and IMF-fixdate. Cache-Control max-age
 * controls cadence; no cache-busting or user refresh bypasses it. */
static int32_t digits(const char *s, int32_t n)
{
    int32_t value = 0;
    while (n-- > 0) { if (*s < '0' || *s > '9') { return -1; } value = value * 10 + *s++ - '0'; }
    return value;
}
static int64_t retry_date(const char *s)
{
    if (app_text_length(s) != 29) { return 0; }
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int32_t month = 0;
    for (int32_t i = 0; i < 12; i++) {
        if (s[8] == months[i * 3] && s[9] == months[i * 3 + 1] && s[10] == months[i * 3 + 2]) { month = i + 1; }
    }
    int32_t year = digits(s + 12, 4), day = digits(s + 5, 2);
    int32_t hour = digits(s + 17, 2), minute = digits(s + 20, 2), second = digits(s + 23, 2);
    if ((hour | minute | second) < 0 || hour > 23 || minute > 59 || second > 59) { return 0; }
    if (!month || (uint32_t)(day - 1) > 30) { return 0; }
    return (int64_t)app_days_from_date(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}
static int64_t header_seconds(const char *name, const char *prefix)
{
    char text[128];
    int32_t n = answer_header(name, app_text_length(name), 0, text, sizeof text - 1);
    if (n < 0) { return 0; }
    text[n] = 0;
    if (!prefix[0]) {
        int64_t seconds;
        return decimal(text, 0, &seconds) ? seconds : retry_date(text) - time_now();
    }
    for (int32_t i = 0; i < n; i++) { if (text[i] >= 'A' && text[i] <= 'Z') { text[i] += 'a' - 'A'; } }
    for (int32_t i = 0; i < n; i++) {
        if (!app_text_starts(text + i, prefix)) { continue; }
        char *at = text + i + app_text_length(prefix);
        while (*at == ' ' || *at == '"') { at++; }
        char *end = at;
        while (*end >= '0' && *end <= '9') { end++; }
        *end = 0;
        int64_t seconds;
        return decimal(at, 0, &seconds) ? seconds : 0;
    }
    return 0;
}
static void pace(int32_t request, bool ok)
{
    int64_t now = time_now();
    if (ok) { state.failures[request] = 0; }
    else if ((now >= state.not_before || !state.failures[request]) && state.failures[request] < 3) { state.failures[request]++; }
    int32_t delay = ok ? 0 : state.failures[request] == 1 ? 300 : state.failures[request] == 2 ? 900 : 3600;
    int64_t retry = header_seconds("Retry-After", "");
    if (state.not_before - now > retry) { retry = state.not_before - now; }
    state.not_before = retry > delay ? now + retry : delay ? now + delay : 0;
    if (state.not_before) { cancel_browse(); }
    /* Waiting the full max-age is conservative when an Age header is present. */
    int64_t ttl = header_seconds("Cache-Control", "max-age=");
    if (request && ok) { state.cache_seconds = ttl > 900 ? ttl : 900; }
    if (ttl > retry) { retry = ttl; }
    if (retry > delay) { delay = retry > 86400 ? 86400 : (int32_t)retry; }
    poll_set_interval(0, delay > refresh ? delay : 0);
    state.status = ok ? 200 : HOST_FAILED_NO_ANSWER;
    if (retry > 86400) { state.pause_until = now + retry; provider_pause(); }
}
static int32_t shown_slot(void) { return scene.scope; }
static event_t *shown_events(void) { return cache.events[shown_slot()]; }
static int32_t shown_count(void)
{
    return scene.scope && cache.browse_page != scene.page ? 0 : cache.count[scene.scope];
}
static void selection(void)
{
    scene.list.count = shown_count();
    if (scene.list.selected >= scene.list.count) { scene.list.selected = 0; }
}
void app_on_picked(int32_t request, int32_t http_status)
{
    if (request < 0 || request > 1 || (request == 0 && !placed)) { return; }
    if (request == 1 && (!requesting || http_status == HOST_FAILED_NOT_READY)) { return; }
    uint32_t before = checksum(&cache);
    if (request == 1) { cancel_browse(); }
    int32_t n = 0;
    event_t fresh[BATCH]; zero(fresh, sizeof fresh);
    bool ok = http_status == 204 || (http_status == 200 && extract(fresh, &n));
    if (ok) {
        bool seed = state.seed || stale(0);
        for (int32_t i = 0; i < n; i++) {
            bool known = false;
            for (int32_t slot = 0; slot < 2; slot++) for (int32_t j = 0; j < cache.count[slot]; j++) {
                event_t *old = &cache.events[slot][j];
                if (!same_event(&fresh[i], old)) { continue; }
                known = true;
                if (old->updated > fresh[i].updated) { copy_bytes(&fresh[i], old, sizeof *old); }
                else { copy_bytes(old, &fresh[i], sizeof *old); }
            }
            if (!request && !known) { observe(&fresh[i], seed); }
            revise_notice(&fresh[i]);
        }
        copy_bytes(cache.events[request], fresh, sizeof fresh); cache.count[request] = n;
        if (request) {
            cache.browse_page = requested_page;
        } else {
            state.seed = 0;
            if (n && fresh[0].time > state.observed) { state.observed = fresh[0].time; }
        }
    }
    bool good = ok || (http_status == 304 && success[request]);
    if (good) {
        int64_t now = time_now();
        success[request] = now;
        if (request || (uint64_t)(now - state.success[request]) >= 3600) { state.success[request] = now; }
    }
    pace(request, good);
    save_cache();
    if (!request && ok) { try_notice(); }
    selection();
    if (request || checksum(&cache) != before) { screen_redraw(HOST_REDRAW_NORMAL); }
}

/* The provider pages eight reports at a time within 120 days. Only the
 * current page is cached; a moving feed can shift offset boundaries. */
static void browse(int32_t page)
{
    if (requesting) { page = requested_page; }
    if (!time_now() || page > 1000 || time_now() < state.not_before) { return; }
    char text[16];
    FILL(text, "{0}", page * BATCH + 1, NULL, NULL);
    if (kv_set("offset", 6, text, app_text_length(text)) < 0) { return; }
    if (cache.browse_page != page) { scene.list.selected = scene.list.first = 0; }
    scene.page = requested_page = page; requesting = true;
    if (poll_run(1) < 0) { cancel_browse(); pace(1, false); save_cache(); }
}
static void history_page(int32_t direction)
{
    if (requesting) { return; }
    if (direction < 0) { scene.page = 0; }
    else { browse(scene.page + 1); }
}

static void maybe_browse(void)
{
    if (scene.scope && scene.nav.screen &&
        (requesting || cache.browse_page != scene.page || (uint64_t)(time_now() - success[1]) >= (uint64_t)state.cache_seconds)) { browse(scene.page); }
}

void app_on_load(void)
{
    zero(&scene, sizeof scene); scene.nav.screens = 2;
    scene.list.top = 42; scene.list.height = 170;
    load_cache(); settings();
    int32_t offset = 0;
    requesting = setting_number("offset", 0, &offset) && (uint32_t)(offset - 1) <= 8000;
    requested_page = (uint32_t)(offset - 1) / BATCH;
    for (int32_t i = 0; i < 2; i++) { success[i] = state.success[i]; }
    if (state.boot != time_boot_id()) {
        state.boot = time_boot_id(); state.seed = 1; state.pending = 0;
    }
    if (state.pending == 2) { state.pending = 0; }
    if (!requesting) { cancel_browse(); }
    provider_pause(); save_cache();
}
void app_on_focus(void)
{
    app_session_load(&scene, sizeof scene);
    selection(); maybe_browse(); timer_set(0, 300000);
}
void app_on_blur(void)
{
    app_session_save(&scene, sizeof scene);
}
void app_on_settings(void)
{
    settings(); reset_configuration(); provider_pause();
    save_cache(); selection(); screen_redraw(HOST_REDRAW_NORMAL);
}
void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED || intent == HOST_INTENT_TIME_CHANGED) {
        state.seed = 1; state.pending = 0;
        provider_pause(); save_cache();
        screen_redraw(HOST_REDRAW_NORMAL);
    }
}
void app_on_updated(void) { app_on_intent(HOST_INTENT_STARTED); }
void app_on_alarm(int32_t id)
{
    if (id != 0) { return; }
    provider_pause();
    if (!state.pause_until) { poll_run(0); }
    save_cache();
}
void app_on_timer(int32_t id)
{
    if (id == 0) { maybe_browse(); timer_set(0, 300000); screen_redraw(HOST_REDRAW_NORMAL); }
}
void app_on_back(void)
{
    if (app_nav_button(&scene.nav, BTN_BACK_PRESSED) == APP_NAV_IGNORED) { screen_leave(); }
    else { screen_redraw(HOST_REDRAW_NORMAL); }
}
static int32_t notable(void)
{
    for (int32_t i = 0; i < cache.count[0]; i++) { if (relevant(&cache.events[0][i])) { return i; } }
    return -1;
}
void app_on_button(int32_t event)
{
    if (scene.nav.depth) { return; }
    if (event == BTN_UP_LONG_PRESSED || event == BTN_DOWN_LONG_PRESSED) {
        scene.nav.screen ^= 1; goto redraw;
    }
    if (scene.nav.screen == 0) {
        if (event == BTN_UP_PRESSED || event == BTN_DOWN_PRESSED) { scene.nav.screen = 1; }
        else if (event == BTN_OK_PRESSED && notable() >= 0) { copy_bytes(&scene.detail, &cache.events[0][notable()], sizeof(event_t)); scene.nav.depth = 1; }
        else { return; }
    } else {
        if (event == BTN_OK_LONG_PRESSED) {
            scene.scope = 1 - scene.scope; scene.page = scene.scope && requesting ? requested_page : 0;
            scene.list.selected = scene.list.first = 0; selection();
        } else if (event == BTN_OK_PRESSED && shown_count()) {
            copy_bytes(&scene.detail, &shown_events()[scene.list.selected], sizeof(event_t)); scene.nav.depth = 1;
        } else if (event == BTN_DOWN_PRESSED && scene.scope && shown_count() && scene.list.selected == shown_count() - 1) {
            history_page(1);
        } else if (event == BTN_UP_PRESSED && scene.list.selected == 0 && scene.page) {
            history_page(-1);
        } else {
            if (event != BTN_UP_PRESSED && event != BTN_DOWN_PRESSED) { return; }
            if (shown_count()) { scene.list.selected = (scene.list.selected + shown_count() + (event == BTN_UP_PRESSED ? -1 : 1)) % shown_count(); }
        }
    }
redraw:
    maybe_browse(); screen_redraw(HOST_REDRAW_NORMAL);
}

/* The selected typeface supplies all metrics; provider names are unchanged
 * and the kit shortens them between UTF-8 characters. All y are app-local. */
static void draw_text(int32_t x, int32_t y, int32_t style, const char *text, int32_t width)
{
    ui_draw_text(x, y, style, text, app_text_length(text), width);
}
static void disc(int32_t x, int32_t y)
{
    for (int32_t dy = -4; dy <= 4; dy++) {
        int32_t width = dy < -2 || dy > 2 ? 5 : 9;
        gfx_draw_rect(x - width / 2, y + dy, width, 1, HOST_RECT_FILLED);
    }
}
static void marker(const host_map_t *map, int32_t lat, int32_t lon, bool home)
{
    host_map_point_t p;
    gfx_map_point(map, lat, lon, &p);
    gfx_set_color(HOST_PAPER); gfx_draw_rect(p.x - 7, p.y - 7, 15, 15, HOST_RECT_FILLED);
    gfx_set_color(HOST_INK);
    if (home) { gfx_draw_icon(p.x - 5, p.y - 5, HOST_ICON_PLUS, 1); }
    else { disc(p.x, p.y); }
}
static void event_map(host_map_t *map, const event_t *e, bool label)
{
    int32_t ring = e ? relevance(e->mag) : 0;
    int32_t r = e ? (ring ? ring : 100) : radius_km;
    int32_t d = e && label ? from_place(e) / 1000 : 0;
    bool home_center = label;
    int32_t extent = (home_center ? d : 0) + r;
    int32_t scale = extent * 2000 / (map->h - 24);
    if (scale < 1500) { scale = 1500; }
    if (scale > 25000) { scale = 25000; }
    map->projection = HOST_MAP_AEQD;
    map->lat_e6 = home_center ? latitude : e->lat;
    map->lon_e6 = home_center ? longitude : e->lon;
    map->m_per_px = scale;
    map->land = 4; map->sea = HOST_PAPER; map->flags = HOST_MAP_COASTLINE;
    gfx_draw_map(map);
    if (ring) { gfx_draw_map_radius(map, e->lat, e->lon, ring * 1000); }
    if (e) { marker(map, e->lat, e->lon, false); }
    if (label) { marker(map, latitude, longitude, true); }

}
static void when_text(char out[32], int64_t at)
{
    int32_t n = time_text(at, HOST_TIME_NEAR, out, 31);
    out[n > 0 ? n : 0] = 0;
}
static void draw_main(void)
{
    nav_title(&scene.nav);
    int32_t i = notable();
    const event_t *e = i < 0 ? NULL : &cache.events[0][i];
    char line[128], a[64], b[32];
    if (!placed) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    host_map_t map = { .x = 0, .y = 44, .w = 400, .h = 180 };
    event_map(&map, e, true);
    gfx_draw_rect(0, 224, 400, 1, HOST_RECT_FILLED);
    if (e) {
        summary(a, e); when_text(b, e->time / 1000);
        FILL(line, "{1}, {2}", 0, a, b);
    } else { copy_text(line, sizeof line, word(success[0] ? STR_NO_EVENTS : state.status < 0 ? STR_OFFLINE : STR_WAIT)); }
    draw_text(16, 231, HOST_UI_TEXT, line, 368);
    if (state.status < 0 || stale(0)) { draw_text(16, 16, HOST_UI_CAPTION, word(state.status < 0 ? STR_OFFLINE : STR_STALE), 180); }
}
static void draw_list(void)
{
    nav_title(&scene.nav);
    char line[128], a[32], b[32];
    FILL(line, "{1}  {2}", 0, word(scene.scope ? STR_WORLDWIDE : STR_NEARBY), word(scene.page ? STR_EARLIER : STR_LATEST));
    draw_text(16, 16, HOST_UI_TEXT, line, 304);
    selection();
    int32_t count = shown_count();
    int32_t first = ui_list_first(&scene.list), visible = ui_list_visible(&scene.list);
    scene.list.first = first;
    event_t *events = shown_events();
    for (int32_t i = first; i < first + visible && i < count; i++) {
        magnitude(a, &events[i]); when_text(b, events[i].time / 1000);
        FILL(line, "{1} {2}", 0, a, events[i].place);
        ui_draw_list_row(&scene.list, i, line, app_text_length(line), b, app_text_length(b), HOST_ICON_NONE);
    }
    if (!count) {
        if (scene.scope == 0 && !placed) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
        if (!ui_draw_reason(state.status)) { draw_text(16, 100, HOST_UI_TEXT, word(success[scene.scope] ? STR_NO_EVENTS : STR_WAIT), 368); }
    }
    int32_t caption = scene.scope && requesting ? STR_WAIT : state.status < 0 ? STR_OFFLINE : stale(scene.scope) ? STR_STALE : STR_CHECKED;
    FILL(line, "{1}  U.S. Geological Survey", 0, word(caption), NULL);
    draw_text(16, 225, HOST_UI_CAPTION, line, 368);
}
static void draw_detail(void)
{
    const event_t *e = &scene.detail;
    char text[128], a[48];
    magnitude(text, e);
    draw_text(16, 4, e->mag == 100 ? HOST_UI_TITLE : HOST_UI_VALUE, text, 180);
    ui_draw_text_lines(16, 84, HOST_UI_TEXT, e->place, app_text_length(e->place), 180, 2);
    host_map_t map = { .x = 208, .y = 12, .w = 176, .h = 146 };
    event_map(&map, e, false);
    gfx_draw_rect(207, 11, 178, 148, HOST_RECT_OUTLINE);
    quantity(text, placed ? (from_place(e) + 500) / 1000 : UNKNOWN);
    draw_text(16, 138, HOST_UI_CAPTION, text, 180);
    quantity(a, e->depth);
    FILL(text, "{1} {2}", 0, word(STR_DEPTH), a);
    draw_text(16, 156, HOST_UI_CAPTION, text, 180);
    when_text(text, e->time / 1000);
    draw_text(16, 174, HOST_UI_CAPTION, text, 368);
    draw_text(16, 196, HOST_UI_CAPTION, "U.S. Geological Survey", 368);
    app_draw_actions(NULL, word(STR_BACK));
}
void app_on_draw(void)
{
    if (scene.nav.depth != 0) { draw_detail(); }
    else if (scene.nav.screen == 0) { draw_main(); }
    else { draw_list(); }
}
