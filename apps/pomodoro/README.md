# Pomodoro

OK starts or pauses a phase; UP stops the cycle, DOWN skips a phase, and a long
OK resets it. Work and breaks continue off screen with a header status and phase
notices. A device restart resets the cycle, including paused phases.

Settings select the work duration (15, 25, 45 or 50 minutes), short break (5 or
10 minutes), long break (15, 20 or 30 minutes), and rounds before a long break
(2, 3 or 4). Active and paused phases keep the duration captured at their start.

If saved state cannot be read, Pomodoro shows a stopped preview and blocks
changes without overwriting the saved phase or pending notice. Reopening retries
the read; controls work again after a successful restore or confirmation that no
compatible state exists.

A write that reports an error is accepted only if reading it back confirms the
intended state. Otherwise the edit stays unacknowledged and can be retried.
