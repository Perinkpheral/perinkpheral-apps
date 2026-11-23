# Earthquake scenarios

Shared mocks contain real, cut-down USGS answers from
`../fixtures/earthquakes/`. The twenty `earthquakes-*` scenarios cover map,
Nearby/Worldwide lists, event pages, older pages, empty data, offline data,
missing setup and system notices in en/ru with Departure/Jersey (80 pictures).
Buttons are UP=1, OK=2, DOWN=3 and BACK=4.

The notice scenarios use the isolated fixture application; production itself
keeps bootstrap silent. Regenerate intended Earthquake baselines, inspect them,
then run the full catalog suite without `--update`.
