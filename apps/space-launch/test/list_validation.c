// SPDX-License-Identifier: MIT
/* Missing whole rows must not turn a complete cached list into a shorter success. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../launch.c"
#include "standin.h"
#include "fixtures.h"

int main(void)
{
    for (int32_t list = 0; list < 2; list++) {
        standin_reset(); app_on_load();
        const char *name = list ? "previous.json" : "upcoming.json";
        answer_file(list, -1, name);
        cache_t before = cache;
        int writes = standin()->file_writes;
        int32_t length;
        char *body = fixture(name, &length);
        /* Drop an entire row's fields together, as the picked columns do for {}. */
        char *first = strstr(body, "\"results\":"); assert(first);
        first = strchr(first, '[') + 1;
        /* Find the row boundary by balancing its nested objects. */
        char *end = strchr(first, '{'); int depth = 0;
        do { if (*end == '{') { depth++; } else if (*end == '}') { depth--; } end++; } while (depth);
        memmove(first + 2, end, strlen(end) + 1); first[0] = '{'; first[1] = '}';
        answer_text(list, -1, 200, body);
        assert(state.status[list] == HOST_FAILED_NO_ANSWER);
        assert(!memcmp(&cache, &before, sizeof cache) && standin()->file_writes == writes);
        free(body);
        static const char *const BAD[] = {
            "{\"count\":10,\"results\":[]}", "{\"count\":1,\"results\":[{}]}",
            "{\"count\":-1,\"results\":[]}", "{\"results\":[]}"
        };
        for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
            answer_text(list, -1, 200, BAD[i]);
            assert(state.status[list] == HOST_FAILED_NO_ANSWER && !memcmp(&cache, &before, sizeof cache));
            assert(standin()->file_writes == writes);
        }
        app_on_load(); assert(!memcmp(&cache, &before, sizeof cache));
        answer_file(list, -1, name); /* an unchanged valid retry does not rewrite flash */
        int all_writes = flash_writes();
        answer_file(list, -1, name);
        assert(state.status[list] == 200 && flash_writes() == all_writes && standin()->file_writes == writes);
        answer_text(list, -1, 200, "{\"count\":0,\"results\":[]}");
        assert(state.status[list] == 200 && cache.count[list] == 0);
    }
    puts("space-launch: malformed list row counts preserve both cached lists");
    return 0;
}
