// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../stocks.c"
#include "standin.h"

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    app_on_load(); app_on_focus();
    const char *invalid[] = { "null", "9223372036854775807", "\"invalid\"" };
    for (int variant = 0; variant < 3; variant++) {
        char body[1024];
        int length = snprintf(body, sizeof body,
            "{\"chart\":{\"result\":[{\"meta\":{\"regularMarketPrice\":123.45,\"chartPreviousClose\":%s,"
            "\"regularMarketTime\":%s},\"indicators\":{\"quote\":[{\"close\":[%s,123.45]}]}}]}}",
            invalid[variant], invalid[variant], invalid[variant]);
        standin_picked(POLL_YAHOO, 0, 200, body, length);
        assert(quote.has_data && quote.price == 12345 && quote.previous == quote.price);
        char text[32];
        assert(when_text(quote.time, text, sizeof text) == 0);
        assert(quote.count == 2 && quote.series[0] == HOST_CHART_NO_VALUE && quote.series[1] == 12345);
        load_current();
        assert(quote.has_data && quote.count == 2 && quote.series[0] == HOST_CHART_NO_VALUE);
    }
}
