# ISS

The station's current position and up to 90 minutes of observed positions.
A picked poll runs once a minute. OK also asks for an update. The visible position follows each good
fix. The track samples at five-minute intervals, and the app checkpoints the readings and track at
that pace and when the last request status changes. The newest visible fix
survives temporary unloads in scene memory. Old points expire by age, and the line breaks at the
date line and over gaps longer than ten minutes.

The 400×200 equirectangular map leaves room under it for altitude on the left and speed on
the right, with a 64-pixel gap between the two readings. Altitude is rounded to whole metres
from the API's kilometres; speed is in km/h. Saved version 1 readings migrate from kilometres
to metres and keep their track. Coordinates are rounded to tenths of a degree, with at least two integer digits
(for example, `04.2°`), in two small inverted badges over
the map. They stay 12 pixels below its top and 8 pixels from the outer edge: on the left
for western longitudes, on the right for eastern longitudes and the prime meridian.
Their size is fixed for the selected font and language, including the hemisphere labels
(`N`, `S`, `E`, `W` in English; `С.Ш`, `Ю.Ш`, `В.Д`, `З.Д` in Russian).
The crosshair's dashed axes intersect at the position. The screen is always light: day and
night are not shown.
The observed track uses thin four-pixel dashes with four-pixel gaps, continued across samples.

Perinkpheral OS's map service draws the world with solid ink land and paper sea.
The service requires firmware 0.1.94 or later; the build computes the package's minimum.
