# Weather

Current weather and a three-day forecast from Open-Meteo. Choose a city, Celsius
or Fahrenheit, a refresh interval (15 minutes, 30 minutes, one hour or three
hours), and whether to show the city in the header.

Perinkpheral OS schedules polls and enforces server cooldowns. OK queues a
refresh, which waits until any active cooldown expires. The application keeps
no separate cooldown in storage or scene memory. The configured interval stays
unchanged. Cached forecasts use the forecast city's date and retain their last
valid data after network failures; expired days are left blank. A failed cache
write shows a storage error until saving succeeds.
