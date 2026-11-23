// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ha.c"
#include "standin.h"

static void answer(const char *body, int piece)
{
    standin_data(request_screen(), -1, 200, body, (int32_t)strlen(body), piece, STANDIN_WHOLE, 0);
}

int main(void)
{
    standin_reset();
    standin_handlers.on_data = app_on_data;
    standin_handlers.on_draw = app_on_draw;
    app_on_load(); app_on_focus();
    answer("None", 1);
    uint64_t reason = standin_draw();
    static const char *const invalid[] = {
        "{[[[layout|single\nitem|Living room|thermometer|22.5°", /* verified blank screen */
        "", "None", "{}", "<html>error</html>", "layout|single", "page|1|3\nlayout|list",
        "layout|tiles\nhero|21|sunny", "item|Room|thermometer|22",
        "hero||unknown", "detail|||unknown", "layout|single\nitem||unknown|",
        "hero", "hero|21", "column|Room|thermometer", "layout|list\nitem|||",
        "layout|list\nitem||sunny|", /* lists do not draw pictures */
        "layout|single\nitem|||\nitem|Room||22", /* only the first item is drawn */
        "layout|tiles\nitem|||\nitem|||\nitem|||\nitem|||\nitem|Room||22",
        ("layout|list\nitem|||\nitem|||\nitem|||\nitem|||\nitem|||\nitem|||"
         "\nitem|||\nitem|||\nitem|||\nitem|||\nitem|||\nitem|||\nitem|Room||22"),
        "detail|||\ndetail|||\ndetail|22||",
        "column|||\ncolumn|||\ncolumn|||\ncolumn|||\ncolumn|Room||22",
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        answer(invalid[i], 1);
        assert(screen_length == 0 && last_status == 200 && standin_draw() == reason);
        int32_t bytes;
        assert(standin_value(SCREEN_KEY, &bytes) == NULL);
    }
    static const char *const valid[] = {
        "hero|21|sunny", "detail|54|%|drop", "column|Door||Closed",
        "layout|single\nitem|Room|thermometer|22.5°",
        "layout|tiles\nitem|Door||Closed", "layout|list\nitem|Room||22",
        "page|2|3\nlayout|single\nitem|Room|thermometer|22.5°",
        "layout|single\nitem||sunny|", /* cells do draw pictures */
        "layout|list\nitem||sunny|\nitem|Room||22",
        "layout|tiles\nitem|||\nitem|Room||22",
    };
    for (unsigned i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        answer(valid[i], 7);
        assert(!strcmp(screen, valid[i]));
    }
    answer(valid[6], 7);
    cached_screen_t before;
    int32_t size;
    const uint8_t *value = standin_value(SCREEN_KEY, &size);
    assert(value && size <= (int32_t)sizeof before);
    memcpy(&before, value, size);
    int32_t cache_size = size;
    uint64_t good = standin_draw();
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        current_page = 3;
        answer(invalid[i], 1);
        assert(!strcmp(screen, valid[6]) && current_page == 2 && shown_page == 2 && page_count == 3);
        value = standin_value(SCREEN_KEY, &size);
        assert(value && size == cache_size && !memcmp(value, &before, size) && standin_draw() == good);
        app_on_load();
        assert(!strcmp(screen, valid[6]) && shown_page == 2);
    }
    char oversized[SCREEN_MAX + 2];
    memset(oversized, 'a', sizeof oversized - 1);
    memcpy(oversized, "hero|21|", 8);
    oversized[sizeof oversized - 1] = '\0';
    answer(oversized, 1);
    assert(!strcmp(screen, valid[6]));
    /* Embedded NULs must not hide malformed fields from the line reader. */
    static const char nul[] = "hero|21|sunny\0junk";
    standin_data(request_screen(), -1, 200, nul, sizeof nul - 1, 1, STANDIN_WHOLE, 0);
    assert(!strcmp(screen, valid[6]));
    /* A cache accepted by an older version must not render blank after upgrade. */
    cached_screen_t old = { .version = SCREEN_VERSION, .pairing = pairing() };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        strcpy(old.text, invalid[i]);
        assert(kv_set(SCREEN_KEY, sizeof SCREEN_KEY - 1, &old,
                      offsetof(cached_screen_t, text) + strlen(old.text)) == HOST_OK);
        app_on_load();
        assert(screen_length == 0 && standin_draw() == reason);
    }
    puts("ha: malformed screens show the reason, preserve the good cache and page, and remain refused after reload");
}
