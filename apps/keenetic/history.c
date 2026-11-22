// SPDX-License-Identifier: MIT
#include "history.h"
void keenetic_history_add(keenetic_history_t *h, int64_t down, int64_t up)
{
    bool valid = down >= 0 && down <= INT32_MAX && up >= 0 && up <= INT32_MAX;
    if (!valid && (!h->count || h->down[h->count - 1] == KEENETIC_GAP))
        return;
    if (h->count == KEENETIC_SAMPLES) {
        for (int i = 1; i < KEENETIC_SAMPLES; i++) {
            h->down[i - 1] = h->down[i];
            h->up[i - 1] = h->up[i];
        }
        h->count--;
    }
    h->down[h->count] = valid ? (int32_t)down : KEENETIC_GAP;
    h->up[h->count++] = valid ? (int32_t)up : KEENETIC_GAP;
}
