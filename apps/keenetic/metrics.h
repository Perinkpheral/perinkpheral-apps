// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_text.h"
typedef struct {
    int32_t cpu, ram, connections, internet;
    int64_t uptime, down, up;
    char wan[32];
} keenetic_reading_t;
/* Read the traffic poll values in manifest order; no JSON in application code. */
bool keenetic_read(keenetic_reading_t *reading);
bool keenetic_uint(const char *text, uint64_t *value);
void keenetic_number(app_text_t *text, int64_t value, int decimals);
