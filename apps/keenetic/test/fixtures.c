// SPDX-License-Identifier: MIT
#include <dirent.h>
#include "fixtures.h"

static void text_at(const fixture_json_t *json, int object, const char *key, char *out, size_t capacity)
{
    fixture_string(json, fixture_member(json, object, key), out, capacity);
}

static void same_member(const fixture_json_t *left, int a, const fixture_json_t *right, int b, const char *key)
{
    assert(fixture_equal(left, fixture_member(left, a, key), right, fixture_member(right, b, key)));
}

static void replace(char *text, size_t capacity, const char *old, const char *value)
{
    char *found = strstr(text, old);
    if (!found) return;
    size_t before = (size_t)(found - text), after = strlen(found + strlen(old));
    assert(before + strlen(value) + after < capacity);
    memmove(found + strlen(value), found + strlen(old), after + 1);
    memcpy(found, value, strlen(value));
}

static void substitute(const char *key, char *value, size_t capacity)
{
    if (fixture_case_equal(key, "Set-Cookie")) replace(value, capacity, "[redacted]", "session_id=fixture-session");
    else if (fixture_case_equal(key, "X-NDM-Realm")) replace(value, capacity, "[redacted]", "Fixture router");
    else if (fixture_case_equal(key, "X-NDM-Challenge")) replace(value, capacity, "[redacted]", "0123456789abcdef0123456789abcdef");
    else if (fixture_case_equal(key, "WWW-Authenticate")) {
        replace(value, capacity, "realm=\"[redacted]\"", "realm=\"Fixture router\"");
        replace(value, capacity, "challenge=\"[redacted]\"", "challenge=\"0123456789abcdef0123456789abcdef\"");
        replace(value, capacity, "session_id=\"[redacted]\"", "session_id=\"fixture-session\"");
        replace(value, capacity, "session_cookie=\"[redacted]\"", "session_cookie=\"session_id\"");
    }
}

static void replay(const fixture_json_t *meta, int response, const char *name, const char *body_name)
{
    char path[256], expected_file[256];
    snprintf(path, sizeof path, "replay/%s", name);
    fixture_json_t mock = fixture_json(path);
    same_member(meta, response, &mock, 0, "status");
    snprintf(expected_file, sizeof expected_file, "../keenetic/fixtures/%s", body_name);
    char actual[1024];
    text_at(&mock, 0, "file", actual, sizeof actual);
    assert(!strcmp(actual, expected_file));
    char *body = fixture_text(body_name);
    int headers = fixture_member(meta, response, "headers"), target = fixture_member(&mock, 0, "headers");
    char names[32][64];
    int occurrences[32] = {0}, unique = 0;
    size_t bytes = 0;
    char content_type[64] = "";
    for (int pair = headers + 1; pair < meta->tokens[headers].next; pair = meta->tokens[pair].next) {
        char key[64], value[1024];
        int first = pair + 1, second = meta->tokens[first].next;
        assert(fixture_count(meta, pair) == 2);
        fixture_string(meta, first, key, sizeof key);
        fixture_string(meta, second, value, sizeof value);
        substitute(key, value, sizeof value);
        if (fixture_case_equal(key, "Content-Length")) snprintf(value, sizeof value, "%zu", strlen(body));
        if (fixture_case_equal(key, "Content-Type")) snprintf(content_type, sizeof content_type, "%s", value);
        bytes += strlen(key) + strlen(value) + 2;
        int index = 0;
        while (index < unique && strcmp(names[index], key)) index++;
        if (index == unique) { assert(unique < 32); strcpy(names[unique++], key); }
        int field = fixture_member(&mock, target, key);
        assert(field >= 0);
        if (mock.text[mock.tokens[field].start] == '[') {
            int element = field + 1;
            for (int n = 0; n < occurrences[index]; n++) element = mock.tokens[element].next;
            assert(element < mock.tokens[field].next);
            fixture_string(&mock, element, actual, sizeof actual);
        } else {
            assert(!occurrences[index]);
            fixture_string(&mock, field, actual, sizeof actual);
        }
        occurrences[index]++;
        assert(!strcmp(value, actual));
    }
    assert(bytes <= 1024);
    int fields = 0;
    for (int key = target + 1; key < mock.tokens[target].next; key = mock.tokens[key + 1].next) fields++;
    assert(fields == unique);
    for (int i = 0; i < unique; i++) {
        int field = fixture_member(&mock, target, names[i]);
        if (mock.text[mock.tokens[field].start] == '[') assert(fixture_count(&mock, field) == occurrences[i]);
    }
    text_at(&mock, 0, "content_type", actual, sizeof actual);
    assert(!strcmp(actual, content_type));
    free(body);
    fixture_close(&mock);
}

