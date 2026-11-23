# Earthquakes / Землетрясения

Recent U.S. Geological Survey reports, a regional coastline map, Worldwide history
and optional post-event nearby notices. Requires Perinkpheral OS 0.1.95 or later.
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
- Long UP/DOWN switches map/list, with the SDK navigation state's 1/2 and 2/2 counts.
- List: UP/DOWN selects; OK opens the report; BACK closes it or exits the list.
- Long OK switches Nearby/Worldwide. An in-flight Worldwide request resumes
  when switching back to that scope.
- Worldwide: DOWN at the last row fetches eight older reports. UP at the first
  row of an older page returns to Latest 8. Nearby retains its latest eight.
- A system notice uses the standard OK/BACK dismissal.

Worldwide keeps only its current eight-report page, with a 120-day display
window and a 1,000-page request cap. The query starts at the platform's earliest
supported date, 2026-01-01; the client stops at the 120-day boundary. Pages
continue from the oldest displayed origin timestamp, so insertions or removals
earlier in the live feed do not shift the next page. Inclusive boundary queries
remove already displayed identities, including when the boundary report itself
has disappeared. Equal timestamps advance within that boundary; history pages
can contain fewer than eight new reports. Missing Place leaves Worldwide available.

The map's filled point is the report and its cross is your configured place.
Nearby event pages retain the inset map; distant events use full-width text.
The event page shows magnitude, place, distance,
depth, local date/time and source. Unknown numbers show `--`. Magnitude type,
review status and extra revision/radius captions are omitted to keep it small.
A ring is an **editorial relevance filter**, never a boundary of shaking,
damage or danger: M4/100 km, M5/300 km, M6/1000 km, M7+/2000 km.

## Requests and persistence

Two poll descriptors replace the original four. Nearby runs at the selected
interval, subject to the SDK's power-saving floor. Worldwide is gated by its
offset parameter and fetched on demand; while its list is visible, it refreshes
no more often than every 15 minutes or the provider's longer max-age. Query
parameters stay locked until a complete reply. A pending request can resume
after an unload. The idle Worldwide descriptor causes no HTTP requests.

Both numeric and HTTP-date Retry-After are respected, with 5/15/60-minute error
backoff. Cache-Control waits the full max-age, conservatively ignoring Age.
An alarm and missing query keys cover provider pauses longer than one day.
There are no cache-busting parameters. HTTP 204 is an empty success.

One versioned, checksummed file holds both pages, 32 notification cluster records,
31 fresh observed identities, freshness and delivery intent. An interrupted write
can lose the cache; a bad checksum discards it and seeds the next response silently. There is no older
bank whose notification ledger can be replayed. Intent is saved before posting;
a crash or storage failure can lose a notice. Failed saves remain retryable.
Unchanged snapshots are not written; both scopes checkpoint freshness hourly.
A rearmed host alarm tracks Nearby liveness across background unloads without
minute-by-minute file writes. After an unload, displayed freshness can still
be conservative. Nearby cache deadlines persist independently of Worldwide
replies. Packaged data and the two-block cache fit the 32 KB file allowance. Seven
settings, even with maximum-length numeric values, plus the three query keys
remain below the 2 KB KV allowance. Valid long coordinates are normalized to
six decimals before use in requests.

Complete, equally sized picked arrays are validated before accepting a page.
Incomplete data, ambiguous/truncated identity strings and duplicate identities
within one collection leave good data intact. Exact IDs and complete aliases
merge across cached scopes; the highest revision wins. Only bounded plain
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
it. Queue rejection is retried on a later successful Nearby response while fresh;
rejected deliveries consume neither a cluster window nor an escalation.
Suppressed reports remain visible. Worldwide never interrupts. No tsunami
interpretation or prediction is implemented.

## Verification

Native checks cover geography, identities, UTC headers, bounded extraction,
malformed/empty/null responses, refused/corrupt writes, paging and unloads,
notification suppression/escalation, queue rejection and both screen families.
The simulator scenarios cover every screen and empty, offline, missing-setup
and notice states in English/Russian with both fonts.

Real USGS captures and clearly marked fault derivatives live in
`tests/fixtures/earthquakes/`. The isolated notice fixture uses a real captured
report with a test clock; it does not simulate publication latency and is not
part of the catalog module. Production has no test switches or injection paths.

The interpreter estimate is **71.879 KB (73,604 bytes)**, including the map
service's 8,128-byte working area: **124 bytes below 72 KB**. The module is
15,454 bytes. The device arena is 80 KB from firmware 0.1.95; the manifest
requires that firmware because the estimate exceeds 64 KB.

Round-three verification includes regression checks for background liveness,
late/equal-time publications, queue expiry and rejected escalation, independent
cache deadlines, boundary paging and hourly Worldwide checkpoints. The simulator
suite covers English/Russian and both fonts; Earthquake references are inspected
before the full suite is run without updating other applications' pictures.

Validation: all ten `app-check apps --app earthquakes` checks pass. The full
simulator suite passes 119 scenarios and 361 pictures; the final Earthquakes
rerun passes 20 scenarios and 80 pictures. All 80 pictures were inspected;
only Earthquakes references were updated. `./build.sh` rebuilt all 18 catalog
applications successfully.
