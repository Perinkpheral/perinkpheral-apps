# Shared response samples

Fixed offline inputs used by simulator mocks and retained application design examples.
Response paths in a standalone `mocks.json` are relative to that file.

- `earthquakes/`: real USGS captures, explicit fault mutations and an isolated notice fixture.
- `iss.json`: one position response for ISS scenarios.
- `air-*.json`: Open-Meteo/CAMS examples retained with the air-quality design brief.
- `cbr-daily.json`, `metals/`: Bank of Russia reference responses.

The ISS map's source image belongs with the application at
[apps/iss/assets](../../apps/iss/assets/), not among network responses.
