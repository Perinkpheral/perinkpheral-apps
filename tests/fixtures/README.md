# Shared response samples

Fixed offline inputs used by simulator mocks and retained application design examples.
Paths in `mocks.json` are relative to the scenario directory.

- `iss.json`: one position response for ISS scenarios.
- `air-*.json`: Open-Meteo/CAMS examples retained with the air-quality design brief.
- `cbr-daily.json`, `metals/`: Bank of Russia reference responses.
- `space-launch/`: Launch Library 2 answers cut to the fields Space Launch picks (upcoming and
  previous from ll.thespacedevs.com, UUID-ordered annual pages from lldev.thespacedevs.com, 2026-11-24),
  and the mocks of its scenarios: all answers, no network, 429.

The ISS map's source image belongs with the application at
[apps/iss/assets](../../apps/iss/assets/), not among network responses.
