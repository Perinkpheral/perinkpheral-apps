// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GrakovNe
#include <stddef.h>
#include "host_api.h"
#include "app_draw.h"
#include "app_state.h"
#include "app_text.h"
#include "auth.h"
#include "metrics.h"
#include "history.h"
#include "app_values.h"
#define keenetic_clear(p, n) __builtin_memset(p, 0, n)
#include "details.h"
#include "strings.h"

enum { BATCH, INTERFACES, LINKS, HOSTS, POLLS };
enum { AUTH, LOGIN, RENEW = 8 };
enum { REFRESH = 0, WATCHDOG = 1, CHECKPOINT_SECONDS = 900 };
enum { shown_rows = 3, WAN_FACTS = 0x10f, LINK_FACTS = 0xc4 };
_Static_assert(162 * shown_rows / KEENETIC_HOSTS_MAX >= 12, "kit scrollbar minimum thumb");
typedef struct {
    int32_t version, epoch, blocked, renewing, auth;
} control_t;
typedef struct {
    int32_t version, has_data;
    keenetic_reading_t reading;
    int64_t saved_at;
    uint32_t boot;
} snapshot_t;
typedef struct {
    uint32_t magic;
    snapshot_t snapshot;
    union {
        char facts[10][40];
        struct { keenetic_host_t rows[shown_rows]; int count, total, first; } viewport;
    } details;
    int32_t detail_status;
    int64_t down, up;
    int32_t page, status, pending, epoch;
    keenetic_history_t history;
} scene_t;
APP_SESSION_FITS(scene_t);
static scene_t scene, previous;
static char cookie[256];
static control_t control;
static bool visible, restored, receiving, snapshot_dirty;
static int32_t response_status;
/* Picked details are transient; only the visible viewport enters the session. */
static keenetic_hosts_t hosts;
static int device_first;
#define detail_status scene.detail_status
#define facts scene.details.facts
static int detail_page;
#define SCENE_MAGIC 0x4b454e3bu

/* Share the language-table lookup instead of expanding it at every label. */
__attribute__((noinline)) static const char *word(int id)
{
    return app_string(id);
}

