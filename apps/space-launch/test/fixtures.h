// SPDX-License-Identifier: MIT
/* The library's answers kept for the scenarios, read by the tests too. */
#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURES "../../../tests/fixtures/space-launch/"
#define AT       1795456800 /* 2026-11-23 18:00 UTC, as test/check.json */

__attribute__((unused)) static char *fixture(const char *name, int32_t *length)
{
    char path[256];
    snprintf(path, sizeof path, FIXTURES "%s", name);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *bytes = malloc((size_t)size + 1);
    assert(bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    bytes[size] = '\0';
    *length = (int32_t)size;
    return bytes;
}

/* A poll's answer from a fixture file, picked as the device picks it. */
__attribute__((unused)) static void answer_file(int32_t poll, int32_t item, const char *name)
{
    int32_t length;
    char *body = fixture(name, &length);
    standin_picked(poll, item, 200, body, length);
    free(body);
}

__attribute__((unused)) static void answer_text(int32_t poll, int32_t item, int32_t status, const char *body)
{
    standin_picked(poll, item, status, body, (int32_t)strlen(body));
}

__attribute__((unused)) static int32_t flash_writes(void)
{
    return standin()->store_changes + standin()->file_writes;
}

/* Everything the scenarios load: both lists, this year's pages and the six countries. */
__attribute__((unused)) static void answer_all(void)
{
    static const int32_t COUNTS[] = { 157, 83, 15, 7, 4, 4 };
    answer_file(0, -1, "upcoming.json");
    answer_file(1, -1, "previous.json");
    answer_file(2, 0, "year-0.json");
    answer_file(2, 1, "year-100.json");
    answer_file(2, 2, "year-200.json");
    answer_file(2, 3, "year-300.json");
    for (int32_t i = 0; i < 6; i++) {
        char body[64];
        snprintf(body, sizeof body, "{\"count\":%d,\"next\":null,\"previous\":null,\"results\":[]}", COUNTS[i]);
        answer_text(3, i, 200, body);
    }
}
