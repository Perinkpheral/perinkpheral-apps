// SPDX-License-Identifier: MIT
#include <assert.h>
#include "../sun.c"

static void check_date(int64_t day, int32_t offset, int64_t latitude, int64_t longitude)
{
    int64_t local_noon = day * SECONDS_PER_DAY + 12 * SECONDS_PER_HOUR - offset;
    sun_day_t sun = sun_on(local_noon, latitude, longitude);
    assert(sun.sky == SKY_USUAL);
    assert((sun.rise + offset) / SECONDS_PER_DAY == day);
    assert((sun.set + offset) / SECONDS_PER_DAY == day);
    assert(sun.set > sun.rise && sun.set - sun.rise < SECONDS_PER_DAY);
    /* Moving to tomorrow must move the events by about a day, even at New Year. */
    sun_day_t tomorrow = sun_on(local_noon + SECONDS_PER_DAY, latitude, longitude);
    assert(tomorrow.rise - sun.rise > 23 * SECONDS_PER_HOUR);
    assert(tomorrow.rise - sun.rise < 25 * SECONDS_PER_HOUR);
}

int main(void)
{
    const int64_t days[] = { 1794955920 / SECONDS_PER_DAY, 20818, 20819 };
    for (int32_t i = 0; i < (int32_t)(sizeof days / sizeof days[0]); i++) {
        check_date(days[i], 13 * SECONDS_PER_HOUR, -13833000, -171750000); /* Apia */
        check_date(days[i], 14 * SECONDS_PER_HOUR, 1872000, -157427000); /* Kiritimati */
        check_date(days[i], -12 * SECONDS_PER_HOUR, 9000000, 167000000);
        check_date(days[i], 3 * SECONDS_PER_HOUR, 59940000, 30310000);
        check_date(days[i], -10 * SECONDS_PER_HOUR, 21307000, -157858000);
    }
    return 0;
}