static void clear_cookie(void)
{
    cookie[0] = 0;
    app_state_forget("session");
}
static void save_control(void) { app_state_save("control", &control, sizeof control); }
/* A single scratch scene avoids a second session reader and a large handler stack frame. */
static bool load_scene(void)
{
    return app_session_load(&previous, sizeof previous) &&
           previous.magic == SCENE_MAGIC && previous.epoch == control.epoch;
}
static void restore(void)
{
    if (restored)
        return;
    restored = true;
    if (visible && !control.blocked && load_scene()) {
        /* A completed background read supersedes the sleeping scene's checkpoint. */
        if (scene.snapshot.has_data && (!previous.snapshot.has_data || scene.snapshot.saved_at > previous.snapshot.saved_at)) {
            previous.snapshot = scene.snapshot;
            previous.down = scene.snapshot.reading.down;
            previous.up = scene.snapshot.reading.up;
        }
        scene = previous;
        if (scene.page == 3) {
            hosts.count = scene.details.viewport.count;
            hosts.total = scene.details.viewport.total;
            device_first = scene.details.viewport.first;
            __builtin_memcpy(hosts.rows + device_first, scene.details.viewport.rows,
                             sizeof scene.details.viewport.rows);
        }
        /* The old read may have finished off screen; start another without writing flash. */
        scene.pending = 0;
    }
    scene.epoch = control.epoch;
    if (control.auth) scene.pending = control.auth;
    app_setting("session", cookie, sizeof cookie);
}
static bool safe_name(const char *name);
__attribute__((noinline)) static bool configured(void)
{
    char value[256];
    if (!app_setting("router", value, sizeof value) ||
        !app_setting("username", value, sizeof value) ||
        !app_setting("password", value, sizeof value) ||
        !app_setting("wan", value, sizeof value) || !safe_name(value)) return false;
    int count = app_setting_count("links");
    if (count < 1 || count > 3) return false;
    for (int i = 0; i < count; i++)
        if (!app_setting_item("links", i, value, sizeof value) || !safe_name(value)) return false;
    return true;
}
static void header(void)
{
    int id = control.blocked                                                      ? STR_DENIED
             : scene.status != 200 && scene.status != 0                        ? STR_NO_CONTACT
             : scene.snapshot.has_data && scene.snapshot.reading.internet == 0 ? STR_NO_INTERNET
                                                                               : -1;
    const char *text = id < 0 ? "Keenetic" : word(id);
    header_set_text(HOST_HEADER_LEFT, text, app_text_length(text));
}
/* Share scheduling and setting lookup to keep the interpreter code within its budget. */
__attribute__((noinline)) static void schedule(int32_t seconds)
{
    if (seconds) alarm_set_every(REFRESH, seconds < 60 ? 60 : seconds, seconds);
    else alarm_set(REFRESH, 0);
}
__attribute__((noinline)) static int32_t pace(void)
{
    if (!visible) return 900;
    char every[8];
    app_setting("every", every, sizeof every);
    return app_text_equal(every, "15 min") ? 900 : app_text_equal(every, "5 min") ? 300 : 60;
}
__attribute__((noinline)) static void read_fact(int pick, int field);
static void finish(int32_t status)
{
    int poll = scene.pending - 1;
    bool detail = poll > BATCH && poll < POLLS;
    bool own_detail = scene.page == 3 ? poll == HOSTS : scene.page == 1 ? poll == LINKS :
                      scene.page == 2 && (poll == INTERFACES || poll == LINKS);
    scene.pending = 0;
    receiving = false;
    if (status == 401 || status == 403) control.blocked = 1;
    if (!detail || control.blocked) scene.status = status;
    if (own_detail) {
        detail_status = status;
        if (status != 200 && scene.page != 3) {
            for (int i = 0; i < 9; i++)
                if ((poll == INTERFACES ? WAN_FACTS : LINK_FACTS) & (1 << i)) read_fact(-1, i);
        }
    }
    if (control.auth) {
        control.auth = 0;
        save_control();
    }
    alarm_set(WATCHDOG, 0);
    if (status != 200 && !detail) {
        scene.down = -1;
        scene.up = -1;
        scene.history.baseline = false;
        if (visible)
            keenetic_history_add(&scene.history, -1, -1);
    }
    if (control.blocked) {
        save_control();
        schedule(0);
    } else if (!configured()) {
        scene.status = 0;
        schedule(0);
    } else {
        int32_t delay = pace();
        /* Repeated failures need a retry delay, not another flash write. */
        if (status != 200 && delay < 120)
            delay = 120;
        schedule(delay);
    }
    header();
    screen_redraw(HOST_REDRAW_NORMAL);
}
static bool safe_name(const char *name)
{
    if (!*name || app_text_length(name) >= 32)
        return false;
    /* ASCII letters, digits, underscore, hyphen and dot, as before. */
    static const uint32_t allowed[4] = {0x0u, 0x3ff6000u, 0x87fffffeu, 0x7fffffeu};
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c >= 128 || !(allowed[c / 32] & (1u << (c % 32))))
            return false;
    }
    return true;
}
__attribute__((noinline)) static void requested(int pending)
{
    scene.pending = pending;
    schedule(pace());
    alarm_set(WATCHDOG, 330);
}
__attribute__((noinline)) static void request_poll(int poll)
{
    int result = poll_run(poll);
    if (result != HOST_OK) {
        scene.pending = poll + 1;
        finish(HOST_FAILED_NOT_READY);
        return;
    }
    requested(poll + 1);
}
static void send_login(int phase, int retry_poll, bool renewed, const char *body)
{
    static const char url[] = "{{router}}/auth";
    static const char login_headers[] = "Content-Type: application/json\nCookie: {{session}}";
    const char *headers = phase == LOGIN ? login_headers : "";
    int tag = HOST_REQUEST_FIRST + control.epoch * 16 + retry_poll * 2 + phase + (renewed ? RENEW : 0);
    control.auth = tag;
    requested(tag);
    if (!app_state_save("control", &control, sizeof control)) {
        control.auth = 0;
        finish(HOST_FAILED_NO_MEMORY);
        return;
    }
    int result = http_send(tag, phase == LOGIN ? HOST_HTTP_POST : HOST_HTTP_GET, url,
                          sizeof url - 1, headers, phase == LOGIN ? sizeof login_headers - 1 : 0, body, app_text_length(body));
    if (result != HOST_OK) {
        finish(result == HOST_ERR_NO_SPACE ? HOST_FAILED_NO_MEMORY : HOST_FAILED_NOT_READY);
        return;
    }
}
__attribute__((noinline)) static void request_details(void)
{
    detail_page = scene.page;
    int poll = scene.page == 3 ? HOSTS : scene.page == 1 ? LINKS : INTERFACES;
    char wan[32]; app_setting("wan", wan, sizeof wan);
    if (poll == INTERFACES && !app_text_equal(wan, scene.snapshot.reading.wan)) {
        for (int i = 0; i < 9; i++) {
            if (i == 4 || i == 5) continue;
            read_fact(-1, i);
        }
        poll = LINKS;
    }
    request_poll(poll);
}
static void start(bool explicit_retry)
{
    restore();
    if (!configured()) { finish(HOST_FAILED_NOT_READY); return; }
    if (scene.pending || (control.blocked && !explicit_retry)) return;
    if (explicit_retry && control.blocked) {
        control.blocked = control.renewing = 0;
        save_control();
        clear_cookie();
    }
    if (cookie[0]) request_poll(BATCH);
    else send_login(AUTH, BATCH, false, "");
}
static bool response_header(const char *name, char *out, int cap)
{
    int n = answer_header(name, app_text_length(name), 0, out, cap - 1);
    if (n <= 0 || n >= cap)
        return false;
    out[n] = 0;
    /* Do not pass control bytes or template delimiters back into headers. */
    for (int i = 0; i < n; i++)
        if ((unsigned char)out[i] < 32 || out[i] == '{' || out[i] == '}')
            return false;
    return true;
}
static bool accept_cookie(bool required)
{
    char value[512], accepted[256];
    int n = answer_header("Set-Cookie", 10, 0, value, sizeof value - 1);
    if (n == HOST_ERR_NOT_FOUND)
        return !required;
    if (n <= 0 || n >= (int)sizeof value)
        return false;
    value[n] = 0;
    /* A second cookie is not silently truncated or confused with attributes. */
    char other[2];
    if (answer_header("Set-Cookie", 10, 1, other, sizeof other) != HOST_ERR_NOT_FOUND)
        return false;
    if (!keenetic_cookie(value, accepted))
        return false;
    /* RCI reissues the 300-second cookie on every answer. Its lifetime is
     * renewed by the router; unchanged name/value pairs need no KV write. */
    if (!app_text_equal(cookie, accepted)) {
        if (kv_set("session", 7, accepted, app_text_length(accepted)) != HOST_OK) return false;
        app_text_copy(cookie, sizeof cookie, accepted);
    }
    return true;
}
static void login(int retry_poll, bool renewed)
{
    char realm[256], challenge[256], password[256], username[256], digest[65], body[513];
    if (!response_header("X-NDM-Realm", realm, sizeof realm) ||
        !response_header("X-NDM-Challenge", challenge, sizeof challenge) || !accept_cookie(true) ||
        !app_setting("username", username, sizeof username) ||
        !app_setting("password", password, sizeof password)) {
        finish(HOST_FAILED_NO_ANSWER);
        return;
    }
    keenetic_digest(username, realm, password, challenge, digest);
    if (!keenetic_login_body(username, digest, body)) {
        finish(HOST_FAILED_NOT_READY);
        return;
    }
    send_login(LOGIN, retry_poll, renewed, body);
    /* No challenge, password or auth payload is stored or logged. */
    for (unsigned i = 0; i < sizeof password; i++)
        ((volatile char *)password)[i] = 0;
    for (unsigned i = 0; i < sizeof body; i++)
        ((volatile char *)body)[i] = 0;
}
/* Empty means pending; a completed pick stores "-" for absent/unspecified metadata. */
__attribute__((noinline)) static void read_fact(int pick, int field)
{
    char *value = facts[field];
    app_value_text(pick, value, sizeof facts[0]);
    if (!*value || app_text_equal(value, "0.0.0.0") || app_text_equal(value, "null")) {
        value[0] = '-';
        value[1] = 0;
    }
}
static void accept_batch(void)
{
    keenetic_reading_t reading;
    if (!keenetic_read(&reading)) { finish(HOST_FAILED_NO_ANSWER); return; }
    if (scene.page != 3) {
        for (int i = 0; i < 2; i++) read_fact(10 + i, 4 + i);
        read_fact(12, 9);
    }
    int64_t sample_ms = time_uptime_ms();
    scene.down = reading.down;
    scene.up = reading.up;
    if (visible && (!scene.history.baseline || sample_ms - scene.history.last_ms >= (int64_t)pace() * 1000)) {
        if (scene.history.count && sample_ms - scene.history.last_ms > (int64_t)pace() * 2500)
            keenetic_history_add(&scene.history, -1, -1);
        keenetic_history_add(&scene.history, scene.down, scene.up);
        scene.history.last_ms = sample_ms;
        scene.history.baseline = true;
    }
    bool first = !scene.snapshot.has_data;
    scene.snapshot.reading = reading;
    scene.status = 200;
    scene.snapshot.has_data = 1;
    int64_t now = time_uptime_ms() / 1000;
    if (first || snapshot_dirty || scene.snapshot.boot != time_boot_id() ||
        now - scene.snapshot.saved_at >= CHECKPOINT_SECONDS) {
        scene.snapshot.saved_at = now;
        scene.snapshot.boot = time_boot_id();
        snapshot_dirty = !app_state_save("snapshot", &scene.snapshot, sizeof scene.snapshot);
    }
    if (visible && scene.page) {
        detail_status = 0;
        request_details();
    } else finish(200);
}
/* An expired session: the router answers 401 with an HTML page and its login challenge. A poll
 * whose answer is not JSON is handed over as HOST_FAILED_NO_ANSWER with its headers kept, so the
 * challenge is the sign that a login is wanted, not the status. */
