/* SPDX-License-Identifier: MIT */
#pragma once
#include "app_draw.h"
#include "app_fixed.h"

/* The captions and point marks are application annotations. Keep their full
 * ink outside the plot rectangle clipped by Perinkpheral OS. */
static void draw_market_chart(const host_chart_t *plot, const int32_t *values)
{
    if (ui_draw_chart(plot, values) != HOST_OK || plot->count < 2 || plot->height < 2) { return; }
    int32_t low = INT32_MAX, high = INT32_MIN, last = -1;
    for (int32_t i = 0; i < plot->count; i++) {
        if (values[i] == HOST_CHART_NO_VALUE) { continue; }
        if (values[i] < low) { low = values[i]; }
        if (values[i] > high) { high = values[i]; }
        last = i;
    }
    if (last < 0) { return; }
    char labels[2][24];
    const int32_t bounds[] = { high, low };
    int32_t column = 0, right = plot->x + plot->width;
    for (int32_t i = 0; i < 2; i++) {
        app_text_t text;
        app_text_start(&text, labels[i], sizeof labels[i]);
        app_text_add_fixed(&text, bounds[i], plot->decimals, 0);
        for (int32_t j = 0; labels[i][j]; j++) {
            if (labels[i][j] == '.') { labels[i][j] = app_string(STR_DECIMAL_POINT)[0]; }
        }
        int32_t width = app_text_width(HOST_FONT_SMALL, labels[i]);
        if (width > column) { column = width; }
        int32_t baseline = i == 0 ? plot->y + gfx_font_cap_height(HOST_FONT_SMALL) :
                                   plot->y + plot->height - 1;
        app_draw_right(right, baseline - gfx_font_ascent(HOST_FONT_SMALL), HOST_FONT_SMALL, labels[i]);
    }
    int32_t width = plot->width - column - 6;
    if (width > 7) { width -= 3; } /* reserve half of the seven-pixel last mark */
    if (width <= 0) { return; }
    if (low == high) {
        if (low > INT32_MIN) { low--; }
        if (high < INT32_MAX) { high++; }
    }
    for (int32_t i = 0; i <= last; i++) {
        if (values[i] == HOST_CHART_NO_VALUE) { continue; }
        bool isolated = (i == 0 || values[i - 1] == HOST_CHART_NO_VALUE) &&
                        (i + 1 == plot->count || values[i + 1] == HOST_CHART_NO_VALUE);
        if (!isolated && i != last) { continue; }
        int32_t x = plot->x + (int32_t)((int64_t)i * (width - 2) / (plot->count - 1));
        int32_t y = plot->y + plot->height - 2 -
            (int32_t)(((int64_t)values[i] - low) * (plot->height - 2) / ((int64_t)high - low));
        if (isolated) { gfx_draw_rect(x - 1, y - 1, 4, 4, HOST_RECT_FILLED); }
        if (i == last) { gfx_draw_rect(x - 3, y - 3, 7, 7, HOST_RECT_FILLED); }
    }
}
