# Bank of Russia metals

Shows the latest published prices and a month of history for Gold, Silver,
Platinum and Palladium. UP and DOWN switch metals; OK refreshes. Background
updates run every six hours. Raw requests retain their Retry-After cooldown,
including HTTP dates normalized by Perinkpheral OS.

A failed month or request-state restore preserves its saved value. Downloads
may still be displayed when the month is unreadable, but cannot replace it
until a later load successfully restores storage.
