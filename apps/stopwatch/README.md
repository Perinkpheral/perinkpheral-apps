# Stopwatch

OK starts, stops or resumes the stopwatch. A long OK resets a stopped stopwatch.
It continues off screen and shows elapsed hours and minutes in the header.
Uptime measures runs within one boot; a saved wall-clock reference allows recovery
after a restart when the clock is known. A run with no saved wall-clock reference
stops on restart, retaining time banked by earlier runs.

If saved state cannot be read, Stopwatch displays zero and blocks controls without
overwriting the saved run. Reopening retries the read; controls work again after
a successful restore or confirmation that no compatible state exists.

A write that reports an error is accepted only if reading it back confirms the
intended state. Otherwise the edit stays unacknowledged and can be retried.
