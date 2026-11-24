// SPDX-License-Identifier: MIT
#include <assert.h>
#include <string.h>
#include "../stocks.c"
#include "standin.h"

static void answer(const char *body)
{
    standin_picked(POLL_YAHOO, 0, 200, body, (int32_t)strlen(body));
}

int main(void)
{
    standin_reset();
    standin_handlers.on_picked = app_on_picked;
    standin_set("tickers", "AAPL");
    app_on_load(); app_on_focus();
    answer("{\"chart\":{\"result\":[{\"meta\":{\"longName\":null,\"shortName\":\"Apple\","
           "\"symbol\":null,\"currency\":null,\"regularMarketPrice\":123.45}}]}}");
    assert(!strcmp(quote.name, "Apple"));
    assert(!strcmp(quote.symbol, "AAPL"));
    assert(!strcmp(quote.currency, ""));
    answer("{\"chart\":{\"result\":[{\"meta\":{\"longName\":null,\"shortName\":null,"
           "\"symbol\":null,\"currency\":null,\"regularMarketPrice\":124.5}}]}}");
    assert(strcmp(quote.name, "null") && !strcmp(quote.symbol, "AAPL") && !quote.currency[0]);
    load_current();
    assert(strcmp(quote.name, "null") && !strcmp(quote.symbol, "AAPL") && !quote.currency[0]);
    /* Old cached literal nulls are repaired before the next response arrives. */
    app_text_copy(quote.name, sizeof quote.name, "null");
    app_text_copy(quote.symbol, sizeof quote.symbol, "null");
    app_text_copy(quote.currency, sizeof quote.currency, "null");
    assert(write_quote(0, &quote));
    load_current();
    assert(!quote.name[0] && !strcmp(quote.symbol, "AAPL") && !quote.currency[0]);

    standin_set("tickers", "MOEX:SBER");
    const char *moex = "{\"securities\":{\"data\":[[\"SBER\",null,200.00,2,null]]},"
                       "\"marketdata\":{\"data\":[[205.00,\"15:30:00\",\"2026-11-18 15:31:00\"]]}}";
    standin_picked(POLL_MOEX, 0, 200, moex, (int32_t)strlen(moex));
    assert(quote.has_data && !quote.name[0] && !strcmp(quote.currency, "RUB"));
}
