# HA Companion

A Home Assistant dashboard for perINKpheral. The
[Home Assistant integration](https://github.com/perinkpheral/perinkpheral-hacs)
chooses the layout and entities; this application draws their values on the display.

## Set up

1. Install **HA Companion** from the application catalog on the device's page.
2. Install the perINKpheral integration through HACS and restart Home Assistant.
3. In Home Assistant, open **Settings - Devices & services - Add integration -
   perINKpheral**. Enter the display address and the Home Assistant origin reachable
   from the display, such as `http://homeassistant.local:8123`. Do not include a
   dashboard path, query or fragment.
4. Add up to four screens in the integration options. Choose each screen's layout and entities.
5. Open HA Companion on the display.

The integration writes the server address and its own pairing secret to the
application. These fields are hidden on the device's page; no long-lived Home
Assistant access token or template sensor is required. The application's visible
setting is the refresh interval. Perinkpheral OS schedules refreshes, with a five-minute
minimum while power saving is active. OK requests a refresh when no server
cooldown is active. Perinkpheral OS enforces cooldowns for scheduled polls without
changing the refresh setting. The application also retains a service-wide cooldown
for button-driven raw requests, using refusals from either request type.
Hold UP or DOWN to switch screens, wrapping at the ends. Short UP and DOWN also
switch screens outside lists. In a list, short presses scroll and stop at the
ends; held presses scroll when there is only one screen.
Returning from another scene keeps the selected screen; opening from the menu or
restarting uses the screen of the cached picture. The previous picture stays until the reply arrives.

## Screen format

The integration serves one rendered screen at
`/api/perinkpheral/screen?page=<n>`, with a 1-based page number. An absent page means
screen 1; a page past the end returns the last screen. The first response line is
`page|<n>|<count>` with the actual page and a count from 1 to 4. Older integrations
without this line keep their existing single-screen behavior.
The application sends the pairing secret in `Authorization: Bearer <secret>`.
This is the integration's endpoint, not Home Assistant's template-sensor API.

Each line describes a place on the screen, with parts separated by `|`:

```text
hero|21°|cloudy
detail|54|%|drop
detail|1013|hPa|gauge
column|Room|thermometer|22.5°
column|Fridge|thermometer|4°
column|CO2|co2|650
```

| Place | Parts | What it is |
|---|---|---|
| `page` | page, count | the selected screen and the number of screens |
| `hero` | value, picture | the main value; up to two UTF-8 bytes before an optional degree sign use the larger hero font |
| `detail` | value, unit, picture | up to two, beside the hero |
| `column` | name, picture, value | up to four, below the rule |

## Layouts

The default layout uses the places above. Other layouts begin with
`layout|<name>` and use `item|name|picture|value` lines:

| Layout | What it is |
|---|---|
| `tiles` | up to four items; one fills the screen, two use halves, three or four use quarters |
| `single` | the first item, as large as it fits |
| `list` | up to twelve rows; short UP and DOWN scroll; held presses switch screens, or scroll with one screen |

```text
layout|tiles
item|Living room|thermometer|22.5°
item|CO2|co2|650
item|Input|plug|5V
item|Network|network|100
```

A picture may be empty (`item|Door||Closed`). Pictures include weather states
(`sunny`, `cloudy`, `rainy`, `snowy`, `lightning`, `partlycloudy`, `clear-night`),
and `drop`, `co2`, `gauge`, `thermometer`, `battery`, `bulb`, `door`, `home`,
`moon`, `plug`, `bolt`, `chip`, `network`, `umbrella`, `wind`, `bell`,
`clock`, `check`, `warning`, `info`, `play`.

Text is UTF-8. Character coverage depends on the selected font and installed glyph
packs. Responses must fit in 512 UTF-8 bytes. HA Companion refuses oversized,
malformed or undrawable responses and keeps the last valid screen. Responses must
contain at least one drawable element in the selected layout. Well-formed content
outside that layout or beyond its limits is ignored. Lists require at least one
row within the twelve-row limit with a nonempty name or value; single layout uses
only the first item. Without a cached screen, request failures show the standard
reason; an unusable successful response shows "No screen". A failed state restore
shows "Storage error." and leaves saved screens, request identities, status and
cooldowns untouched until a later load can read the store. A pending failed save
also shows "Storage error." The integration removes whole
trailing lines to fit, avoiding partial values.
