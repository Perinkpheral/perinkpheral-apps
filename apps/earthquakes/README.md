# Earthquakes

Recent U.S. Geological Survey reports, a regional coastline map, Worldwide history
and optional post-event nearby notices. Requires Perinkpheral OS 0.2.0 or later.
English/Russian and Departure/Jersey use the SDK's fonts and UI kit. Maps contain
land, sea and coastlines; report place names remain as supplied by USGS.

**Not a safety system. Reports may be delayed, missing or revised. No earthquake
prediction, dependable early warning, tsunami alerts or all-clear. Use official
local warning channels.**

## Controls

Set Place in application settings. Radius is 100/300/1000/2000 km; minimum
magnitude is 3/4/5/6. Nearby refresh is 1/5/15 minutes, default 1 minute.
Notifications default to Off and require an explicit choice of On.

- Map: UP/DOWN opens the list; OK opens the latest qualifying nearby report.
- On the map or list, hold UP/DOWN to switch views, with header counts 1/2 and 2/2.
- List: UP/DOWN selects; OK opens the report; BACK closes it or exits the list.
- In the list, hold OK to switch Nearby/Worldwide. An in-flight Worldwide request resumes
  when switching back to that scope.
- Close report details with BACK before switching views or scope.
- Worldwide: DOWN at the last row fetches eight older reports. UP at the first
  row of an older page returns to Latest 8. Nearby retains its latest eight.
- A system notice uses the standard OK/BACK dismissal.

Worldwide keeps only its current eight-report page, with a 120-day display
window and a 1,000-page request cap. The query starts at the platform's earliest
supported date, 2026-01-01; the client stops at the 120-day boundary. Pages
continue from the oldest displayed origin timestamp, so insertions or removals
earlier in the live feed do not shift the next page. Inclusive boundary queries
remove already displayed identities, including when the boundary report itself
has disappeared. Each page and refresh restarts at that time, de-duplicating
against all identities already shown there, so deletion or insertion within a
timestamp cannot move unseen reports behind a retained offset. Older boundary
identities live in a checksummed `history` file without growing the RAM cache.
If that identity file cannot be extended, the current page stays available and
advancement can be retried; storage failure never silently discards identities.
History pages can contain fewer than eight new reports. Missing Place leaves
Worldwide available.

The map's filled point is the report; the house and You/Вы label mark your
configured place. Dashed circles show the editorial radius. The regional map
uses the full area above its summary; nearby details show both places.
Nearby event pages retain the inset map; distant events use full-width text.
The event page shows magnitude, place, distance,
depth, local date/time and source. Unknown numbers show `--`. Magnitude type,
review status and extra revision/radius captions are omitted to keep it small.
A ring is an **editorial relevance filter**, never a boundary of shaking,
damage or danger: M4/100 km, M5/300 km, M6/1000 km, M7+/2000 km.

## Requests and persistence

The manifest defines two polls. Nearby runs at the selected
interval, subject to the SDK's power-saving floor. Worldwide is a manual poll
requested by the application; while its list is visible, it refreshes
no more often than every 15 minutes or the provider's longer max-age. Query
parameters stay locked until a complete reply. A pending request can resume
after an unload. A separate pending marker retains that request binding; `end` and
`offset` remain available for requested-work retries after a reply. Perinkpheral OS never
starts idle Worldwide polls at startup, on settings changes, periodically or on reconnect.

Perinkpheral OS normalizes numeric and HTTP-date Retry-After and defers all manifest
polls by that delay (60 seconds for a 429 without one), using uptime within the current
boot. The application retains independent 5/15/60-minute error backoff. Cache-Control
waits the full max-age, conservatively ignoring Age.
An alarm and missing query keys cover provider pauses longer than one day.
There are no cache-busting parameters. HTTP 204 is an empty success.

One versioned, checksummed file holds both pages, 32 notification cluster records,
31 fresh observed identities, freshness and delivery intent. An interrupted write
can lose the cache; a bad checksum discards it and seeds the next response silently. There is no older
bank whose notification ledger can be replayed. Intent is saved before posting;
a crash or storage failure can lose a notice. Failed saves remain retryable. A storage
read failure preserves the durable cache and identity history, blocks replacement writes
and pauses notification delivery until a later restore succeeds. Partial failed reads
are never treated as an empty cache or empty history.
Unchanged snapshots are not written; both scopes checkpoint freshness hourly.
Worldwide also saves its latest success at blur/termination, so reopening
retains the full refresh deadline, including a reply delivered offscreen.
A rearmed Perinkpheral OS alarm tracks Nearby liveness across background unloads without
minute-by-minute file writes. After an unload, displayed freshness can still
be conservative. Nearby keeps its Cache-Control duration in the snapshot and the platform
retains its poll interval/deadline. Worldwide replies preserve that full interval,
so identical headers cause neither snapshot writes nor interval changes.
Packaged data and the two-block cache fit the 32 KB file allowance; the boundary
identity file uses the remaining room and refuses advancement if it fills. Seven
settings, even with maximum-length numeric values, plus the three query keys
remain below the 2 KB KV allowance. Valid long coordinates are normalized to
six decimals before use in requests.

Complete, equally sized picked arrays are validated before accepting a page.
Unequal picked array lengths, ambiguous/truncated identity strings and duplicate
identities within one collection leave good data intact. A scalar `metadata.count`
extracted by the picker must be readable and match the number of events. The picker
skips object and array counts, which are indistinguishable from an absent count.
Without an extracted count, a whole missing record cannot be distinguished from
legitimately fewer events.
Exact IDs and complete aliases merge across cached scopes; the highest revision wins. Only bounded plain
decimal numbers are accepted; null magnitude/depth remains unknown.

