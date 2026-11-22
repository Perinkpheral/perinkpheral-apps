// SPDX-License-Identifier: MIT
#include "metrics.h"
#include "app_values.h"
#include "app_text.h"
#include <stddef.h>
#include <limits.h>
/* Router speeds are bit/s; the display uses hundredths of Mbit/s. */
bool keenetic_uint(const char *text, uint64_t *out)
{
    if (!text || !*text)
        return false;
    uint64_t value = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || value > (UINT64_MAX - (unsigned)(*text - '0')) / 10)
            return false;
        value = value * 10 + (unsigned)(*text - '0');
    }
    *out = value;
    return true;
}
__attribute__((noinline)) static int64_t integer(const char *text)
{
    uint64_t n;
    return keenetic_uint(text, &n) && n <= INT64_MAX ? (int64_t)n : -1;
}
__attribute__((noinline)) static int64_t metric(int index)
{
    char text[64];
    return app_value_text(index, text, sizeof text) ? integer(text) : -1;
}
void keenetic_number(app_text_t *text, int64_t value, int decimals)
{
    char digits[32];
    int at = sizeof digits, n = 0;
    digits[--at] = 0;
    uint64_t remaining = value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
    do {
        if (n == decimals && n)
            digits[--at] = '.';
        digits[--at] = (char)('0' + remaining % 10);
        remaining /= 10;
        n++;
    } while (remaining || n <= decimals);
    if (value < 0)
        digits[--at] = '-';
    app_text_add(text, digits + at);
}
bool keenetic_read(keenetic_reading_t *v)
{
    __builtin_memset(v, 0, sizeof *v);
    int64_t cpu = metric(0);
    v->cpu = cpu >= 0 && cpu <= 100 ? (int32_t)cpu : -1;
    char memory[64];
    int64_t used = -1;
    if (app_value_text(1, memory, sizeof memory)) {
        for (int i = 0; memory[i]; i++)
            if (memory[i] == '/') { memory[i] = 0; break; }
        used = integer(memory);
    }
    int64_t total = metric(2);
    v->ram = used >= 0 && total > 0 && used <= total && total <= INT32_MAX
                 ? (int32_t)((used * 100 + total / 2) / total) : -1;
    v->uptime = metric(3);
    int64_t all = metric(4), free = metric(5);
    v->connections = all >= 0 && free >= 0 && free <= all && all - free <= INT32_MAX
                         ? (int32_t)(all - free) : -1;
    app_value_text(8, v->wan, sizeof v->wan);
    if (!v->wan[0]) v->wan[0] = '-';
    char internet[64];
    app_value_text(9, internet, sizeof internet);
    v->internet = app_text_equal(internet, "true") ? 1 : app_text_equal(internet, "false") ? 0 : -1;
    int64_t down = metric(6), up = metric(7);
    v->down = down < 0 ? -1 : down / 10000;
    v->up = up < 0 ? -1 : up / 10000;
    /* Rates may be absent; core system fields still reject command errors. */
    return v->cpu >= 0 && v->ram >= 0 && v->uptime >= 0 && v->connections >= 0;
}
