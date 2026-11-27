// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures.h"
#include "json_pick.h"
/* Login and picked-response header tests share the recorded-header seam. */
#define alarm_set_every fixture_alarm_every
#define alarm_set fixture_alarm
#define poll_run fixture_poll_run
#define answer_header fixture_header
#define gfx_draw_line fixture_draw_line
#define gfx_draw_rect fixture_draw_rect
#define ui_draw_text fixture_draw_text
#include "../keenetic.c"
#undef alarm_set_every
#undef alarm_set
#undef poll_run
#undef answer_header
#undef ui_draw_text
#undef gfx_draw_rect
#undef gfx_draw_line
#define finish hash_finish
#include "../auth.c"
#undef finish
#include "../metrics.c"
#include "../history.c"
#include "../details.c"
#include "standin.h"

static bool refuse_every, refuse_watchdog;
int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first);
int32_t alarm_set(int32_t id, int32_t seconds);
int32_t fixture_alarm_every(int32_t id, int32_t seconds, int32_t first)
{
    return refuse_every ? HOST_ERR_NO_SPACE : alarm_set_every(id, seconds, first);
}
int32_t fixture_alarm(int32_t id, int32_t seconds)
{
    return refuse_watchdog && id == WATCHDOG && seconds ? HOST_ERR_NO_SPACE : alarm_set(id, seconds);
}
static int refused_poll = -1;
int32_t poll_run(int32_t poll);
int32_t fixture_poll_run(int32_t poll)
{
    return poll == refused_poll ? HOST_ERR_NOT_FOUND : poll_run(poll);
}
int32_t ui_draw_text(int32_t x, int32_t y, int32_t style, const char *value,
                     int32_t length, int32_t width);
int32_t gfx_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t mode);

/* Record the actual application draw calls, then use the SDK stand-in normally. */
static struct { int x, y, style, width; char value[128]; } drawn_text[32];
static int drawn_text_count, drawn_rect_count, chart_lines;
static bool recording_chart;
static struct { int x, y, width, height, mode; } drawn_rect[8];
static bool recording_draw;
int32_t gfx_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
int32_t fixture_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (recording_chart) chart_lines++;
    return gfx_draw_line(x1, y1, x2, y2);
}
int32_t fixture_draw_text(int32_t x, int32_t y, int32_t style, const char *value,
                          int32_t length, int32_t width)
{
    if (recording_draw) {
        assert(drawn_text_count < 32 && length >= 0 && length < 128);
        drawn_text[drawn_text_count].x = x;
        drawn_text[drawn_text_count].y = y;
        drawn_text[drawn_text_count].style = style;
        drawn_text[drawn_text_count].width = width;
        memcpy(drawn_text[drawn_text_count].value, value, (size_t)length);
        drawn_text[drawn_text_count++].value[length] = 0;
    }
    return ui_draw_text(x, y, style, value, length, width);
}
int32_t fixture_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t mode)
{
    if (recording_draw) {
        assert(drawn_rect_count < 8);
        drawn_rect[drawn_rect_count].x = x;
        drawn_rect[drawn_rect_count].y = y;
        drawn_rect[drawn_rect_count].width = width;
        drawn_rect[drawn_rect_count].height = height;
        drawn_rect[drawn_rect_count++].mode = mode;
    }
    return gfx_draw_rect(x, y, width, height, mode);
}
static void draw_recorded(void)
{
    drawn_text_count = drawn_rect_count = 0;
    recording_draw = true;
    standin_draw();
    recording_draw = false;
}
static void network_value(int row, const char *expected)
{
    int y = 16 + row * 22;
    for (int i = 0; i < drawn_text_count; i++)
        if (drawn_text[i].x == 184 && drawn_text[i].y == y) {
            if (strcmp(drawn_text[i].value, expected))
                fprintf(stderr, "Network row %d: got '%s', expected '%s'\n", row, drawn_text[i].value, expected);
            assert(!strcmp(drawn_text[i].value, expected));
            return;
        }
    assert(!"Network value was not drawn");
}

static const char *header_names[32], *header_values[32];
static standin_request_t answered_request;
static const standin_request_t *last_request(void)
{
    standin_t *host = standin();
    return host->request_count ? &host->requests[host->request_count - 1] : &answered_request;
}
#define sends (standin()->sends)
#define writes (standin()->store_changes)
#define last_tag (last_request()->tag)
#define last_method (last_request()->method)
#define last_url (last_request()->url)
#define last_headers (last_request()->headers)
#define last_body ((const char *)last_request()->body)
#define uptime_ms (standin()->uptime_ms)
#define session_size (standin()->session_length)
#define in_scene (standin()->is_session_writable)
#define fail_send (standin()->refuse_requests)
#define fail_write (standin()->refuse_writes)
#define setting standin_set
int32_t fixture_header(const char *name, int32_t length, int32_t occurrence, char *out, int32_t cap)
{
    for (int i = 0; i < 32; i++)
        if (header_names[i] && (int)strlen(header_names[i]) == length &&
            fixture_case_equal_n(header_names[i], name, (size_t)length)) {
            if (occurrence-- > 0)
                continue;
            int n = (int)strlen(header_values[i]);
            if (n > cap)
                return HOST_ERR_NO_SPACE;
            memcpy(out, header_values[i], (size_t)n);
            return n;
        }
    return HOST_ERR_NOT_FOUND;
}
static char *load(const char *name) { return fixture_text(name); }
static void no_headers(void)
{
    memset(header_names, 0, sizeof header_names);
    memset(header_values, 0, sizeof header_values);
}
static void answer(int status, const char *data, int chunk)
{
    answered_request = *last_request();
    standin()->request_count = 0;
    standin_data(answered_request.tag, -1, status, data, (int32_t)strlen(data), chunk, STANDIN_WHOLE, 0);
    assert(!standin()->refused_request[0] && !standin()->refused_what[0]);
    assert(!strstr(standin()->last_log, "secret") && !strstr(standin()->last_log, "session_id"));
    no_headers();
}
static void recorded_answer(const char *name, const char *body)
{
    char path[128];
    snprintf(path, sizeof path, "replay/%s.json", name);
    fixture_json_t mock = fixture_json(path);
    int headers = fixture_member(&mock, 0, "headers");
    char names[32][64], values[32][1024];
    no_headers();
    int count = 0;
    for (int key = headers + 1; key < mock.tokens[headers].next; key = mock.tokens[key + 1].next) {
        assert(count < 32);
        fixture_string(&mock, key, names[count], sizeof names[count]);
        /* The app reads only these singleton headers; duplicate Cache-Control is kept in the evidence. */
        if (!fixture_case_equal(names[count], "Set-Cookie") && !fixture_case_equal(names[count], "X-NDM-Realm") &&
            !fixture_case_equal(names[count], "X-NDM-Challenge")) continue;
        fixture_string(&mock, key + 1, values[count], sizeof values[count]);
        header_names[count] = names[count];
        header_values[count] = values[count];
        count++;
    }
    char filename[256];
    fixture_string(&mock, fixture_member(&mock, 0, "file"), filename, sizeof filename);
    const char *prefix = "../keenetic/fixtures/";
    assert(!strncmp(filename, prefix, strlen(prefix)));
    char *data = load(body ? body : filename + strlen(prefix));
    int status = atoi(mock.text + mock.tokens[fixture_member(&mock, 0, "status")].start);
    answer(status, data, 17);
    free(data);
    fixture_close(&mock);
}
static void reset(bool foreground)
{
    int64_t previous_uptime = uptime_ms;
    standin_reset();
    refused_poll = -1;
    uptime_ms = previous_uptime;
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_picked = app_on_picked;
    standin_handlers.on_draw = app_on_draw;
    drawn_text_count = drawn_rect_count = 0;
    in_scene = foreground;
    memset(&answered_request, 0, sizeof answered_request);
    no_headers();
    app_state_forget("session");
    setting("router", "http://192.0.2.1");
    setting("username", "admin");
    setting("password", "secret");
    setting("every", "1 min");
    app_on_load();
    if (foreground)
        app_on_focus();
    else {
        app_on_installed();
        app_on_alarm(REFRESH);
    }
}
static void challenge(void)
{
    recorded_answer("login-success-challenge", NULL);
    assert(last_method == HOST_HTTP_POST && !strcmp(last_url, "http://192.0.2.1/auth"));
    assert(standin_headers_ready(last_request()));
    char *expected = load("login-request.json");
    json_pick_t a, b;
    static const char *const keys[] = {"login", "password"};
    json_pick_init(&a, keys, 2);
    json_pick_init(&b, keys, 2);
    assert(json_pick_feed(&a, last_body, strlen(last_body)) && json_pick_finish(&a));
    assert(json_pick_feed(&b, expected, strlen(expected)) && json_pick_finish(&b));
    /* Picker scalars are 63 bytes: compare the full digest separately too. */
    char digest[65];
    keenetic_digest("admin", "Fixture router", "secret", "0123456789abcdef0123456789abcdef",
                    digest);
    assert(strstr(last_body, digest) && strstr(expected, digest));
    assert(!strcmp(json_pick_value(&a, 0), json_pick_value(&b, 0)));
    assert(strstr(last_headers, "Cookie: {{session}}") &&
           !strstr(last_headers, "Path="));
    free(expected);
}