static unsigned kind(const fixture_json_t *json, int token)
{
    const char *text = json->text + json->tokens[token].start;
    switch (*text) {
    case '{': return 1;
    case '[': return 2;
    case '"': return 4;
    case 't': case 'f': return 8;
    case 'n': return 16;
    default:
        for (int i = json->tokens[token].start; i < json->tokens[token].end; i++)
            if (strchr(".eE", json->text[i])) return 32;
        return 64;
    }
}

static void evidence(void)
{
    fixture_json_t manifest = fixture_json("router/manifest.json");
    assert(fixture_count(&manifest, 0) == 23);
    for (int item = 1; item < manifest.tokens[0].next; item = manifest.tokens[item].next) {
        char name[128], path[256], body[128];
        text_at(&manifest, item, "http", name, sizeof name);
        snprintf(path, sizeof path, "router/%s", name);
        fixture_json_t meta = fixture_json(path);
        int request = fixture_member(&meta, 0, "request"), response = fixture_member(&meta, 0, "response");
        same_member(&manifest, item, &meta, request, "method");
        same_member(&manifest, item, &meta, request, "path");
        same_member(&manifest, item, &meta, response, "status");
        text_at(&manifest, item, "body", body, sizeof body);
        char linked[128]; text_at(&meta, response, "body_file", linked, sizeof linked);
        assert(!strcmp(body, linked));
        int request_body = fixture_member(&meta, request, "body_file");
        if (request_body >= 0) {
            fixture_string(&meta, request_body, linked, sizeof linked);
            snprintf(path, sizeof path, "router/%s", linked);
            char *text = fixture_text(path); free(text);
        }
        snprintf(path, sizeof path, "router/%s", body);
        char replay_name[128]; strcpy(replay_name, name);
        char *suffix = strstr(replay_name, ".http.json"); assert(suffix); strcpy(suffix, ".json");
        replay(&meta, response, replay_name, path);
        if (!strcmp(name, "batch-sample-a.http.json")) replay(&meta, response, "extended-a.json", "batch.json");
        if (!strcmp(name, "batch-sample-b.http.json")) replay(&meta, response, "extended-b.json", "batch-second.json");
        fixture_close(&meta);
    }
    fixture_close(&manifest);
}

static void compositions(void)
{
    const char *sources[] = {"router/batch-sample-a.json", "router/batch-sample-b.json"};
    const char *targets[] = {"batch.json", "batch-second.json"};
    fixture_json_t hotspot = fixture_json("router/v9-get-02.json");
    for (int i = 0; i < 2; i++) {
        fixture_json_t source = fixture_json(sources[i]), target = fixture_json(targets[i]);
        assert(fixture_count(&source, 0) == 3 && fixture_count(&target, 0) == 4);
        int a = 1, b = 1;
        for (int n = 0; n < 3; n++) {
            assert(fixture_equal(&source, a, &target, b));
            a = source.tokens[a].next; b = target.tokens[b].next;
        }
        b = fixture_member(&target, fixture_member(&target, fixture_member(&target, b, "show"), "ip"), "name-server");
        assert(fixture_equal(&hotspot, 0, &target, b));
        fixture_close(&source); fixture_close(&target);
    }
    fixture_json_t normal = fixture_json("batch.json"), offline = fixture_json("batch-offline.json");
    assert(normal.count == offline.count);
    int changes = 0;
    for (int i = 0; i < normal.count; i++) {
        if (normal.tokens[i].next != i + 1 || fixture_equal(&normal, i, &offline, i)) continue;
        assert(kind(&normal, i) == 8 && kind(&offline, i) == 8);
        assert(!strncmp(normal.text + normal.tokens[i].start, "true", 4));
        assert(!strncmp(offline.text + offline.tokens[i].start, "false", 5));
        changes++;
    }
    assert(changes == 1);
    int third = offline.tokens[offline.tokens[1].next].next;
    int internet = fixture_member(&offline, fixture_member(&offline,
        fixture_member(&offline, fixture_member(&offline, third, "show"), "internet"), "status"), "internet");
    assert(!strncmp(offline.text + offline.tokens[internet].start, "false", 5));
    fixture_close(&normal); fixture_close(&offline); fixture_close(&hotspot);
}

