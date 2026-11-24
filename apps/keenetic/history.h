// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define KEENETIC_SAMPLES 30
#define KEENETIC_GAP INT32_MIN
typedef struct {
    int32_t down[KEENETIC_SAMPLES], up[KEENETIC_SAMPLES];
    int32_t count;
    int64_t last_ms;
    uint32_t boot, revision;
    bool baseline, background;
} keenetic_history_t;
/* Unknown observations break the line; consecutive gaps take just one slot. */
void keenetic_history_add(keenetic_history_t *history, int64_t down, int64_t up);