static void picked_item(int poll, int item, int status, const char *data)
{
    if (status != 200) app_on_picked(poll, status);
    else standin_picked(poll, item, status, data, (int32_t)strlen(data));
    assert(!standin()->refused_request[0] && !standin()->refused_what[0]);
    no_headers();
}
static void picked(int poll, int status, const char *data) { picked_item(poll, -1, status, data); }
static void interface_fixture(void)
{
    if (scene.pending == INTERFACES + 1) {
        char *body = load("selected-wan.json"); picked(INTERFACES, 200, body); free(body);
    }
    const char *files[] = {"selected-carrier.json", "selected-radio0.json", "selected-radio1.json"};
    for (int i = 0; i < 3; i++) {
        char *body = load(files[i]); picked_item(LINKS, i, 200, body); free(body);
    }

}
static void poll_fixture(int poll, const char *file)
{
    char *data = load(file);
    picked(poll, 200, data);
    free(data);
}
static void logged_in(bool foreground)
{
    reset(foreground);
    challenge();
    recorded_answer("login-success", NULL);
    assert(!strcmp(cookie, "session_id=fixture-session"));
    assert(standin_poll_ready(BATCH, -1));
    assert(standin()->polls_run[BATCH] == 1);
}
static void protocol(void)
{
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(scene.status == 200 && scene.snapshot.has_data && scene.down == 9294);
    assert(scene.snapshot.reading.connections == 568 && scene.snapshot.reading.ram == 43);
    int before = standin()->polls_run[BATCH], requests = sends;
    app_on_button(BTN_OK_PRESSED); app_on_button(BTN_OK_PRESSED);
    assert(standin()->polls_run[BATCH] == before + 1 && sends == requests);
    poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.down == 9434 && scene.up == 162);
    app_on_button(BTN_OK_PRESSED);
    poll_fixture(BATCH, "v9-batch-command-error.json");
    assert(scene.status == 200 && scene.down == -1 && scene.up == -1 && scene.snapshot.reading.cpu >= 0);
    const char *expired[] = {"router/system-no-cookie.body.html", "router/system-wrong-cookie.body.html",
        "router/system-past-cookie-max-age.body.html", "router/system-clean-idle-expired.body.html"};
    for (unsigned i = 0; i < sizeof expired / sizeof *expired; i++) {
        logged_in(true);
        char *html = load(expired[i]);
        /* as the device hands it over: the HTML is not JSON, the status is lost, the router's
         * challenge header stays (recorded in router/system-no-cookie.http.json) */
        header_names[0] = "WWW-Authenticate";
        header_values[0] = "x-ndw2-interactive endpoint=\"/auth\" session_cookie=\"session_id\"";
        picked(BATCH, HOST_FAILED_NO_ANSWER, html); free(html);
        assert(last_method == HOST_HTTP_GET && strstr(last_url, "/auth") && !cookie[0]);
        challenge(); recorded_answer("login-renewed", NULL);
        poll_fixture(BATCH, "v9-batch-sample-a.json");
        assert(!control.blocked && scene.status == 200 && !control.renewing);
    }
    /* firmware 0.1.92 and later: the refusal keeps its status through the host's picking */
    logged_in(true);
    char *page = load("router/system-no-cookie.body.html");
    standin_picked(BATCH, -1, 401, page, (int32_t)strlen(page));
    assert(last_method == HOST_HTTP_GET && strstr(last_url, "/auth") && !cookie[0]);
    challenge(); recorded_answer("login-renewed", NULL);
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(!control.blocked && scene.status == 200 && !control.renewing);
    /* a success whose page is not JSON is still no answer, and no reason to log in */
    logged_in(true); requests = sends;
    standin_picked(BATCH, -1, 200, page, (int32_t)strlen(page)); free(page); no_headers();
    assert(sends == requests && scene.status == HOST_FAILED_NO_ANSWER && cookie[0]);
    /* an answer that is not JSON without the challenge is no reason to log in */
    logged_in(true); requests = sends;
    picked(BATCH, HOST_FAILED_NO_ANSWER, "<html>");
    assert(sends == requests && scene.status == HOST_FAILED_NO_ANSWER && cookie[0]);
    header_names[0] = "WWW-Authenticate"; header_values[0] = "x-ndw2-interactiveness";
    picked(BATCH, HOST_FAILED_NO_ANSWER, "<html>");
    assert(sends == requests && cookie[0]);
    picked(INTERFACES, 401, "");
    challenge(); recorded_answer("login-success", NULL);
    assert(scene.pending == INTERFACES + 1);
    picked(INTERFACES, 401, "");
    assert(control.blocked && !standin_alarm_left(REFRESH));
    requests = sends; app_on_alarm(REFRESH); assert(sends == requests);
    app_on_button(BTN_OK_PRESSED); challenge(); recorded_answer("login-refused", NULL);
    assert(control.blocked && sends == requests + 2);
    app_on_settings(); assert(!control.blocked);
    int stale = last_tag; app_on_settings(); requests = sends;
    app_on_data(stale, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 401);
    assert(sends == requests);
    puts("keenetic: login-only HTTP, poll coalescing, real counters, nested command error, four HTML 401s, retry target, renewal/refusal and settings epoch passed");
}
static void background(void)
{
    reset(false); challenge();
    app_on_load(); recorded_answer("login-success", NULL);
    app_on_load(); poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(scene.snapshot.has_data && standin_alarm_left(REFRESH) == 900);
    int before = writes;
    app_on_load(); app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(writes == before);
    app_on_load(); app_on_alarm(REFRESH); app_on_alarm(WATCHDOG);
    assert(scene.status == HOST_FAILED_TIMEOUT && !scene.pending && standin_alarm_left(REFRESH) >= 900);
    reset(true); fail_send = HOST_ERR_NO_SPACE; scene.pending = 0; start(true);
    assert(scene.status == HOST_FAILED_NO_MEMORY);
    reset(false); fail_write = HOST_ERR_NO_SPACE;
    recorded_answer("login-success-challenge", NULL);
    assert(scene.status == HOST_FAILED_NO_ANSWER);
    reset(false); int requests = sends;
    app_on_load(); picked(BATCH, 401, ""); app_on_load(); picked(INTERFACES, 401, "");
    assert(sends == requests && control.auth); /* One login across background loads. */
    challenge(); app_on_load(); recorded_answer("login-success", NULL);
    assert(!control.auth && scene.pending == BATCH + 1);
    puts("keenetic: unload between login/poll callbacks, cached session, background pace, no per-sample writes, watchdog and storage/queue refusal passed");
}
static void interface_settings(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    setting("wan", "Another0"); char url[512];
    assert(standin_poll_url(INTERFACES, -1, url, sizeof url) && strstr(url, "name=Another0"));
    scene.page = 2; scene.pending = 0; start(true); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.pending == LINKS + 1); interface_fixture();
    for (int i = 0; i < 4; i++) assert(!strcmp(facts[i], "-"));
    assert(!strcmp(facts[8], "-"));
    setting("wan", "PPPoE0"); scene.pending = 0; start(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    picked(INTERFACES, 200, "{\"address\":\"192.0.2.99\",\"speed\":\"100\",\"via\":\"Different0\"}");
    interface_fixture(); assert(!strcmp(facts[2], "100"));
    setting("wan", "bad\"name"); int requests = sends; app_on_settings();
    assert(!configured() && sends == requests && !standin_poll_ready(BATCH, -1));
    for (int poll = INTERFACES; poll < POLLS; poll++) {
        logged_in(true); picked(poll, 401, ""); challenge(); recorded_answer("login-success", NULL);
        assert(scene.pending == poll + 1);
        if (poll == HOSTS) poll_fixture(HOSTS, "router/hotspot.json");
        else interface_fixture();
        assert(scene.status == 0 && !control.blocked);
    }
    reset(false); int tag = last_tag;
    app_on_data(tag, (const uint8_t *)"{", 1, HOST_DATA_FIRST, 200);
    app_on_load(); app_on_alarm(WATCHDOG); int requests_after_timeout = sends;
    app_on_data(tag, NULL, 0, HOST_DATA_LAST, 401);
    assert(!control.auth && sends == requests_after_timeout && scene.status == HOST_FAILED_TIMEOUT);
    puts("keenetic: configurable fixed-path requests, WAN/carrier binding, unsafe settings, all detail-poll renewal targets and abandoned login passed");
}
static void router_rates(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(scene.down == 9294 && scene.up == 167 && scene.snapshot.reading.ram == 43);
    app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    assert(scene.down == 9294 && scene.up == 167);
    standin_forget_session(); app_on_load(); app_on_focus();
    assert(scene.down == 9294 && scene.up == 167);
    poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.down == 9434 && scene.up == 162);
    uint64_t n;
    assert(keenetic_uint("18446744073709551615", &n) && n == UINT64_MAX);
    assert(!keenetic_uint("18446744073709551616", &n));
    puts("keenetic: first-answer router bit/s rates, cached reopening and uint64 bounds passed");
}
static void stream_failures(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    int64_t good = scene.snapshot.reading.down;
    const char *bad[] = {"[", "[{]", "{}{}", "{\"a\":1,}", "{\"a\":01}", "{\"a\":trueX}", "{\"a\":\"\\q\"}"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        app_on_button(BTN_OK_PRESSED); picked(BATCH, 200, bad[i]);
        assert(scene.status != 200 && scene.snapshot.reading.down == good);
    }
    picked(BATCH, HOST_FAILED_NO_NETWORK, "");
    assert(scene.status == HOST_FAILED_NO_NETWORK && scene.down == good && standin_alarm_left(REFRESH) >= 120);
    puts("keenetic: OS picker rejects malformed/truncated responses; network failures retain good snapshot and back off");
}
static void hash_vectors(void)
{
    char *data = load("hash-vectors.json");
    for (int i = 0; i < 11; i++) {
        int sizes[] = {0, 1, 15, 31, 55, 56, 63, 64, 65, 127, 255};
        int n = sizes[i];
        char u[256], r[256], p[256], c[256], digest[65];
        memset(u, 'u', (size_t)n);
        memset(r, 'r', (size_t)n);
        memset(p, 'p', (size_t)n);
        memset(c, 'c', (size_t)n);
        u[n] = 0;
        r[n] = 0;
        p[n] = 0;
        c[n] = 0;
        keenetic_digest(u, r, p, c, digest);
        assert(strstr(data, digest));
    }
    free(data);
    puts("keenetic: 11 independent hashlib vectors across block boundaries passed");
}