static void recorded_counts(void)
{
    fixture_json_t hotspot = fixture_json("router/hotspot.json");
    int hosts = fixture_member(&hotspot, 0, "host"), active = 0;
    assert(fixture_count(&hotspot, hosts) == 102);
    for (int host = hosts + 1; host < hotspot.tokens[hosts].next; host = hotspot.tokens[host].next) {
        int value = fixture_member(&hotspot, host, "active");
        if (value >= 0 && !strncmp(hotspot.text + hotspot.tokens[value].start, "true", 4)) active++;
    }
    assert(active == 55);
    fixture_close(&hotspot);
    fixture_json_t associations = fixture_json("router/associations.json");
    assert(fixture_count(&associations, fixture_member(&associations, 0, "station")) == 9);
    fixture_close(&associations);
    char *request = fixture_text("router/batch-request.json");
    assert(strlen(request) == 111);
    free(request);
    const char *files[] = {"system.json", "system-after-renewal.json", "batch-1.json",
        "batch-sample-a.json", "batch-sample-b.json", "batch-command-error.json"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        char path[128]; snprintf(path, sizeof path, "router/%s", files[i]);
        fixture_json_t json = fixture_json(path);
        int system = json.text[json.tokens[0].start] == '['
            ? fixture_member(&json, fixture_member(&json, 1, "show"), "system") : 0;
        char memory[64]; text_at(&json, system, "memory", memory, sizeof memory);
        long long used, total;
        assert(sscanf(memory, "%lld/%lld", &used, &total) == 2);
        long long recorded_total = atoll(json.text + json.tokens[fixture_member(&json, system, "memtotal")].start);
        long long available = 0;
        const char *parts[] = {"memfree", "membuffers", "memcache"};
        for (int part = 0; part < 3; part++)
            available += atoll(json.text + json.tokens[fixture_member(&json, system, parts[part])].start);
        assert(total == recorded_total && used == total - available);
        fixture_close(&json);
    }
}

static void scenario_mocks(void)
{
    DIR *directory = opendir("../../../tests"); assert(directory);
    struct dirent *entry;
    int scenarios = 0;
    while ((entry = readdir(directory))) {
        if (strncmp(entry->d_name, "keenetic", 8)) continue;
        char path[512]; snprintf(path, sizeof path, "../../%s/scenario.json", entry->d_name);
        fixture_json_t scenario = fixture_json(path);
        char file[128]; text_at(&scenario, 0, "mocks", file, sizeof file);
        assert(!strcmp(file, "mocks.json"));
        snprintf(path, sizeof path, "../../%s/%s", entry->d_name, file);
        fixture_json_t mocks = fixture_json(path);
        int fallback = fixture_member(&mocks, 0, "*");
        assert(fallback >= 0);
        int status = fixture_member(&mocks, fallback, "status");
        assert(status >= 0 && atoi(mocks.text + mocks.tokens[status].start) == 599);
        int responses = 0;
        for (int key = 1; key < mocks.tokens[0].next; key = mocks.tokens[key + 1].next) {
            int body = fixture_member(&mocks, key + 1, "file");
            if (body < 0) continue;
            char filename[256]; fixture_string(&mocks, body, filename, sizeof filename);
            const char *prefix = "../keenetic/fixtures/";
            assert(!strncmp(filename, prefix, strlen(prefix)));
            char *text = fixture_text(filename + strlen(prefix));
            int headers = fixture_member(&mocks, key + 1, "headers");
            int length = fixture_member(&mocks, headers, "Content-Length");
            if (length >= 0) {
                char expected[32]; fixture_string(&mocks, length, expected, sizeof expected);
                assert(strtoul(expected, NULL, 10) == strlen(text));
            }
            free(text);
            responses++;
        }
        assert(responses >= 7 && responses <= 8);
        fixture_close(&mocks); fixture_close(&scenario);
        scenarios++;
    }
    closedir(directory);
    assert(scenarios == 11);
}

