# Earthquake fixtures

`near.json`, `world.json`, `history.json` and `new.json` are cut-down real USGS
FDSN FeatureCollection responses fetched from earthquake.usgs.gov on 2026-11-23/24.
Their metadata retains the actual source URL and generation time. Event IDs,
places, coordinates, magnitudes, origin and revision times are unchanged.
`history` is the next Worldwide page; `new` reuses the captured Worldwide answer
as a distinct valid payload for persistence tests. Scenario clocks are fixed
for rendering, not a reconstruction of original publication times.

- `near`: eight nearby reports around Panormos on Kalymnos, Greece.
- `world`: eight Worldwide M4.5+ reports.
- `history`: the next eight Worldwide reports, fetched with offset 9.
- `empty`: the captured envelope with its features removed.
- `missing`, `invalid`, `cut-identity`: deliberate mutations removing a field,
  exceeding a coordinate bound, or exceeding the identity extraction boundary.
- `null`, `worst`, `duplicate`, `revision`: deliberate mutations exercising
  unknown numbers, retained string limits, ambiguous collections and revisions.
  These are stress inputs, not additional observations.
- `notice-app`: an isolated simulator fixture including the production source.
  It injects an unchanged captured report with the clock six minutes after its
  origin, exercising the real OS notice layout and quiet production bootstrap.
  It does not model publication latency and is never packaged in the catalog.

USGS government data are retained with the credit **U.S. Geological Survey**.
Mocks resolve paths relative to the file containing them. Native checks use
`apps/earthquakes/test/check.json` and the same shared mocks.
