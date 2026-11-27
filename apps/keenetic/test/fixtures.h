// SPDX-License-Identifier: MIT
#pragma once
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "json_pick.h"

/* Keep the original evidence beside the simulator mocks so their relative paths still work. */
static inline char *fixture_text(const char *name)
{
    char path[512];
    int length = snprintf(path, sizeof path, "../../../tests/keenetic/fixtures/%s", name);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "rb");
    if (!file) fprintf(stderr, "cannot open fixture %s\n", path);
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t)size + 1, 1);
    assert(text && fread(text, 1, (size_t)size, file) == (size_t)size && !ferror(file));
    assert(fclose(file) == 0);
    return text;
}

typedef struct { int start, end, next; } fixture_token_t;
typedef struct {
    char *text;
    fixture_token_t *tokens;
    int count;
} fixture_json_t;

/* The SDK validates JSON first. This index retains complete strings and uint64 numbers,
 * which the SDK picker's 63-byte scalars cannot compare without truncation. */
static inline int fixture_token(fixture_json_t *json, int *offset)
{
    char *text = json->text;
    while (isspace((unsigned char)text[*offset])) ++*offset;
    int token = json->count++;
    json->tokens[token].start = *offset;
    char kind = text[(*offset)++];
    if (kind == '{' || kind == '[') {
        char close = kind == '{' ? '}' : ']';
        for (;;) {
            while (isspace((unsigned char)text[*offset]) || text[*offset] == ',' || text[*offset] == ':') ++*offset;
            if (text[*offset] == close) { ++*offset; break; }
            fixture_token(json, offset);
        }
    } else if (kind == '"') {
        while (text[*offset] != '"') {
            if (text[*offset] == '\\') ++*offset;
            ++*offset;
        }
        ++*offset;
    } else {
        while (text[*offset] && !strchr(" \r\n\t,]}", text[*offset])) ++*offset;
    }
    json->tokens[token].end = *offset;
    json->tokens[token].next = json->count;
    return token;
}

static inline fixture_json_t fixture_json(const char *name)
{
    fixture_json_t json = {.text = fixture_text(name)};
    size_t length = strlen(json.text);
    json_pick_t valid;
    json_pick_init(&valid, NULL, 0);
    bool whole = json_pick_feed(&valid, json.text, length) && json_pick_finish(&valid);
    if (!whole)
        fprintf(stderr, "invalid JSON fixture: %s\n", name);
    assert(whole);
    json.tokens = calloc(length + 1, sizeof *json.tokens);
    assert(json.tokens);
    int offset = 0;
    fixture_token(&json, &offset);
    return json;
}

static inline void fixture_close(fixture_json_t *json)
{
    free(json->text);
    free(json->tokens);
}

static inline void fixture_string(const fixture_json_t *json, int token, char *out, size_t capacity)
{
    assert(token >= 0 && token < json->count && capacity);
    fixture_token_t span = json->tokens[token];
    assert(json->text[span.start] == '"');
    size_t used = 0;
    for (int at = span.start + 1; at < span.end - 1; at++) {
        unsigned char value = (unsigned char)json->text[at];
        if (value == '\\') {
            value = (unsigned char)json->text[++at];
            switch (value) {
            case 'b': value = '\b'; break;
            case 'f': value = '\f'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case 'u': {
                /* Captured keys and escaped header parameters are ASCII. UTF-8 values
                 * are kept verbatim; reject an unsupported escape instead of losing bytes. */
                char hex[5];
                memcpy(hex, json->text + at + 1, 4); hex[4] = 0;
                unsigned long code = strtoul(hex, NULL, 16);
                assert(code > 0 && code < 128);
                value = (unsigned char)code;
                at += 4;
                break;
            }
            default: break;
            }
        }
        assert(used + 1 < capacity);
        out[used++] = (char)value;
    }
    out[used] = 0;
}

static inline int fixture_member(const fixture_json_t *json, int object, const char *name)
{
    assert(object >= 0 && json->text[json->tokens[object].start] == '{');
    for (int key = object + 1; key < json->tokens[object].next; key = json->tokens[key + 1].next) {
        char text[256];
        fixture_string(json, key, text, sizeof text);
        if (!strcmp(text, name)) return key + 1;
    }
    return -1;
}

static inline int fixture_count(const fixture_json_t *json, int array)
{
    assert(array >= 0 && json->text[json->tokens[array].start] == '[');
    int count = 0;
    for (int item = array + 1; item < json->tokens[array].next; item = json->tokens[item].next) count++;
    return count;
}

static inline bool fixture_equal(const fixture_json_t *left, int a, const fixture_json_t *right, int b)
{
    assert(a >= 0 && b >= 0);
    char kind = left->text[left->tokens[a].start];
    if (kind != right->text[right->tokens[b].start]) return false;
    if (kind == '{' || kind == '[') {
        int x = a + 1, y = b + 1;
        while (x < left->tokens[a].next && y < right->tokens[b].next) {
            if (!fixture_equal(left, x, right, y)) return false;
            x = left->tokens[x].next;
            y = right->tokens[y].next;
        }
        return x == left->tokens[a].next && y == right->tokens[b].next;
    }
    int length = left->tokens[a].end - left->tokens[a].start;
    return length == right->tokens[b].end - right->tokens[b].start &&
           !memcmp(left->text + left->tokens[a].start, right->text + right->tokens[b].start, (size_t)length);
}