static void selected_interfaces(void)
{
    fixture_json_t original = fixture_json("router/interfaces.json");
    const char *names[] = {"wan", "carrier", "radio0", "radio1"};
    const char *ids[] = {"PPPoE0", "GigabitEthernet1", "WifiMaster0", "WifiMaster1"};
    const char *probes[] = {"04", "07", "08", "09"};
    for (int i = 0; i < 4; i++) {
        char path[128]; snprintf(path, sizeof path, "selected-%s.json", names[i]);
        fixture_json_t selected = fixture_json(path);
        assert(fixture_equal(&original, fixture_member(&original, 0, ids[i]), &selected, 0));
        snprintf(path, sizeof path, "router/v8-get-%s.http.json", probes[i]);
        fixture_json_t meta = fixture_json(path);
        char method[16], url[128], expected[128];
        int request = fixture_member(&meta, 0, "request");
        text_at(&meta, request, "method", method, sizeof method); assert(!strcmp(method, "GET"));
        text_at(&meta, request, "path", url, sizeof url);
        snprintf(expected, sizeof expected, "/rci/show/interface?name=%s", ids[i]); assert(!strcmp(url, expected));
        snprintf(path, sizeof path, "router/v8-get-%s.json", probes[i]);
        fixture_json_t captured = fixture_json(path);
        assert(fixture_member(&captured, 0, "id") >= 0); /* Selected object, no ID wrapper. */
        fixture_close(&captured); fixture_close(&meta); fixture_close(&selected);
    }
    fixture_close(&original);
}
static void manifest_polls(void)
{
    fixture_json_t manifest = fixture_json("../../../apps/keenetic/manifest.json");
    int polls = fixture_member(&manifest, 0, "polls");
    assert(fixture_count(&manifest, polls) == 4);
    const char *files[] = {"v9-batch-sample-a.json", "selected-wan.json", "selected-carrier.json", "router/hotspot.json"};
    const char *urls[] = {"{{router}}/rci/", "{{router}}/rci/show/interface?name={{wan}}",
        "{{router}}/rci/show/interface?name={{links}}", "{{router}}/rci/show/ip/hotspot"};
    int row = polls + 1;
    for (int i = 0; i < 4; i++, row = manifest.tokens[row].next) {
        char url[128], cookie[128], method[16] = "GET";
        text_at(&manifest, row, "url", url, sizeof url); assert(!strcmp(url, urls[i]));
        int headers = fixture_member(&manifest, row, "headers");
        text_at(&manifest, headers, "Cookie", cookie, sizeof cookie); assert(!strcmp(cookie, "{{session}}"));
        int m = fixture_member(&manifest, row, "method");
        if (m >= 0) fixture_string(&manifest, m, method, sizeof method);
        assert(!strcmp(method, i == 0 ? "POST" : "GET"));
        if (i == 0) {
            char body[1024]; text_at(&manifest, row, "body", body, sizeof body);
            replace(body, sizeof body, "{{wan}}", "PPPoE0");
            char *recorded = fixture_text("router/batch-request.json"); assert(!strncmp(body, recorded, strlen(recorded) - 1));
            assert(strstr(body, ",{\"show\":{\"ip\":{\"name-server\":{}}}}]")); free(recorded);
        }
        int picks = fixture_member(&manifest, row, "pick"), n = fixture_count(&manifest, picks);
        assert(n <= JSON_PICK_PATHS_MAX);
        char written[16][64]; const char *paths[16];
        int item = picks + 1;
        for (int j = 0; j < n; j++, item = manifest.tokens[item].next) {
            fixture_string(&manifest, item, written[j], sizeof written[j]); paths[j] = written[j];
            assert(!strstr(paths[j], "{{") && json_pick_path_is_valid(paths[j]));
        }
        char *body = fixture_text(files[i]);
        for (int chunk = 1; chunk <= 1024; chunk *= 2) {
            json_pick_t pick; char lists[8192]; json_pick_init(&pick, paths, n); json_pick_lists(&pick, lists, sizeof lists);
            size_t length = strlen(body);
            for (size_t start = 0; start < length; start += chunk)
                assert(json_pick_feed(&pick, body + start, length - start < (size_t)chunk ? length - start : (size_t)chunk));
            assert(json_pick_finish(&pick));
            for (int j = 0; j < n; j++) {
                int expected = i == 3 ? 102 : i == 1 && j == 2 ? 0 : i == 2 && j == 1 ? 0 : 1;
                assert(json_pick_count(&pick, j) == expected);
            }
            assert(!json_pick_is_cut(&pick));
        }
        /* Exercise the same literal paths against the actual targeted captures. */
        if (i == 1 || i == 2) {
            const char *actual[] = {"router/v8-get-04.json", "router/v8-get-07.json", "router/v8-get-08.json", "router/v8-get-09.json"};
            for (int a = i == 1 ? 0 : 1; a < (i == 1 ? 1 : 4); a++) {
                char *capture = fixture_text(actual[a]); json_pick_t pick; json_pick_init(&pick, paths, n);
                assert(json_pick_feed(&pick, capture, strlen(capture)) && json_pick_finish(&pick));
                assert(json_pick_value(&pick, i == 1 || a == 1 ? 0 : 1)); free(capture);
            }
        }
        free(body);
    }
    fixture_close(&manifest);
}

