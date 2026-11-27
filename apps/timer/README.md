# Timer

UP and DOWN set 1–180 minutes; a long press changes five minutes. OK starts or
stops the countdown. It continues off screen, keeps the remaining minutes in the
header, and posts a completion notice. A device restart resets the countdown.

If saved state cannot be read, Timer displays the default duration and blocks
changes without overwriting the saved countdown or its pending notice. Reopening
the application retries the read; controls work again after a successful restore
or confirmation that no compatible state exists.

A write that reports an error is accepted only if reading it back confirms the
intended state. Otherwise the edit stays unacknowledged and can be retried.
