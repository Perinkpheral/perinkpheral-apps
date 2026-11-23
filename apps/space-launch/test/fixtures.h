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

/* Deliver a response using the same persisted binding as an explicit request. */
__attribute__((unused)) static void answer_text(int32_t poll, int32_t item, int32_t status, const char *body)
{
    state.request = poll;
    state.item = item;
    state.year = cache.year;
    int32_t driver = poll;
    state.active_poll = driver + 1;
    if (poll == POLL_YEAR && item == 0) { cache.staging = (annual_t){ .year = cache.year, .total = -1 }; }
    standin_handlers.on_picked = app_on_picked;
    standin_picked(driver, -1, status, body, (int32_t)strlen(body));
}

/* A response from a fixture file. */
__attribute__((unused)) static void answer_file(int32_t poll, int32_t item, const char *name)
{
    int32_t length;
    char *body = fixture(name, &length);
    answer_text(poll, item, 200, body);
    free(body);
}

__attribute__((unused)) static int32_t flash_writes(void)
{
    return standin()->store_changes + standin()->file_writes;
}

/* Everything the scenarios load: both lists, this year's pages and the six countries. */
__attribute__((unused)) static void answer_all(void)
{
    answer_file(0, -1, "upcoming.json");
    answer_file(1, -1, "previous.json");
    for (int32_t offset = 0; offset < 271; offset += PAGE_SIZE) {
        char name[32];
        snprintf(name, sizeof name, "year-%d.json", offset);
        answer_file(POLL_YEAR, offset, name);
    }
}