static void v9_compositions(void)
{
    const char *names[] = {"batch-1", "batch-sample-a", "batch-sample-b", "batch-command-error"};
    fixture_json_t dns = fixture_json("router/v9-get-02.json");
    for (int i = 0; i < 4; i++) {
        char path[128]; snprintf(path, sizeof path, "router/%s.json", names[i]);
        fixture_json_t original = fixture_json(path);
        snprintf(path, sizeof path, "v9-%s.json", names[i]);
        fixture_json_t composed = fixture_json(path);
        int count = fixture_count(&original, 0);
        assert(fixture_count(&composed, 0) == count + 1);
        int a = 1, b = 1;
        for (int n = 0; n < count; n++) {
            assert(fixture_equal(&original, a, &composed, b));
            a = original.tokens[a].next; b = composed.tokens[b].next;
        }
        b = fixture_member(&composed, fixture_member(&composed, fixture_member(&composed, b, "show"), "ip"), "name-server");
        assert(fixture_equal(&dns, 0, &composed, b));
        fixture_close(&original); fixture_close(&composed);
    }
    fixture_close(&dns);
    fixture_json_t meta = fixture_json("router/v9-get-02.http.json");
    char method[16], path[128]; int request = fixture_member(&meta, 0, "request");
    text_at(&meta, request, "method", method, sizeof method);
    text_at(&meta, request, "path", path, sizeof path);
    assert(!strcmp(method, "GET") && !strcmp(path, "/rci/show/ip/name-server"));
    fixture_close(&meta);
}
static void picker_limits(void)
{
    const char *paths[] = {"host[*].ip", "host[*].active", "host[*].name", "host[*].hostname"};
    const char *sparse = "{\"host\":[{\"ip\":\"192.0.2.1\",\"active\":true,\"hostname\":\"first\"},"
        "{\"ip\":\"192.0.2.2\",\"active\":false,\"name\":\"second\"}]}";
    json_pick_t pick; char list[8192]; json_pick_init(&pick, paths, 4); json_pick_lists(&pick, list, sizeof list);
    assert(json_pick_feed(&pick, sparse, strlen(sparse)) && json_pick_finish(&pick));
    assert(json_pick_count(&pick, 0) == 2 && json_pick_count(&pick, 1) == 2);
    assert(json_pick_count(&pick, 2) == 1 && json_pick_count(&pick, 3) == 1);
    assert(!strcmp(json_pick_element(&pick, 2, 0), "second") && !strcmp(json_pick_element(&pick, 3, 0), "first"));
    const char *typed = "{\"host\":[{\"active\":true},{\"active\":\"true\"}]}";
    json_pick_init(&pick, paths, 4); json_pick_lists(&pick, list, sizeof list);
    assert(json_pick_feed(&pick, typed, strlen(typed)) && json_pick_finish(&pick));
    assert(!strcmp(json_pick_element(&pick, 1, 0), json_pick_element(&pick, 1, 1)));
}

int main(void)
{
    v9_compositions(); picker_limits();
    selected_interfaces();
    manifest_polls();
    evidence();
    compositions();
    recorded_counts();
    scenario_mocks();
    puts("Keenetic fixtures: retained JSON inputs, 23 response links, interface projections, replay headers, DNS compositions and eleven method-aware scenario mocks passed");
    return 0;
}