static void input_boundaries(void)
{
    /* Exhaust interface identifiers before inserting one into a JSON template. */
    for (int c = 1; c <= 255; c++) {
        char name[2] = {(char)c, 0};
        bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        assert(safe_name(name) == allowed);
    }
    char name[33];
    memset(name, 'a', 31);
    name[31] = 0;
    assert(safe_name(name) && !safe_name(""));
    name[31] = 'a'; name[32] = 0;
    assert(!safe_name(name));
    const struct { int64_t value; int decimals; const char *expected; } numbers[] = {
        {0, 0, "0"}, {0, 2, "0.00"}, {1, 2, "0.01"}, {-1, 2, "-0.01"},
        {9403, 2, "94.03"}, {INT64_MAX, 0, "9223372036854775807"},
        {INT64_MIN, 2, "-92233720368547758.08"}
    };
    for (unsigned i = 0; i < sizeof numbers / sizeof *numbers; i++) {
        for (int cap = 1; cap <= 32; cap++) {
            char output[32], expected[32];
            snprintf(expected, (size_t)cap, "%s", numbers[i].expected);
            app_text_t text;
            app_text_start(&text, output, cap);
            keenetic_number(&text, numbers[i].value, numbers[i].decimals);
            assert(!strcmp(output, expected));
        }
    }
    puts("keenetic: WAN byte/length boundaries and int64 fixed formatting/truncation passed");
}

static void system_temperatures(void)
{
    static const struct { const char *first, *second; int64_t expected; } cases[] = {
        {"64", "63", 64}, {"63", "64", 64}, {"", "63", 63}, {"64", "", 64},
        {"0", "0", 0}, {"", "", -1}, {"null", "unknown", -1},
        {"18446744073709551615", "63", 63}, {"64", "-1", 64},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        app_text_copy(facts[6], sizeof facts[6], cases[i].first);
        app_text_copy(facts[7], sizeof facts[7], cases[i].second);
        assert(radio_temperature() == cases[i].expected);
    }
    puts("keenetic: higher radio temperature, missing/invalid readings and zero passed");
}

static void cold_history(void)
{
    int64_t saved_uptime = uptime_ms;
    uptime_ms = 10000;
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    snapshot_t legacy = scene.snapshot;
    legacy.version = 4;
    assert(app_state_save("snapshot", &legacy, sizeof legacy));
    standin_forget_session(); app_on_load(); app_on_focus();
    assert(scene.history.count == 1 && scene.history.down[0] == 9294);

    uptime_ms += 900000;
    app_on_blur(); in_scene = false; standin_forget_session(); app_on_load();
    app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-sample-b.json"); /* Background poll. */
    assert(scene.history.count == 3 && scene.history.down[1] == KEENETIC_GAP &&
           scene.history.down[2] == 9434);
    uptime_ms += 900000;
    app_on_load(); app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-1.json");
    assert(scene.history.count == 4 && scene.history.down[3] == 9770);
    uptime_ms += 60000;
    app_on_load(); picked(BATCH, HOST_FAILED_NO_ANSWER, "");
    assert(scene.history.count == 5 && scene.history.down[4] == KEENETIC_GAP);
    int saved = writes;
    app_on_load(); picked(BATCH, HOST_FAILED_NO_ANSWER, "");
    assert(scene.history.count == 5 && writes == saved);
    uptime_ms += 840000; /* Normal 15-minute spacing still splits at explicit failure. */
    app_on_load(); app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.history.count == 6 && scene.history.down[4] == KEENETIC_GAP &&
           scene.history.down[5] == 9434);
    uptime_ms += 1800000; /* A missed interval without a failure callback. */
    app_on_load(); app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-1.json");
    assert(scene.history.count == 8 && scene.history.down[6] == KEENETIC_GAP &&
           scene.history.down[7] == 9770);
    app_on_load(); in_scene = true; app_on_focus();
    assert(scene.history.count == 8 && scene.history.down[7] == 9770);
    standin_restart(); uptime_ms = 0;
    app_on_load(); in_scene = true; app_on_focus();
    assert(scene.history.count == 9 && scene.history.down[7] == 9770 &&
           scene.history.down[8] == KEENETIC_GAP);
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(scene.history.count == 10 && scene.history.down[9] == 9294);
    uptime_ms = saved_uptime;
    puts("keenetic: old snapshot, background point, cold open and reboot keep real points with gaps");
}
static void corrupt_history(void)
{
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-1.json");
    checkpoint_t good;
    int32_t length = kv_get("snapshot", 8, &good, sizeof good);
    assert(length > (int32_t)offsetof(checkpoint_t, samples));
    standin_forget_session();
    for (int i = 0; i < 7; i++) {
        checkpoint_t broken = good;
        int32_t bytes = length;
        if (i == 0) broken.snapshot.saved_at = INT64_MIN;
        if (i == 1) broken.snapshot.saved_at = time_uptime_ms() / 1000 + 1;
        if (i == 2 || i == 3) memset(broken.snapshot.reading.wan, 'w', sizeof broken.snapshot.reading.wan);
        if (i == 3) { broken.snapshot.version = 4; bytes = sizeof(snapshot_t); }
        if (i == 4) broken.flags = 4;
        if (i == 5) bytes = offsetof(checkpoint_t, samples); /* Baseline with zero records. */
        if (i == 6) broken.last_ms = time_uptime_ms() + 1;
        assert(kv_set("snapshot", 8, &broken, bytes) == HOST_OK);
        app_on_load();
        assert(!scene.snapshot.has_data && scene.history.count == 0 && !scene.history.baseline);
    }
}

static void graph_history(void)
{
    int64_t prior_uptime = uptime_ms;
    uptime_ms = 0;
    keenetic_history_t h = {0};
    keenetic_history_add(&h, -1, -1);
    assert(h.count == 0); /* No invented zero before a baseline. */
    keenetic_history_add(&h, 8653, 168);
    keenetic_history_add(&h, 9403, 171);
    keenetic_history_add(&h, -1, -1);
    keenetic_history_add(&h, -1, -1);
    keenetic_history_add(&h, 9403, 171);
    assert(h.count == 4 && h.down[2] == HOST_CHART_NO_VALUE && h.up[2] == HOST_CHART_NO_VALUE);
    for (int i = 0; i < 50; i++) keenetic_history_add(&h, i, i / 2);
    assert(h.count == 30 && h.down[0] == 20 && h.down[29] == 49);
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-1.json");
    assert(scene.history.count == 1 && scene.down == 9770);
    int before = writes;
    app_on_button(BTN_OK_PRESSED); poll_fixture(BATCH, "v9-batch-sample-a.json");
    assert(scene.history.count == 1 && scene.down == 9294 && writes == before);
    uptime_ms += 60000;
    app_on_button(BTN_OK_PRESSED); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.history.count == 2 && scene.history.down[1] == 9434 && writes == before);
    app_on_button(BTN_DOWN_PRESSED); poll_fixture(BATCH, "v9-batch-1.json"); interface_fixture();
    assert(scene.page == 1 && scene.history.count == 2); /* Page changes do not compress time. */
    app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    assert(scene.down >= 0 && scene.history.count == 2);
    uptime_ms += 900000; poll_fixture(BATCH, "v9-batch-sample-b.json"); interface_fixture();
    assert(scene.history.count == 4 && scene.history.down[2] == KEENETIC_GAP && scene.down == 9434);
    app_on_blur(); in_scene = false; standin_forget_session(); app_on_load();
    in_scene = true; app_on_focus();
    assert(scene.down == 9434 && scene.history.count == 4 && scene.history.down[3] == 9434);
    before = writes;
    app_on_draw();
    assert(writes == before); /* Opening and painting the saved graph writes nothing. */
    poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(scene.history.count == 4 && writes == before);
    uptime_ms = scene.snapshot.saved_at * 1000 + 899000;
    app_on_button(BTN_OK_PRESSED); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(writes == before);
    uptime_ms += 1000; app_on_button(BTN_OK_PRESSED); poll_fixture(BATCH, "v9-batch-sample-b.json");
    assert(writes == before + 1);
    setting("every", "5 min");
    app_on_settings(); assert(scene.history.count == 6); /* Same router and WAN. */
    setting("wan", "OtherWan");
    app_on_settings();
    assert(scene.history.count == 0 && scene.down == -1 && !scene.history.baseline);
    standin_forget_session(); app_on_load();
    assert(!scene.snapshot.has_data && scene.history.count == 0);
    uptime_ms = prior_uptime;
    puts("keenetic: 30-point history, time-spaced samples, gaps, restore and checkpoint bounds passed");
}