## Notices

Only fresh, newly observed, qualifying nearby reports interrupt: at most 30
minutes old, within both the selected filters and the editorial radius above.
Bootstrap, restart, settings/time changes and stale-data recovery seed silently.
A bounded identity ledger retains fresh reports independently of their origin
order and the displayed pages. Late publications and distinct reports sharing
a timestamp can notify; cached identities and revisions stay silent.

Reports within 100 km of a notified report share a six-hour suppression window.
One new event at least one magnitude stronger may escalate it. Both notified
positions are retained; overlapping windows are closed together. A full ledger
seeds silently. There is one pending report; a stronger new report can replace
it. Queue rejection is retried on a later successful Nearby response while fresh.
Queue rejection preserves cluster and escalation eligibility when its rollback
is saved. If rollback writes continue to fail through unload, reloading in the
same boot can consume that eligibility and lose the notice.
Suppressed reports remain visible. Worldwide never interrupts. No tsunami
interpretation or prediction is implemented.

## Verification

Native checks cover geography, identities, UTC headers, bounded extraction,
malformed/empty/null responses, refused/corrupt writes, paging and unloads,
notification suppression/escalation, queue rejection and both screen families.
The simulator scenarios cover every screen and empty, failed-network, missing-setup
and notice states in English/Russian with both fonts. Empty results retain the
stale caption after expiry. Older-page requests show Loading until delivery.
Loading-state scenarios use `tests/earthquakes/loading-mocks.json`, whose older
page returns `HOST_FAILED_NOT_READY` to retain the pending query for a stable
snapshot. `pending_history` in `test/presentation.c` checks the intermediate
frame before any response is delivered.

Real USGS captures and clearly marked fault derivatives live in
`tests/fixtures/earthquakes/`. The isolated notice fixture uses a real captured
report with a test clock; it does not simulate publication latency and is not
part of the catalog module. Production has no test switches or injection paths.

Regression tests cover the following cases:

1. Expired pending intent is discarded before observing the next candidate
   (`expired_candidate` in `test/notices.c`).
2. Delivery rechecks cluster eligibility after revisions, preserving an unused
   escalation (`revised_escalation` in `test/notices.c`).
3. Equal-time history advances past prior boundary identities, including after
   refresh/unload (`equal_time_history` in `test/network.c`). Deleting an already
   shown report or inserting a report during a 24-report tie preserves every
   unseen report (`changing_equal_time_history`).
4. Worldwide's latest deadline survives foreground and background unloads
   (`world_deadline` in `test/pacing.c`).
5. Identical 60/600-second Cache-Control replies across reloads write no cache
   or settings before the hourly checkpoint; a longer Worldwide max-age
   preserves the Nearby interval (`unchanged_headers` and
   `unchanged_long_headers` in `test/pacing.c`).
6. Each scope retains its status. Only a missing network says No network; other
   failures say No answer. An idle, unrequested Worldwide view says No data
   (`status_scopes` in `test/presentation.c`).
7. Both maps show a labeled home marker, the main map reclaims its upper band,
   and relevance circles use dashes (`map_markers` in `test/presentation.c`).
8. USGS shares the date row above the actions, with measured horizontal space
   (`source_clearance` in `test/presentation.c`, plus all four event pictures
   per language/font).

`test/pacing.c` covers manual Worldwide dispatch and firmware cooldowns alongside
Cache-Control and independent backoff. `test/read_failures.c` injects partial cache and
history read failures and verifies preservation and subsequent restoration.

Cache format version 4 is required. Incompatible snapshots are discarded, and
the next Nearby reply seeds notifications silently.

The following measurements describe the earlier module pinned by source and module hashes in
[test/arena.json](test/arena.json); they are not measurements of later builds.

That measured runtime peak is **72.477 KB (74,216 bytes)** of interpreter
area, **7,704 bytes below the 80 KB capacity** and 488 bytes above the previous
72 KB target. This includes map scratch, allocator metadata
and moving realloc overlap; the 65,536-byte linear page is counted separately.
The 32-bit ARM/QEMU harness ran all twenty scenarios and a combined history,
malformed-reply, notice, background-unload and foreground-return path at the
80 KB interpreter capacity, with no allocation failures. The separate eager
compile-all probe reports 75.727 KB including scratch; that sizing sweep is
recorded separately from scenario runtime peaks in [test/arena.json](test/arena.json).
That measured module is 16,750 bytes and requires firmware 0.1.95.

Historical validation for that module: all 19 applications pass `app-check apps` (125 checks); all 80
Earthquakes pictures were visually reviewed in both languages and fonts.
The full `sim-test tests --jobs 3` run passes 132 scenarios and 435 pictures;
the final Earthquakes rerun passes 20 scenarios and 80 pictures without updates.
Only Earthquakes references changed. `./build.sh` rebuilt all 19 catalog
applications; the measured module hash matches the delivered module.

Refused session saves and alarm allocations are logged. Storage failures remain
visible until the cache or affected query key is saved. Picked columns
must agree with each other, and with `metadata.count` of the [USGS GeoJSON format](https://earthquake.usgs.gov/earthquakes/feed/v1.0/geojson.php)
when the answer has it: the service omits it from a query with a limit. Fixtures
cover both. Exact null magnitude, depth and place variants in
`test/check.json` retain their documented unavailable-field fallbacks; required
identity, time, coordinates and row counts remain validated.
