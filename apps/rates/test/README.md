# Failure paths

`refusal.c` requires a diagnostic when session allocation fails, for both host
errors, and verifies that an ordinary save is quiet. Navigation remains a scene
session; it is not written to flash at each switch.

`required.c` feeds daily JSON one byte at a time and requires invalid records,
IDs, values and nominals to leave both the good in-memory snapshot and the store
untouched. Never-seen codes absent from the provider can still be unavailable;
a previously available quote disappearing rejects the complete answer.

`storage.c` refuses fresh-response writes with both storage errors. The screen
reports the unsaved update; a repeated answer retries it, clears the warning and
survives a reload. The simulator's `rates-storage-*` scenarios fill the store to
exercise the same warning in both languages and fonts.

The exact `valid_responses` in `check.json` permit only a null optional `Name`
for each configured currency. The required quote and identity stay intact;
the unavailable localized name falls back to the currency code and RUB. This
is not an exception for null required fields or an entire mutation category.