static void chart_frame(void) { traffic_chart(40, 180); }
static void chart_neighbours(void)
{
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    for (int state = 0; state < 4; state++) {
        if (state == 1) keenetic_history_add(&scene.history, -1, -1);
        if (state) keenetic_history_add(&scene.history, 9403, 171);
        chart_lines = drawn_text_count = drawn_rect_count = 0;
        recording_draw = recording_chart = true;
        standin_handlers.on_draw = chart_frame;
        standin_draw();
        standin_handlers.on_draw = app_on_draw;
        recording_draw = recording_chart = false;
        if (state < 2) {
            assert(chart_lines == 0); /* Single point, then two separated by a gap. */
            assert(drawn_rect_count == (state ? 4 : 2));
        } else assert(chart_lines > 0); /* Adjacent live points still fill/connect. */
    }
    puts("keenetic: isolated chart samples are dots; fills require valid neighbours passed");
}

static void refresh_phase(void)
{
    int64_t prior_uptime = uptime_ms;
    const int seconds[] = {60, 300, 900};
    const char *every[] = {"1 min", "5 min", "15 min"};
    const int buttons[] = {BTN_OK_PRESSED, BTN_DOWN_LONG_PRESSED}; /* a refresh, a screen switch */
    for (int rate = 0; rate < 3; rate++) for (int button = 0; button < 2; button++) {
        logged_in(true); setting("every", every[rate]); uptime_ms = 0;
        poll_fixture(BATCH, "v9-batch-sample-a.json");
        uptime_ms = (int64_t)seconds[rate] * 750;
        app_on_button(buttons[button]);
        poll_fixture(BATCH, "v9-batch-sample-b.json");
        if (scene.page) interface_fixture();
        assert(scene.history.count == 1);
        assert(standin_alarm_left(REFRESH) == seconds[rate]);
        uptime_ms = standin()->alarms[REFRESH].due_uptime_ms + 1000;
        assert(standin_alarm_due(REFRESH));
        app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-sample-b.json");
        if (scene.page) interface_fixture();
        assert(scene.history.count == 2 && scene.history.down[1] == 9434);
        uptime_ms += (int64_t)seconds[rate] * 2500 + 1;
        app_on_alarm(REFRESH); poll_fixture(BATCH, "v9-batch-sample-b.json");
        if (scene.page) interface_fixture();
        assert(scene.history.count == 4 && scene.history.down[2] == KEENETIC_GAP);
    }
    uptime_ms = prior_uptime;
    puts("keenetic: OK/page alarm postponement preserves samples at all paces; late reads gap passed");
}

static void optional_rates(void)
{
    for (int missing = 1; missing <= 3; missing++) {
        logged_in(true); scene.page = 2;
        char *body = load("v9-batch-sample-a.json");
        const char *keys[] = {"rxspeed", "txspeed"};
        for (int i = 0; i < 2; i++) if (missing & (1 << i)) {
            char *key = strstr(body, keys[i]); assert(key); key[0] = '_';
        }
        picked(BATCH, 200, body); free(body); interface_fixture();
        assert(scene.status == 200 && scene.snapshot.has_data && !scene.pending);
        assert(scene.down == (missing & 1 ? -1 : 9294));
        assert(scene.up == (missing & 2 ? -1 : 167));
        assert(scene.snapshot.reading.cpu == 53 && scene.snapshot.reading.ram == 43);
        assert(!strcmp(facts[4], "192.0.2.3") && !strcmp(facts[2], "1000"));
        scene.page = 0; header(); draw_recorded();
        assert(!strcmp(standin()->header_text[HOST_HEADER_LEFT], "1/4 Keenetic"));
        /* Rate labels use number(); absent values must never be rendered as zero. */
        char value[32]; number(value, sizeof value, scene.down, 2, "");
        assert(!strcmp(value, missing & 1 ? "-" : "92.94"));
        number(value, sizeof value, scene.up, 2, "");
        assert(!strcmp(value, missing & 2 ? "-" : "1.67"));
    }
    puts("keenetic: either/both missing rates preserve header, system and Network; show dash passed");
}

