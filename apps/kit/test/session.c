// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../kit.c"
#include "standin.h"

int main(void)
{
    /* Older firmware cannot keep this editor's whole session. */
    assert(sizeof(session_t) == 140);
    FILE *file = fopen("../manifest.json", "rb");
    assert(file);
    char manifest[4096] = {0};
    size_t bytes = fread(manifest, 1, sizeof manifest - 1, file);
    assert(bytes > 0 && !ferror(file) && feof(file));
    fclose(file);
    const char *minimum = strstr(manifest, "\"firmware\"");
    assert(minimum);
    int major, minor, patch;
    assert(sscanf(strchr(minimum, ':') + 1, " \"%d.%d.%d\"", &major, &minor, &patch) == 3);
    assert(major > 0 || (major == 0 && (minor > 1 || (minor == 1 && patch >= 61))));
    standin_reset();
    standin()->is_session_writable = true;
    standin_clock(1794902400);
    contents.selected = PAGE_DATE;
    app_on_button(BTN_OK_PRESSED);
    assert(page == PAGE_DATE);
    app_on_button(BTN_UP_PRESSED);
    app_on_button(BTN_OK_PRESSED);
    app_on_button(BTN_DOWN_PRESSED);
    app_date_t edited = when;
    app_on_blur();
    page = PAGE_CONTENTS;
    when = (app_date_t){0};
    contents.selected = 0;
    app_on_focus();
    assert(page == PAGE_DATE && contents.selected == PAGE_DATE);
    assert(!memcmp(&when, &edited, sizeof when));
    int writes = standin()->store_writes;
    app_on_button(BTN_UP_PRESSED);
    assert(memcmp(&when, &edited, sizeof when));
    app_on_blur();
    assert(standin()->store_writes == writes);
    return 0;
}
