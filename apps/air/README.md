# Air quality

Air quality from Open-Meteo and CAMS: European or US AQI, particles, UV and the
three strongest available irritants. Optional missing pollen readings do not
replace the other valid readings. Changing the index scale uses the cached data.

Choose a city, index scale and refresh interval (15 minutes, 30 minutes or one
hour). The city can also appear in the header. Perinkpheral OS schedules polls
at the configured interval and enforces server cooldowns. OK queues a refresh;
if a cooldown is active, the request waits for it to expire. Cooldowns do not
change the refresh setting or require application storage or session state.
The last valid readings remain visible after a network failure. A failed cache
write shows a storage error until saving succeeds.