static void bounds(void)
{
    char accepted[256], body[513], digest[65];
    keenetic_digest("admin", "Fixture router", "secret", "0123456789abcdef0123456789abcdef", digest);
    assert(keenetic_cookie("session_id=fake; Path=/; HttpOnly", accepted) && !strcmp(accepted, "session_id=fake"));
    assert(!keenetic_cookie("id=bad\r\nHeader:x", accepted) && !keenetic_cookie("id={{password}}", accepted));
    char long_cookie[300]; memset(long_cookie, 'a', sizeof long_cookie); long_cookie[1] = '='; long_cookie[255] = 0;
    assert(keenetic_cookie(long_cookie, accepted)); long_cookie[255] = 'a'; long_cookie[256] = 0;
    assert(!keenetic_cookie(long_cookie, accepted));
    assert(keenetic_login_body("a\"b\\c", digest, body) && strstr(body, "a\\\"b\\\\c"));
    char user[256]; memset(user, '"', 255); user[255] = 0; assert(!keenetic_login_body(user, digest, body));
    reset(true); char realm[300]; memset(realm, 'r', 299); realm[299] = 0;
    header_names[0] = "X-NDM-Realm"; header_values[0] = realm;
    int before = sends; answer(401, "", 1); assert(sends == before && scene.status == HOST_FAILED_NO_ANSWER);
    reset(true); header_names[0] = "X-NDM-Realm"; header_values[0] = "{{password}}";
    answer(401, "", 1); assert(scene.status == HOST_FAILED_NO_ANSWER);
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    before = writes;
    header_names[0] = "set-cookie"; header_values[0] = "session_id=rotated; Path=/; Max-Age=300";
    poll_fixture(BATCH, "v9-batch-sample-b.json"); assert(!strcmp(cookie, "session_id=rotated"));
    int changed = writes; assert(changed == before + 1);
    header_names[0] = "Set-Cookie"; header_values[0] = "session_id=rotated; Path=/; Max-Age=300";
    poll_fixture(BATCH, "v9-batch-sample-b.json"); assert(writes == changed);
    app_on_blur(); in_scene = false; app_on_load(); app_on_alarm(REFRESH);
    assert(!strcmp(cookie, "session_id=rotated") && standin_poll_ready(BATCH, -1));
    header_names[0] = "Set-Cookie"; header_values[0] = "id=one";
    header_names[1] = "Set-Cookie"; header_values[1] = "id=two";
    poll_fixture(BATCH, "v9-batch-sample-a.json"); assert(scene.status == HOST_FAILED_NO_ANSWER);
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    char longest[256]; memset(longest, 'a', 255); longest[255] = 0;
    char router[256]; memset(router, 'a', 255); router[255] = 0;
    memcpy(router, "http://", 7); router[70] = router[134] = router[198] = '.';
    setting("router", router); setting("username", longest); setting("password", longest);
    longest[31] = 0; setting("wan", longest);
    char links[96]; int written = snprintf(links, sizeof links, "%s\n%s\n%s", longest, longest, longest);
    assert(written > 0 && written < (int)sizeof links); setting("links", links);
    longest[31] = 'a';
    setting("every", "15 min");
    longest[1] = '='; app_text_copy(cookie, sizeof cookie, longest); assert(kv_set("session", 7, cookie, (int32_t)strlen(cookie)) == HOST_OK);
    scene.history_source = settings_identity(3);
    for (int i = 0; i < KEENETIC_SAMPLES; i++) keenetic_history_add(&scene.history, 10000 + i, i);
    save_checkpoint();
    assert(!snapshot_dirty && scene.history.count == KEENETIC_SAMPLES);
    scene.epoch = (int32_t)settings_identity(5);
    assert(app_session_save(&scene, sizeof scene));
    app_on_load(); app_on_focus();
    assert(scene.history.count == KEENETIC_SAMPLES); /* Same revision, fuller in-stack copy. */
    standin_forget_session(); app_on_load();
    assert(scene.history.count > 0 && scene.history.count < KEENETIC_SAMPLES);
    assert(scene.history.down[scene.history.count - 1] == 10000 + KEENETIC_SAMPLES - 1);
    assert(standin_store_bytes() <= HOST_KV_TOTAL_MAX);
    printf("keenetic: maximum valid settings + 255-byte session + control + snapshot: %d/%d store bytes\n", standin_store_bytes(), HOST_KV_TOTAL_MAX);
    printf("keenetic: scene %zu bytes, reading %zu, snapshot %zu\n", sizeof scene, sizeof(keenetic_reading_t), sizeof(snapshot_t));
    puts("keenetic: cookie/header/body bounds, JSON escaping, header injection, duplicate cookies, rotation and write suppression passed");
}
static void four_pages(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-1.json");
    int before = writes;
    app_on_button(BTN_DOWN_PRESSED); poll_fixture(BATCH, "v9-batch-1.json"); interface_fixture();
    assert(scene.page == 1 && !strcmp(facts[6], "64") && !strcmp(facts[7], "63"));
    app_on_button(BTN_DOWN_PRESSED); poll_fixture(BATCH, "v9-batch-1.json"); interface_fixture();
    assert(!strcmp(facts[0], "192.0.2.62") && !strcmp(facts[1], "23293") && !strcmp(facts[2], "1000") &&
           !strcmp(facts[8], "192.0.2.63") && !strcmp(facts[4], "192.0.2.3") && !strcmp(facts[5], "192.0.2.4"));
    app_on_button(BTN_DOWN_PRESSED); poll_fixture(BATCH, "v9-batch-1.json"); poll_fixture(HOSTS, "router/hotspot.json");
    assert(hosts.count == 32 && hosts.total == 55 && writes == before);
    standin_draw(); int first = device_first;
    app_on_button(BTN_DOWN_PRESSED); assert(scene.page == 3 && device_first == first + 1);
    app_on_button(BTN_UP_PRESSED); assert(device_first == first);
    /* short presses stop at the list's ends; held ones switch screens */
    app_on_button(BTN_UP_PRESSED); assert(scene.page == 3 && device_first == first);
    app_on_button(BTN_UP_LONG_PRESSED); assert(scene.page == 2);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 3 && device_first == 0);
    device_first = hosts.count - shown_rows; app_on_button(BTN_DOWN_PRESSED);
    assert(scene.page == 3 && device_first == hosts.count - shown_rows);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 0);
    app_on_button(BTN_UP_PRESSED); assert(scene.page == 3 && device_first == 0);
    app_on_button(BTN_DOWN_PRESSED); app_on_button(BTN_DOWN_PRESSED);
    int saved_first = device_first;
    app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    assert(scene.page == 3 && device_first == saved_first && saved_first == 2 && hosts.rows[saved_first].ip[0]);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 0);
    scene.pending = 0; int count = standin()->polls_run[HOSTS]; start(true); poll_fixture(BATCH, "v9-batch-1.json");
    assert(!scene.pending && standin()->polls_run[HOSTS] == count);
    app_on_button(BTN_DOWN_PRESSED); poll_fixture(BATCH, "v9-batch-1.json");
    app_on_button(BTN_DOWN_PRESSED); interface_fixture();
    assert(scene.pending == INTERFACES + 1); interface_fixture();
    assert(scene.page == 2 && !scene.pending && !strcmp(facts[2], "1000"));
    picked(INTERFACES, 200, "{\"address\":\"192.0.2.99\",\"uptime\":12,\"remote\":\"192.0.2.7\"}");
    interface_fixture();
    assert(!strcmp(facts[0], "192.0.2.99") && !strcmp(facts[1], "12") &&
           !strcmp(facts[8], "192.0.2.7") && !strcmp(facts[4], "192.0.2.3"));
    picked(INTERFACES, 200, "{\"remote\":\"0.0.0.0\"}");
    assert(!strcmp(facts[0], "-") && !strcmp(facts[1], "-") && !facts[2][0] && !strcmp(facts[8], "192.0.2.56"));
    picked(INTERFACES, 200, "{}"); assert(!strcmp(facts[8], "192.0.2.56"));
    puts("keenetic: four pages, fixed fixture picks, optional DNS/uptime, list scrolling, viewport restore, page changes and page-only detail requests passed");
}
static void list_navigation(void)
{
    logged_in(true); scene.page = 3;
    poll_fixture(HOSTS, "router/v9-get-08.json");
    device_first = 0; standin_draw();
    assert(shown_rows == 3 && hosts.count == 32);
    int before = writes;
    int last = hosts.count - shown_rows;
    /* short presses walk the rows and stop at both ends */
    app_on_button(BTN_UP_PRESSED); assert(scene.page == 3 && device_first == 0);
    for (int row = 1; row <= last; row++) {
        app_on_button(BTN_DOWN_PRESSED);
        assert(scene.page == 3 && device_first == row);
    }
    app_on_button(BTN_DOWN_PRESSED); assert(scene.page == 3 && device_first == last);
    for (int row = last - 1; row >= 0; row--) {
        app_on_button(BTN_UP_PRESSED);
        assert(scene.page == 3 && device_first == row);
    }
    app_on_button(BTN_UP_PRESSED); assert(scene.page == 3 && device_first == 0 && writes == before);
    /* held presses switch screens from anywhere in the list */
    device_first = 5;
    app_on_button(BTN_UP_LONG_PRESSED); assert(scene.page == 2);
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 3 && device_first == 0);
    device_first = last;
    app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 0);
    /* a restored middle viewport keeps its place and still steps */
    scene.page = 3; device_first = 5;
    app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    assert(device_first == 5 && hosts.rows[5].ip[0] && !hosts.rows[4].ip[0]);
    app_on_button(BTN_UP_PRESSED); assert(scene.page == 3 && device_first == 4);
    standin_draw();
    /* empty and single-viewport lists: short presses stay, held ones switch */
    for (int count = 0; count <= shown_rows; count++) {
        hosts.count = count; scene.page = 3; device_first = 0;
        app_on_button(BTN_UP_PRESSED); app_on_button(BTN_DOWN_PRESSED);
        assert(scene.page == 3 && device_first == 0);
        app_on_button(BTN_UP_LONG_PRESSED); assert(scene.page == 2);
        scene.page = 3;
        app_on_button(BTN_DOWN_LONG_PRESSED); assert(scene.page == 0);
    }
    puts("keenetic: short row steps clamped at the ends, held screen switches and empty/short lists passed");
}
static void host_lists(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-1.json"); poll_fixture(HOSTS, "router/hotspot.json");
    assert(hosts.count == 32 && hosts.total == 55 && !strcmp(hosts.rows[0].ip, "192.0.2.1"));
    const char *sample = "{\"host\":[{\"active\":false,\"ip\":\"192.0.2.1\",\"name\":\"Ignore\",\"hostname\":\"Ignore\"},"
        "{\"active\":true,\"ip\":\"192.0.2.2\",\"name\":\"\",\"hostname\":\"Fallback\"},"
        "{\"active\":true,\"ip\":\"192.0.2.3\",\"name\":null,\"hostname\":null}]}";
    picked(HOSTS, 200, sample);
    assert(hosts.count == 2 && !strcmp(hosts.rows[0].name, "Fallback") && !strcmp(hosts.rows[1].name, app_string(STR_UNKNOWN)));
    size_t capacity = HOST_ANSWER_MAX; char *many = malloc(capacity); assert(many);
    size_t at = (size_t)snprintf(many, capacity, "{\"host\":[");
    for (int i = 0; i < 2000; i++) at += (size_t)snprintf(many + at, capacity - at,
        "%s{\"active\":true,\"name\":\"Device %04d\",\"hostname\":\"host\",\"ip\":\"192.0.2.1\"}", i ? "," : "", i);
    at += (size_t)snprintf(many + at, capacity - at, "]}"); assert(at < capacity);
    picked(HOSTS, 200, many); free(many);
    assert(scene.status == 200 && hosts.count == 32 && hosts.total > 32 && hosts.total < 2000);
    printf("keenetic: 2000-record response: %zu bytes, first %d of %d retained active rows\n", at, hosts.count, hosts.total);
    assert(!strcmp(hosts.rows[0].name, "Device 0000") && !strcmp(hosts.rows[31].name, "Device 0031"));
    picked(HOSTS, 200, "{\"host\":[]}"); assert(!hosts.count && !hosts.total);
    char unicode[512] = "{\"host\":[{\"active\":true,\"ip\":\"192.0.2.1\",\"name\":\"";
    for (int i = 0; i < 100; i++) strcat(unicode, "Я");
    strcat(unicode, "\",\"hostname\":\"\"}]}"); picked(HOSTS, 200, unicode);
    assert(hosts.count == 1 && strlen(hosts.rows[0].name) == 62);
    strcpy(unicode, "{\"host\":[{\"active\":true,\"ip\":\"192.0.2.1\",\"name\":\"");
    for (int i = 0; i < 100; i++) strcat(unicode, "猫");
    strcat(unicode, "\",\"hostname\":\"\"}]}"); picked(HOSTS, 200, unicode);
    assert(hosts.count == 1 && strlen(hosts.rows[0].name) == 63);
    assert(!strcmp(hosts.rows[0].name + 60, "猫"));
    printf("keenetic: overflow keeps first %d active rows; retained total is bounded by %d list bytes\n", KEENETIC_HOSTS_MAX, HOST_LIST_BYTES);
    puts("keenetic: captured 102/55 devices, null/empty name fallback, first32, 2000 records within answer limit, beyond OS list buffer, empty lists and UTF-8 truncation passed");
}
static void live_v9(void)
{
    logged_in(true); scene.page = 2;
    poll_fixture(BATCH, "v9-batch-1.json");
    poll_fixture(INTERFACES, "router/v9-get-01.json");
    const char *files[] = {"router/v9-get-05.json", "router/v9-get-06.json", "router/v9-get-07.json"};
    for (int i = 0; i < 3; i++) {
        char *body = load(files[i]); picked_item(LINKS, i, 200, body); free(body);
    }
    assert(!scene.pending && scene.status == 200);
    assert(!strcmp(facts[0], "192.0.2.1") && !strcmp(facts[8], "192.0.2.2"));
    assert(!strcmp(facts[4], "192.0.2.3") && !strcmp(facts[5], "192.0.2.4"));
    assert(!strcmp(facts[1], "46879") && !strcmp(facts[2], "1000"));
    assert(!strcmp(facts[3], "GigabitEthernet1") && radio_temperature() == 61);
    picked(INTERFACES, 200, "{\"remote\":null}"); assert(!strcmp(facts[8], "192.0.2.56"));
    picked(INTERFACES, 200, "{}"); assert(!strcmp(facts[8], "192.0.2.56"));
    read_fact(0, 8); assert(!strcmp(facts[8], "-")); /* Missing picks clear old metadata. */
    poll_fixture(BATCH, "v9-batch-dns-unspecified.json");
    assert(!strcmp(facts[4], "-") && !strcmp(facts[5], "-"));
    poll_fixture(BATCH, "v9-batch-1.json"); assert(facts[4][0] && facts[5][0]);
    poll_fixture(BATCH, "v9-batch-dns-missing.json");
    assert(!strcmp(facts[4], "-") && !strcmp(facts[5], "-"));
    scene.pending = 0; scene.page = 3;
    poll_fixture(HOSTS, "router/v9-get-08.json");
    assert(hosts.count == 32 && hosts.total == 56);
    assert(!strcmp(hosts.rows[0].ip, "203.0.113.46"));
    assert(!strcmp(hosts.rows[0].name, app_string(STR_UNKNOWN)));
    picked(HOSTS, 200, "{\"host\":[{\"active\":true,\"ip\":\"203.0.113.46\"}]}");
    assert(hosts.count == 1 && !strcmp(hosts.rows[0].name, app_string(STR_UNKNOWN)));
    puts("keenetic: V9 live PPP peer, active DNS, carrier/radios, absent peer and unnamed device regressions passed");
}
static void network_loading(void)
{
    logged_in(true); scene.page = 2;
    draw_recorded(); assert(!drawn_text_count); /* The kit owns the cold-start notice. */
    poll_fixture(BATCH, "v9-batch-dns-missing.json");
    draw_recorded();
    network_value(0, "PPPoE0"); network_value(1, word(STR_REACHABLE));
    for (int row = 2; row < 9; row++)
        network_value(row, row == 4 || row == 5 ? "-" : word(STR_LOADING));
    picked(INTERFACES, 200, "{\"address\":\"192.0.2.99\",\"uptime\":12,"
           "\"remote\":\"192.0.2.7\",\"via\":\"GigabitEthernet1\"}");
    draw_recorded();
    network_value(2, "192.0.2.99"); network_value(3, "192.0.2.7");
    network_value(6, "0h"); network_value(7, word(STR_LOADING)); network_value(8, "GigabitEthernet1");
    picked_item(LINKS, 0, 200, "{\"port\":{\"speed\":1000}}");
    draw_recorded(); network_value(7, "1000");
    interface_fixture();
    app_on_button(BTN_OK_PRESSED);
    draw_recorded(); network_value(4, "-"); network_value(7, "1000"); /* A refresh keeps known data. */
    poll_fixture(BATCH, "v9-batch-dns-unspecified.json");
    picked(INTERFACES, 200, "{\"address\":null,\"remote\":\"0.0.0.0\"}");
    draw_recorded();
    network_value(2, "-"); network_value(3, "192.0.2.56"); network_value(6, "-");
    network_value(7, word(STR_LOADING)); network_value(8, "-");
    picked_item(LINKS, 0, 200, "{}");
    draw_recorded(); network_value(7, "-");
    /* Known missing rows survive session restoration and another pending refresh. */
    app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    draw_recorded(); network_value(2, "-"); network_value(4, "-"); network_value(7, "-");
    picked(INTERFACES, HOST_FAILED_NO_NETWORK, "");
    draw_recorded(); network_value(2, "-");
    /* A cached system snapshot alone has no arrived Network detail rows. */
    standin_forget_session(); app_on_load(); app_on_focus(); scene.page = 2;
    draw_recorded();
    for (int row = 2; row < 9; row++) network_value(row, word(STR_LOADING));
    picked(BATCH, HOST_FAILED_NO_NETWORK, "");
    draw_recorded(); network_value(2, word(STR_LOADING));
    /* Leaving Devices clears the overlaid detail buffer back to pending fields. */
    scene.page = 3; app_on_button(BTN_UP_LONG_PRESSED);
    draw_recorded();
    for (int row = 2; row < 9; row++) network_value(row, word(STR_LOADING));
    app_on_settings();
    for (int field = 0; field < 9; field++) assert(!facts[field][0]);
    puts("keenetic: Network pending/partial rows, completed absent/null/unspecified values, carrier speed, refresh and restored missing values passed");
}
static void device_scrollbar(int thumb_y, int thumb_height)
{
    assert(drawn_rect_count == 2);
    assert(drawn_rect[0].x == 393 && drawn_rect[0].y == 16);
    assert(drawn_rect[0].width == 1 && drawn_rect[0].height == 202);
    assert(drawn_rect[1].x == 391 && drawn_rect[1].y == thumb_y);
    assert(drawn_rect[1].width == 5 && drawn_rect[1].height == thumb_height);
    assert(drawn_rect[0].mode == HOST_RECT_FILLED && drawn_rect[1].mode == HOST_RECT_FILLED);
}
static void device_two_lines(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-1.json"); scene.page = 3;
    poll_fixture(HOSTS, "v11-long-hosts.json"); device_first = 0;
    draw_recorded(); assert(hosts.count == 5 && drawn_text_count == 7);
    for (int i = 0; i < shown_rows; i++) {
        int name = 2 * i, ip = name + 1;
        assert(!strcmp(drawn_text[name].value, hosts.rows[i].name));
        assert(!strcmp(drawn_text[ip].value, hosts.rows[i].ip));
        assert(drawn_text[name].style == HOST_UI_TEXT && drawn_text[ip].style == HOST_UI_CAPTION);
        assert(drawn_text[name].x == 16 && drawn_text[ip].x == 16);
        assert(drawn_text[name].y == 16 + i * 68 && drawn_text[ip].y == 44 + i * 68);
        assert(drawn_text[name].width == 356 && drawn_text[ip].width == 356);
    }
    device_scrollbar(16, 121);
    app_on_button(BTN_DOWN_PRESSED); app_on_button(BTN_DOWN_PRESSED); draw_recorded();
    assert(device_first == 2); device_scrollbar(97, 121);
    /* A shrinking list clamps to its start and gives the scrollbar's space back. */
    hosts.count = 1; draw_recorded(); assert(drawn_text_count == 3 && !drawn_rect_count && !device_first);
    assert(drawn_text[0].width == 368 && !strcmp(drawn_text[1].value, "198.51.100.255"));
    /* Uncached rows keep the complete localized loading label and no address. */
    hosts.rows[0].ip[0] = 0; draw_recorded();
    assert(!strcmp(drawn_text[0].value, word(STR_LOADING)) && !drawn_text[1].value[0]);
    poll_fixture(HOSTS, "router/v9-get-08.json"); device_first = 0; draw_recorded();
    assert(!strcmp(drawn_text[0].value, word(STR_UNKNOWN)));
    device_scrollbar(16, 18);
    device_first = 29; app_on_blur(); in_scene = false; app_on_load(); in_scene = true; app_on_focus();
    assert(device_first == 29 && hosts.rows[31].ip[0] && !hosts.rows[28].ip[0]);
    draw_recorded(); assert(drawn_text_count == 7); device_scrollbar(200, 18);
    puts("keenetic: three two-line devices, full-width kit body names, caption IPs, unknown/pending names and final viewport restore passed");
}
/* app_delivery.c gives VALUES synchronously, clears the answer, then lets the
 * window manager draw. There must be no value-reader access during that draw. */