static bool asks_for_login(int32_t status)
{
    if (status == 401) return true;
    if (status != HOST_FAILED_NO_ANSWER) return false;
    static const char scheme[] = "x-ndw2-interactive";
    char value[160];
    int n = answer_header("WWW-Authenticate", 16, 0, value, sizeof value - 1);
    if (n < (int)sizeof scheme - 1) return false;
    for (int i = 0; i < (int)sizeof scheme - 1; i++) {
        char c = value[i] >= 'A' && value[i] <= 'Z' ? (char)(value[i] - 'A' + 'a') : value[i];
        if (c != scheme[i]) return false;
    }
    return n == (int)sizeof scheme - 1 || value[sizeof scheme - 1] == ' ';
}
void app_on_picked(int32_t poll, int32_t status)
{
    restore();
    if (poll < 0 || poll >= POLLS || control.blocked || control.auth) return;
    if (asks_for_login(status)) {
        if (control.renewing) { finish(401); return; }
        clear_cookie();
        control.renewing = 1;
        save_control();
        send_login(AUTH, poll, true, "");
        return;
    }
    scene.pending = poll + 1;
    if (status != 200) { finish(status); return; }
    if (!accept_cookie(false)) { finish(HOST_FAILED_NO_ANSWER); return; }
    if (control.renewing) { control.renewing = 0; save_control(); }
    if (poll == BATCH) { accept_batch(); return; }
    if (poll == HOSTS) {
        keenetic_hosts_read(&hosts);
    } else if (poll == INTERFACES) {
        if (scene.page != 3) {
            for (int i = 0; i < 4; i++) read_fact(i, i);
            read_fact(4, 8);
            if (facts[8][0] == '-') app_text_copy(facts[8], sizeof facts[8], facts[9]);
            /* An absent WAN speed still has the carrier pick to come. */
            if (facts[2][0] == '-') facts[2][0] = 0;
        }
        if (visible && scene.page == 2) { request_poll(LINKS); return; }
    } else {
        int item = answer_item();
        if (item < 0 || item > 2) return;
        if (scene.page != 3) {
            if (item) read_fact(1, 5 + item);
            else if (!facts[2][0]) {
                char carrier[32]; app_setting_item("links", 0, carrier, sizeof carrier);
                read_fact(app_text_equal(carrier, facts[3]) || app_text_equal(carrier, scene.snapshot.reading.wan) ? 0 : -1, 2);
            }
        }
        if (item + 1 < app_setting_count("links")) return;
    }
    int wanted = scene.page == 3 ? HOSTS : LINKS;
    if (visible && scene.page && (poll != wanted || detail_page != scene.page)) {
        request_details();
        return;
    }
    finish(200);
}
void app_on_data(int32_t tag, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)data; (void)length; /* Login is defined by status and challenge headers. */
    restore();
    if (tag != control.auth || tag < HOST_REQUEST_FIRST || (tag - HOST_REQUEST_FIRST) / 16 != control.epoch) return;
    int bits = (tag - HOST_REQUEST_FIRST) & 15;
    int phase = bits & 1, retry_poll = (bits & 7) / 2;
    bool renewed = (bits & RENEW) != 0;
    if (retry_poll >= POLLS) return;
    if (flags & HOST_DATA_FAILED) { finish(status); return; }
    if (flags & HOST_DATA_FIRST) {
        response_status = status; receiving = true;
    }
    if (!receiving || !(flags & HOST_DATA_LAST)) return;
    receiving = false;
    if (phase == AUTH && response_status == 401) { login(retry_poll, renewed); return; }
    if (response_status != 200) { finish(response_status); return; }
    if (!accept_cookie(false)) { finish(HOST_FAILED_NO_ANSWER); return; }
    control.auth = 0;
    save_control();
    request_poll(retry_poll);
}
void app_on_load(void)
{
    if (!app_state_load("control", &control, sizeof control, 3)) {
        keenetic_clear(&control, sizeof control);
        control.version = 3;
    }
    if (control.epoch < 0 || control.epoch > 13)
        control.epoch = 0;
    keenetic_clear(&scene, sizeof scene);
    scene.magic = SCENE_MAGIC;
    scene.down = scene.up = -1;
    if (!app_state_load("snapshot", &scene.snapshot, sizeof scene.snapshot, 4)) {
        keenetic_clear(&scene.snapshot, sizeof scene.snapshot);
        scene.snapshot.version = 4;
    }
    if (scene.snapshot.has_data) {
        scene.down = scene.snapshot.reading.down;
        scene.up = scene.snapshot.reading.up;
    }
    if (control.blocked)
        scene.status = 401;
    cookie[0] = 0;
    keenetic_clear(&hosts, sizeof hosts);
    visible = false;
    restored = false;
    receiving = false;
    snapshot_dirty = false;
}
void app_on_focus(void)
{
    visible = true;
    restore();
    header();
    start(false);
}
void app_on_blur(void)
{
    if (scene.page == 3) {
        __builtin_memcpy(scene.details.viewport.rows, hosts.rows + device_first,
                         sizeof scene.details.viewport.rows);
        scene.details.viewport.count = hosts.count;
        scene.details.viewport.total = hosts.total;
        scene.details.viewport.first = device_first;
    }
    app_session_save(&scene, sizeof scene);
    visible = false;
    if (!scene.pending && !control.blocked && configured())
        schedule(pace());
}
void app_on_alarm(int32_t id)
{
    restore();
    if (id == WATCHDOG) {
        /* Raw responses can be abandoned without a final callback. Invalidate
         * their tags before retrying; no per-sample pending record in flash. */
        control.epoch = (control.epoch + 1) % 14;
        control.renewing = 0;
        save_control();
        scene.pending = 0;
        finish(HOST_FAILED_TIMEOUT);
    } else
        start(false);
}
void app_on_settings(void)
{
    restore();
    control.epoch = (control.epoch + 1) % 14;
    scene.epoch = control.epoch;
    control.blocked = control.renewing = control.auth = 0;
    save_control();
    clear_cookie();
    scene.pending = 0;
    keenetic_clear(&scene.history, sizeof scene.history);
    scene.down = scene.up = -1;
    keenetic_clear(&scene.snapshot, sizeof scene.snapshot);
    scene.snapshot.version = 4;
    app_state_forget("snapshot");
    keenetic_clear(facts, sizeof facts);
    keenetic_clear(&hosts, sizeof hosts);
    detail_status = 0;
    start(true);
}
void app_on_intent(int32_t intent)
{
    if (intent == HOST_INTENT_STARTED) {
        restore();
        if (scene.pending) alarm_set(WATCHDOG, 1);
        else if (!control.blocked) schedule(1);
    }
}
void app_on_installed(void)
{
    app_on_intent(HOST_INTENT_STARTED);
}
void app_on_updated(void)
{
    if (!control.blocked) {
        schedule(pace());
        /* The package lost its alarms; a queued request still needs its watchdog. */
        if (control.auth || (load_scene() && previous.pending))
            alarm_set(WATCHDOG, 330);
    }
}
void app_on_button(int32_t event)
{
    int step = event == BTN_UP_PRESSED || event == BTN_UP_LONG_PRESSED ? -1 :
               event == BTN_DOWN_PRESSED || event == BTN_DOWN_LONG_PRESSED ? 1 : 0;
    if (step) {
        int last = hosts.count - shown_rows;
        if (last < 0) last = 0;
        bool short_press = event == BTN_UP_PRESSED || event == BTN_DOWN_PRESSED;
        if (scene.page == 3 && (!short_press ||
            (step < 0 ? device_first > 0 : device_first < last))) {
            device_first += step * (short_press ? 1 : shown_rows);
            if (device_first < 0) device_first = 0;
            if (device_first > last) device_first = last;
        } else {
            if (scene.page == 3) keenetic_clear(facts, sizeof facts);
            scene.page = (scene.page + step + 4) % 4;
            if (scene.page == 3) device_first = step < 0 ? last : 0;
            start(false);
        }
        screen_redraw(HOST_REDRAW_SOON);
    } else if (event == BTN_OK_PRESSED)
        start(true);
}
__attribute__((noinline)) static void number(char *out, int cap, int64_t value, int decimals, const char *suffix)
{
    app_text_t t;
    app_text_start(&t, out, cap);
    if (value < 0)
        app_text_add(&t, "-");
    else {
        keenetic_number(&t, value, decimals);
        app_text_add(&t, suffix);
    }
}
/* Share string-length handling across body and caption drawing. */
__attribute__((noinline)) static void text(int x, int y, int style, const char *value, int width)
{
    ui_draw_text(x, y, style, value, app_text_length(value), width);
}
__attribute__((noinline)) static void page_title(int id)
{
    const int margin = HOST_UI_MARGIN;
    if (id >= 0)
        text(margin, margin, HOST_UI_TEXT, word(id), 320);
    char page[] = "1/4";
    page[0] += scene.page;
    ui_draw_text(gfx_width() - margin - app_text_width(HOST_FONT_SMALL, page), margin,
                 HOST_UI_CAPTION, page, 3, 48);
}
static void caption(int x, int y, const char *value, int width)
{
    text(x, y, HOST_UI_CAPTION, value, width);
}
enum { DOWNLOAD_SHADE = 4, UPLOAD_SHADE = 10 };
/* Paint the series with the larger peak first, the smaller one on top.
 * Redraw both outlines after all fills so crossings remain readable. */
