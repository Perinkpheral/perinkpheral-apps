// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../stocks.c"
#include "standin.h"

static void refused_quote(int32_t item, int32_t failure)
{
    static const char body[] =
        "{\"chart\":{\"result\":[{\"meta\":{\"regularMarketPrice\":123.45,"
        "\"chartPreviousClose\":122,\"priceHint\":2},"
        "\"indicators\":{\"quote\":[{\"close\":[122,123.45]}]}}]}}";
    standin()->refuse_writes = failure;
    standin_picked(POLL_YAHOO, item, 200, body, sizeof body - 1);
    standin_draw();
    assert(strstr(standin()->draw_text, "Could not save.") != NULL);
    standin()->refuse_writes = HOST_OK;
}

int main(void)
{
    standin_handlers.on_picked = app_on_picked;
    standin_handlers.on_draw = app_on_draw;
    const int32_t failures[] = { HOST_ERR_NO_SPACE, HOST_ERR_FAILED };
    for (int32_t i = 0; i < 2; i++) {
        standin_reset();
        standin_set("tickers", "AAPL\nIBM");
        app_on_load(); app_on_focus();
        refused_quote(0, failures[i]);
        /* No cached record exists when the ticker's very first save fails. */
        standin_set("rotate", "2 min"); app_on_settings();
        standin_draw();
        assert(strstr(standin()->draw_text, "Could not save.") != NULL);
        app_on_button(BTN_DOWN_PRESSED);
        refused_quote(1, failures[i]);
        /* Preserve an unchanged slot; clear the replaced slot even off screen. */
        standin_set("tickers", "MSFT\nIBM"); app_on_settings();
        assert(unsaved_quotes == (1u << 1));
        standin_draw();
        assert(strstr(standin()->draw_text, "Could not save.") != NULL);
        app_on_button(BTN_UP_PRESSED); standin_draw();
        assert(strstr(standin()->draw_text, "Could not save.") == NULL);
        standin_set("tickers", "IBM"); app_on_settings();
        assert(unsaved_quotes == 0);
        standin_draw();
        assert(strstr(standin()->draw_text, "Could not save.") == NULL);
        refused_quote(0, failures[i]);
        standin_set("tickers", ""); app_on_settings(); standin_draw();
        assert(unsaved_quotes == 0);
        assert(strstr(standin()->draw_text, "Could not save.") == NULL);
        assert(standin()->visible_failure);
        uint64_t unconfigured = standin()->picture;
        app_on_terminate(); app_on_load(); app_on_focus();
        assert(standin_draw() == unconfigured);
    }
}
