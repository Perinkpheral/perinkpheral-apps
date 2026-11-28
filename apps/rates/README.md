# Exchange rates

Shows Bank of Russia exchange rates and history for up to eight currencies.
UP and DOWN switch currencies; OK toggles rotation. Refresh and Rotation
settings control their respective intervals. On HTTP 429, the application retains
the raw history request's Retry-After cooldown, including HTTP dates normalized
by Perinkpheral OS. Without a usable delay, or after any other completion, the
per-currency retry delay is 60 seconds.

A failed state restore preserves saved quotes, preferences and request deadlines
until a later load can read them. A failed history read skips the update; only
an absent file starts empty. A failed size query never permits overwriting or
extending the history file.