static void traffic_chart(int top, int bottom)
{
    keenetic_history_t *h = &scene.history;
    if (!h->count) {
        caption(HOST_UI_MARGIN, top, word(STR_NO_DATA), gfx_width() - 2 * HOST_UI_MARGIN);
        return;
    }
    int32_t maximum = 0;
    int first = 0;
    for (int i = 0; i < h->count; i++) {
        int series = h->up[i] > h->down[i];
        int32_t value = series ? h->up[i] : h->down[i];
        if (value > maximum) {
            maximum = value;
            first = series;
        }
    }
    char label[32];
    number(label, sizeof label, maximum, 2, "");
    int right = gfx_width() - HOST_UI_MARGIN;
    int label_width = app_text_width(HOST_FONT_SMALL, label);
    int plot_width = right - HOST_UI_MARGIN - label_width - 8;
    caption(right - label_width, top, label, label_width);
    caption(right - label_width, bottom - gfx_font_height(HOST_FONT_SMALL), "0", label_width);
    int32_t denominator = maximum ? maximum : 1;
    for (int pass = 0; pass < 4; pass++) {
        int series = first ^ (pass & 1);
        const int32_t *values = series ? h->up : h->down;
        gfx_set_color(pass >= 2 ? HOST_INK : series ? UPLOAD_SHADE : DOWNLOAD_SHADE);
        int previous_x = -1, previous_y = 0;
        for (int i = 0; i < h->count; i++) {
            if (values[i] == KEENETIC_GAP) {
                previous_x = -1;
                continue;
            }
            int x = HOST_UI_MARGIN + (h->count > 1 ? i * (plot_width - 2) / (h->count - 1)
                                                                  : plot_width / 2);
            int y = bottom - 2 - (int)((int64_t)values[i] * (bottom - top - 3) / denominator);
            if (pass < 2) {
                if (previous_x >= 0) for (int at = previous_x; at <= x; at++) {
                    int row = previous_y + (y - previous_y) * (at - previous_x) / (x - previous_x);
                    gfx_draw_line(at, row, at, bottom - 2);
                }
            } else if (previous_x >= 0) {
                gfx_draw_line(previous_x, previous_y, x, y);
            } else if (i + 1 == h->count || values[i + 1] == KEENETIC_GAP)
                gfx_draw_rect(x - 1, y - 1, 4, 4, HOST_RECT_FILLED);
            previous_x = x;
            previous_y = y;
        }
    }
}
static void overview(void)
{
    page_title(-1);
    int margin = HOST_UI_MARGIN, width = gfx_width() - 2 * margin;
    int row = margin;
    char down[32], up[32], speed[96], connections[80];
    number(down, sizeof down, scene.down, 2, "");
    number(up, sizeof up, scene.up, 2, "");
    app_text_t t;
    app_text_start(&t, speed, sizeof speed);
    app_text_add(&t, down);
    app_text_add(&t, " / ");
    app_text_add(&t, up);
    ui_draw_value(margin, row, HOST_FONT_TEXT, speed, app_text_length(speed),
                  word(STR_MBIT), app_text_length(word(STR_MBIT)));
    row += gfx_font_height(HOST_FONT_TEXT);
    app_text_start(&t, connections, sizeof connections);
    app_text_add(&t, word(STR_CONNECTIONS));
    app_text_add(&t, " ");
    keenetic_number(&t, scene.snapshot.reading.connections, 0);
    caption(margin, row, connections, width);
    row += gfx_font_height(HOST_FONT_SMALL) + 4;
    int legend_top = gfx_height() - gfx_font_height(HOST_FONT_SMALL) - 4;
    traffic_chart(row, legend_top - 8);
    for (int series = 0; series < 2; series++) {
        int x = series ? gfx_width() / 2 : margin;
        gfx_set_color(series ? UPLOAD_SHADE : DOWNLOAD_SHADE);
        gfx_draw_rect(x, legend_top + 4, 16, 8, HOST_RECT_FILLED);
        gfx_set_color(HOST_INK);
        gfx_draw_rect(x, legend_top + 4, 16, 8, HOST_RECT_OUTLINE);
        caption(x + 24, legend_top, word(series ? STR_UPLOAD : STR_DOWNLOAD), width / 2 - 24);
    }
}
__attribute__((noinline)) static void fact(int y, int label, const char *value)
{
    caption(16, y, word(label), 170);
    caption(184, y, *value ? value : word(STR_LOADING), 200);
}
static int64_t radio_temperature(void)
{
    uint64_t temperature;
    int64_t highest = -1;
    for (int i = 6; i <= 7; i++) {
        if (keenetic_uint(facts[i], &temperature) && temperature <= INT32_MAX &&
            (int64_t)temperature > highest)
            highest = (int64_t)temperature;
    }
    return highest;
}
static void system_page(void)
{
    const keenetic_reading_t *v = &scene.snapshot.reading;
    const int labels[] = {STR_CPU, STR_RAM, STR_UPTIME, STR_TEMPERATURE};
    const int icons[] = {HOST_ICON_CHIP, HOST_ICON_CHIP, HOST_ICON_CLOCK, HOST_ICON_THERMOMETER};
    const int64_t values[] = {v->cpu, v->ram, v->uptime / 3600,
                              radio_temperature()};
    host_cell_t cells[4];
    int width = gfx_width() / 2, height = gfx_height() / 2;
    for (int i = 0; i < 4; i++) {
        cells[i] = (host_cell_t){.x = i % 2 * width, .y = i / 2 * height,
                                 .width = width, .height = height};
        app_text_copy(cells[i].name, sizeof cells[i].name, word(labels[i]));
        number(cells[i].value, sizeof cells[i].value, values[i], 0,
               i < 2 ? "%" : i == 2 ? word(STR_HOURS) : "°C");
    }
    app_draw_cells(cells, NULL, icons, 4);
    gfx_draw_rect(width, HOST_UI_MARGIN, 1, gfx_height() - 2 * HOST_UI_MARGIN, HOST_RECT_FILLED);
    gfx_draw_rect(HOST_UI_MARGIN, height, gfx_width() - 2 * HOST_UI_MARGIN, 1, HOST_RECT_FILLED);
    page_title(-1);
}
static void network_page(void)
{
    page_title(-1);
    fact(16, STR_INTERFACE, scene.snapshot.reading.wan);
    fact(38, STR_INTERNET, word(scene.snapshot.reading.internet == 1 ? STR_REACHABLE :
                                scene.snapshot.reading.internet == 0 ? STR_NO_INTERNET : STR_NO_DATA));
    static const int labels[] = {STR_ADDRESS, STR_GATEWAY, STR_DNS, STR_DNS2, STR_WAN_UPTIME, STR_LINK, STR_VIA};
    static const unsigned char fields[] = {0, 8, 4, 5, 1, 2, 3};
    for (int i = 0; i < 7; i++) {
        const char *value = facts[fields[i]];
        char hours[32];
        uint64_t seconds;
        if (i == 4 && keenetic_uint(value, &seconds)) {
            number(hours, sizeof hours, seconds / 3600, 0, word(STR_HOURS));
            value = hours;
        }
        fact(60 + 22 * i, labels[i], value);
    }
}
static void devices_page(void)
{
    page_title(STR_DEVICES);
    int last = hosts.count - shown_rows;
    if (last < 0) last = 0;
    if (device_first > last) device_first = last;
    if (!hosts.count) caption(16, 64, word(detail_status == 200 ? STR_EMPTY :
                           detail_status ? STR_NO_CONTACT : STR_LOADING), gfx_width() - 32);
    int width = gfx_width() - 2 * HOST_UI_MARGIN - (hosts.count > shown_rows ? 12 : 0);
    for (int i = device_first; i < hosts.count && i < device_first + shown_rows; i++) {
        int y = 56 + (i - device_first) * 56;
        const char *name = hosts.rows[i].ip[0] ? hosts.rows[i].name : word(STR_LOADING);
        text(HOST_UI_MARGIN, y, HOST_UI_TEXT, name, width);
        caption(HOST_UI_MARGIN, y + 28, hosts.rows[i].ip, width);
    }
    /* Match the kit scrollbar's track and 5 px thumb. At most 32 devices
     * keeps the thumb >= 15 px, above the kit's 12 px minimum. The list
     * remains read-only, as in V10/V11; each scroll step is a device. */
    if (hosts.count > shown_rows) {
        int height = 162 * shown_rows / hosts.count;
        int x = gfx_width() - 9;
        gfx_draw_rect(x + 2, 56, 1, 162, HOST_RECT_FILLED);
        gfx_draw_rect(x, 56 + (162 - height) * device_first / last, 5, height, HOST_RECT_FILLED);
    }
    char count[48];
    number(count, sizeof count / 2, hosts.count, 0, " / ");
    number(count + app_text_length(count), sizeof count / 2, hosts.total, 0, "");
    caption(16, 222, hosts.count && detail_status && detail_status != 200 ? word(STR_NO_CONTACT) : count, 368);
}
void app_on_draw(void)
{
    if (!configured()) { ui_draw_reason(HOST_FAILED_NOT_READY); return; }
    if (!scene.snapshot.has_data) { ui_draw_reason(scene.status); return; }
    switch (scene.page) {
    case 1: system_page(); break;
    case 2: network_page(); break;
    case 3: devices_page(); break;
    default: overview(); break;
    }
}
