# Exchange rates

Shows Bank of Russia exchange rates and history for up to eight currencies.
UP and DOWN switch currencies; OK toggles rotation. Refresh and Rotation
settings control their respective intervals. Raw history requests retain their
Retry-After cooldown, including HTTP dates normalized by Perinkpheral OS.

A failed state restore preserves saved quotes, preferences and request deadlines
until a later load can read them. A failed history read skips the update; only
an absent file starts empty. A failed size query never permits overwriting or
extending the history file.