static void network_delivery_values(bool carrier_pending)
{
    draw_recorded();
    const char *expected[] = {"PPPoE0", word(STR_REACHABLE), "192.0.2.1", "192.0.2.2",
                              "192.0.2.3", "192.0.2.4", "13h",
                              carrier_pending ? word(STR_LOADING) : "1000", "GigabitEthernet1"};
    for (int row = 0; row < 9; row++) network_value(row, expected[row]);
}
static void network_reload(bool foreground)
{
    /* A new wasm instance also reinitializes globals not touched by on_load. */
    detail_page = 0;
    device_first = 0;
    app_on_load();
    in_scene = foreground;
    if (foreground) app_on_focus();
}
static void network_delivery_order(void)
{
    const char *links[] = {"router/v9-get-05.json", "router/v9-get-06.json", "router/v9-get-07.json"};
    logged_in(true);
    poll_fixture(BATCH, "v9-batch-1.json");
    app_on_button(BTN_DOWN_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    assert(scene.page == 2);
    /* The simulator queues each-items in settings order: 0, 1, 2. Replay
     * seventeen completed rounds, matching the reported successful polls. */
    for (int round = 0; round < 17; round++) {
        poll_fixture(BATCH, "v9-batch-1.json");
        poll_fixture(INTERFACES, "router/v9-get-01.json");
        for (int item = 0; item < 3; item++) {
            char *body = load(links[item]);
            picked_item(LINKS, item, 200, body);
            free(body);
        }
        assert(scene.pending == 0 && scene.status == 200);
        network_delivery_values(false);
        uptime_ms += 60000;
        app_on_alarm(REFRESH);
    }
    /* Resume a separate instance between every pair of answers. */
    for (int poll = BATCH; poll <= LINKS; poll++) {
        app_on_blur(); in_scene = false;
        network_reload(true);
        if (poll == BATCH) poll_fixture(poll, "v9-batch-1.json");
        else if (poll == INTERFACES) poll_fixture(poll, "router/v9-get-01.json");
        else for (int item = 0; item < 3; item++) {
            app_on_blur(); in_scene = false;
            network_reload(true);
            char *body = load(links[item]);
            picked_item(poll, item, 200, body);
            free(body);
        }
        network_delivery_values(poll == INTERFACES);
    }
    /* Off-screen VALUES get distinct instances and cannot write the saved
     * session. On return, keep the known viewport and finish a fresh round. */
    app_on_blur(); in_scene = false;
    network_reload(false); poll_fixture(BATCH, "v9-batch-1.json");
    network_reload(false); poll_fixture(INTERFACES, "router/v9-get-01.json");
    for (int item = 0; item < 3; item++) {
        network_reload(false);
        char *body = load(links[item]);
        picked_item(LINKS, item, 200, body);
        free(body);
    }
    network_reload(true);
    network_delivery_values(false);
    poll_fixture(BATCH, "v9-batch-1.json");
    poll_fixture(INTERFACES, "router/v9-get-01.json");
    for (int item = 0; item < 3; item++) {
        char *body = load(links[item]); picked_item(LINKS, item, 200, body); free(body);
    }
    network_delivery_values(false);
    puts("keenetic: saved router picks then redraw, 17 rounds, reloads between polls and each-items, off-screen deliveries and return passed");
}
static void restart_login(void)
{
    for (int renewed = 0; renewed < 2; renewed++) {
        for (int phase = 0; phase < 2; phase++) {
            reset(true);
            if (renewed) {
                challenge(); recorded_answer("login-success", NULL);
                picked(BATCH, 401, "");
            }
            if (phase) challenge();
            int stale = last_tag;
            standin_restart(); in_scene = false; app_on_load();
            app_on_intent(HOST_INTENT_STARTED);
            assert(control.auth && standin_alarm_left(WATCHDOG) == 1);
            app_on_alarm(WATCHDOG);
            assert(!control.auth && !control.renewing && !scene.pending && !control.blocked);
            assert(standin_alarm_left(REFRESH) == 900);
            app_on_alarm(REFRESH);
            if (!control.auth) picked(BATCH, 401, "");
            int tag = control.auth;
            app_on_data(stale, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
            assert(control.auth == tag);
            challenge(); recorded_answer("login-success", NULL);
            poll_fixture(BATCH, "v9-batch-sample-a.json");
            assert(scene.snapshot.has_data && !control.blocked);
        }
    }
    reset(false); app_on_load(); app_on_installed();
    assert(standin_alarm_left(WATCHDOG) == 1);
    reset(false); app_on_load(); app_on_updated();
    assert(standin_alarm_left(WATCHDOG) > 0);
    reset(true); setting("password", ""); app_on_settings(); app_on_blur();
    assert(!standin_alarm_left(REFRESH));
    puts("keenetic: restart in both login phases, renewal, reinstall/update and unconfigured sleep passed");
}
static void detail_failures(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    refused_poll = HOSTS; request_poll(HOSTS); refused_poll = -1;
    assert(scene.status == 200 && scene.down == 9294 && !strcmp(standin()->header_text[0], "1/4 Keenetic"));
    for (int write_failure = 0; write_failure < 2; write_failure++) {
        logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
        if (write_failure) fail_write = HOST_ERR_NO_SPACE;
        else fail_send = HOST_ERR_NO_SPACE;
        picked(HOSTS, 401, "");
        assert(scene.status != 200 && !strncmp(standin()->header_text[0], "1/4 ", 4) && !strcmp(standin()->header_text[0] + 4, word(STR_NO_CONTACT)));
    }
    const int failures[] = {599, HOST_FAILED_TOO_LARGE, HOST_FAILED_TIMEOUT, HOST_FAILED_NO_NETWORK};
    for (int poll = INTERFACES; poll < POLLS; poll++) {
        for (unsigned i = 0; i < sizeof failures / sizeof *failures; i++) {
            logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
            int64_t down = scene.down, up = scene.up;
            int samples = scene.history.count;
            app_on_button(BTN_OK_PRESSED); /* Unsolicited detail while batch is pending. */
            picked(poll, failures[i], "");
            assert(scene.status == 200 && scene.down == down && scene.up == up);
            assert(scene.history.count == samples && !strcmp(standin()->header_text[0], "1/4 Keenetic"));
            picked(BATCH, HOST_FAILED_NO_NETWORK, "");
            picked(poll, 200, "{}");
            assert(!strncmp(standin()->header_text[0], "1/4 ", 4) && !strcmp(standin()->header_text[0] + 4, word(STR_NO_CONTACT)));
            poll_fixture(BATCH, "batch-offline.json"); picked(poll, failures[i], "");
            assert(!strncmp(standin()->header_text[0], "1/4 ", 4) && !strcmp(standin()->header_text[0] + 4, word(STR_NO_INTERNET)));
        }
        logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
        picked(poll, 403, "");
        assert(control.blocked && !strncmp(standin()->header_text[0], "1/4 ", 4) && !strcmp(standin()->header_text[0] + 4, word(STR_DENIED)));
    }
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    scene.page = 3; poll_fixture(HOSTS, "router/hotspot.json");
    picked(HOSTS, 599, ""); draw_recorded();
    assert(hosts.count == 32 && !strcmp(drawn_text[drawn_text_count - 1].value, word(STR_NO_CONTACT)));
    picked(LINKS, 200, "{}");
    assert(detail_status == 599); /* An unrelated page cannot clear the warning. */
    picked(HOSTS, 200, "{\"host\":[]}");
    picked(HOSTS, 599, ""); draw_recorded();
    assert(!strcmp(drawn_text[0].value, word(STR_NO_CONTACT)));
    puts("keenetic: unsolicited detail failures preserve header/rates/history, blocked precedence and stale Devices warning passed");
}
static void long_host_cut(void)
{
    logged_in(true); scene.page = 3;
    char *data = load("long-hosts-130.json"); picked(HOSTS, 200, data);
    assert(hosts.count == 32 && hosts.total > 32 && hosts.total < 130);
    for (int i = 0; i < 32; i++) {
        char expected[40]; snprintf(expected, sizeof expected, "198.51.100.%d", i + 1);
        assert(!strcmp(hosts.rows[i].ip, expected) && atoi(hosts.rows[i].name) == i);
    }
    /* Bring the retained rows near the cut into the visible first-32 set. */
    char *at = data;
    for (int i = 0; i < 80; i++) {
        at = strstr(at, "true"); assert(at); memcpy(at, "null", 4); at += 4;
    }
    picked(HOSTS, 200, data); free(data);
    for (int i = 0; i < hosts.count; i++) {
        int address; assert(sscanf(hosts.rows[i].ip, "198.51.100.%d", &address) == 1);
        assert(address >= 81 && atoi(hosts.rows[i].name) == address - 1);
    }
    assert(hosts.count > 0);
    puts("keenetic: 130 long Cyrillic names beyond 8 KiB retain names and boundary-row identity passed");
}
static void network_variants(void)
{
    logged_in(true); scene.page = 2;
    poll_fixture(BATCH, "v9-batch-1.json"); interface_fixture();
    setting("wan", "Other0");
    app_on_button(BTN_OK_PRESSED); poll_fixture(BATCH, "v9-batch-1.json");
    assert(!strcmp(facts[4], "192.0.2.3") && !strcmp(facts[5], "192.0.2.4"));
    char *batch = load("v9-batch-1.json");
    char *gateway = strstr(batch, "\"gateway\""); assert(gateway); memcpy(gateway + 1, "missing", 7);
    poll_fixture(BATCH, "v9-batch-1.json");
    picked(BATCH, 200, batch); free(batch); draw_recorded();
    network_value(0, "-");
    for (int count = 1; count <= 3; count++) {
        logged_in(true); setting("wan", "GigabitEthernet1");
        setting("links", count == 1 ? "GigabitEthernet1" : count == 2 ?
                "GigabitEthernet1\nWifiMaster0" : "GigabitEthernet1\nWifiMaster0\nWifiMaster1");
        assert(configured()); scene.page = 2;
        batch = load("v9-batch-1.json");
        char *wan = strstr(batch, "PPPoE0"); assert(wan);
        char ipoe[8192]; size_t prefix = (size_t)(wan - batch);
        snprintf(ipoe, sizeof ipoe, "%.*sGigabitEthernet1%s", (int)prefix, batch, wan + 6); free(batch);
        picked(BATCH, 200, ipoe);
        picked(INTERFACES, 200, "{\"address\":\"192.0.2.90\",\"uptime\":1209600}");
        picked_item(LINKS, 0, 200, "{\"port\":{\"speed\":1000}}");
        for (int item = 1; item < count; item++) picked_item(LINKS, item, 200, "{\"temperature\":60}");
        assert(!scene.pending && !standin_alarm_left(WATCHDOG));
        assert(!strcmp(facts[2], "1000") && !strcmp(facts[8], "192.0.2.56"));
        draw_recorded(); network_value(6, "336h");
    }
    setting("links", ""); assert(!configured());
    setting("links", "A\nB\nC\nD"); assert(!configured());
    puts("keenetic: gateway omission, independent DNS, IPoE gateway/carrier and one/two/three links passed");
}

static void timeout_scene(void)
{
    logged_in(true); poll_fixture(BATCH, "v9-batch-sample-a.json");
    refuse_every = true;
    int logs = standin()->logs;
    schedule(60);
    assert(standin()->logs > logs && standin()->alarms[REFRESH].kind == STANDIN_ALARM_IN);
    refuse_every = false;
    refuse_watchdog = true;
    app_on_button(BTN_OK_PRESSED);
    assert(!scene.pending && standin()->logs > logs);
    refuse_watchdog = false;
    scene.page = 2;
    int64_t down = scene.down, up = scene.up;
    int points = scene.history.count;
    app_on_blur(); in_scene = false;
    app_on_load(); app_on_alarm(WATCHDOG);
    assert(scene.down == down && scene.up == up);
    app_on_load(); in_scene = true; app_on_focus();
    assert(scene.page == 2 && scene.history.count == points);
    assert(scene.down == down && scene.up == up);
}

static int cooldown_requests(void)
{
    int total = sends;
    for (int poll = 0; poll < POLLS; poll++) { total += standin()->polls_run[poll]; }
    return total;
}

static void resumed_refresh(void)
{
    static const char *const intervals[] = { "1 min", "5 min", "15 min" };
    static const int seconds[] = { 60, 300, 900 };
    for (int interval = 0; interval < 3; interval++) for (int pending = 0; pending < 2; pending++) {
        logged_in(true);
        poll_fixture(BATCH, "v9-batch-sample-a.json");
        setting("every", intervals[interval]);
        app_on_button(BTN_OK_PRESSED);
        if (!pending) poll_fixture(BATCH, "v9-batch-sample-b.json");
        int requests = cooldown_requests();
        int64_t due = uptime_ms + seconds[interval] * 1000;
        for (int swap = 0; swap < 3; swap++) {
            standin_pass(10001);
            app_on_blur(); in_scene = false;
            app_on_load(); in_scene = true; app_on_focus();
            assert(cooldown_requests() == requests);
            assert(scene.refresh_at == due);
            assert(standin_alarm_left(REFRESH) == (due - uptime_ms + 999) / 1000);
        }
        if (pending) {
            poll_fixture(BATCH, "v9-batch-sample-a.json");
            due = uptime_ms + seconds[interval] * 1000;
        }
        standin_pass(due - uptime_ms - 1000);
        app_on_blur(); app_on_load(); app_on_focus();
        assert(cooldown_requests() == requests && standin_alarm_left(REFRESH) == 1);
        standin_pass(1000); app_on_alarm(REFRESH);
        assert(cooldown_requests() == requests + 1);
        app_on_button(BTN_OK_PRESSED);
        assert(cooldown_requests() == requests + 1); /* Coalesce the pending read. */
    }
    logged_in(true); setting("every", "15 min");
    poll_fixture(BATCH, "v9-batch-sample-a.json");
    app_on_blur(); app_on_load(); app_on_focus();
    int requests = cooldown_requests();
    app_on_button(BTN_OK_PRESSED);
    assert(cooldown_requests() == requests + 1);
    poll_fixture(BATCH, "v9-batch-sample-b.json");
    app_on_blur(); in_scene = false;
    standin_pass(900000);
    app_on_load(); in_scene = true; app_on_focus();
    assert(cooldown_requests() == requests + 2);
    poll_fixture(BATCH, "v9-batch-sample-b.json");
    standin_forget_session(); app_on_load(); app_on_focus();
    assert(cooldown_requests() == requests + 3); /* A fresh opening refreshes. */
    puts("keenetic: restored refresh deadlines preserve request counts; due alarms and OK still refresh");
}

static void blocked_navigation(void)
{
    for (int page = 2; page <= 3; page++) for (int background = 0; background < 2; background++) {
        logged_in(true); scene.page = page;
        poll_fixture(BATCH, "v9-batch-1.json");
        if (page == 3) {
            poll_fixture(HOSTS, "router/hotspot.json");
            device_first = 5;
        } else interface_fixture();
        if (background) {
            app_on_blur(); in_scene = false; app_on_load();
        }
        control.renewing = 1;
        picked(page == 3 ? HOSTS : INTERFACES, 401, "{}");
        assert(control.blocked);
        if (!background) app_on_blur();
        scene_t saved;
        assert(app_session_load(&saved, sizeof saved));
        int requests = cooldown_requests();
        in_scene = false; app_on_load(); in_scene = true; app_on_focus();
        assert(scene.page == page && scene.status == 401 && control.blocked);
        assert(!memcmp(&scene.details, &saved.details, sizeof scene.details));
        if (page == 3) {
            assert(device_first == 5 && hosts.count == saved.details.viewport.count);
            assert(!memcmp(hosts.rows + device_first, saved.details.viewport.rows,
                           sizeof saved.details.viewport.rows));
        }
        assert(cooldown_requests() == requests && !standin_alarm_left(REFRESH));
    }
    puts("keenetic: authentication refusal preserves Network details and Devices viewport across resume");
}

static void cooldown_and_sessions(void)
{
    reset(true);
    control_t old = { .version = 3, .epoch = 7, .blocked = 1 };
    assert(kv_set("control", 7, &old, offsetof(control_t, boot)) == HOST_OK);
    app_on_load();
    assert(control.version == 4 && control.epoch == 7 && control.blocked && !control.retry_at);
    for (int raw = 0; raw < 2; raw++) {
        reset(true);
        if (!raw) { challenge(); recorded_answer("login-success", NULL); }
        standin_pair_t headers[] = { { "Retry-After", "300" } };
        standin_answer_headers(headers, 1);
        if (raw) { answer(429, "{}", 2); }
        else { standin_picked(BATCH, -1, 429, "{}", 2); }
        int before = cooldown_requests();
        assert(control.retry_at == uptime_ms + 300000);
        assert(standin_alarm_left(REFRESH) >= 300);
        app_on_button(BTN_OK_PRESSED); app_on_settings(); app_on_alarm(REFRESH);
        assert(cooldown_requests() == before);
        app_on_blur(); app_on_load(); app_on_focus();
        standin_clock(time_now() + 3600);
        standin_pass(299999); app_on_button(BTN_OK_PRESSED); app_on_alarm(REFRESH);
        assert(cooldown_requests() == before);
        standin_pass(1); app_on_button(BTN_OK_PRESSED);
        assert(cooldown_requests() == before + 1);
        /* A second refusal starts a full new cooldown. */
        standin_answer_headers(headers, 1);
        answer(429, "{}", 2);
        before = cooldown_requests();
        standin_pass(299999); app_on_alarm(REFRESH);
        assert(cooldown_requests() == before);
    }
    reset(true);
    uint64_t diagnostics = standin()->diagnostics;
    standin_refuse_session(HOST_ERR_NO_SPACE);
    app_on_blur();
    assert(standin_refused_sessions() == 1 && standin()->diagnostics != diagnostics);
    puts("keenetic: login and poll cooldowns survive settings, clock changes and unload; session failures are diagnosed");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    resumed_refresh(); blocked_navigation();
    cooldown_and_sessions(); timeout_scene();
    chart_neighbours(); refresh_phase(); optional_rates();
    restart_login(); detail_failures(); long_host_cut(); network_variants();
    network_delivery_order(); network_loading(); device_two_lines(); live_v9(); system_temperatures(); protocol(); background(); interface_settings(); router_rates(); bounds(); stream_failures();
    hash_vectors(); input_boundaries(); graph_history(); cold_history(); corrupt_history(); four_pages(); list_navigation(); host_lists();
    return 0;
}
