# SDK network recovery compatibility

Run `tools/app-check apps` with `PERINKPHERAL` and `WASI_SDK_PATH` set as for
`build.sh`. It invokes the SDK's `app-check`, including every capability and each
application's own tests, using a temporary SDK view. Only `sdk/check/checks.c` is
patched. The SDK checkout, compiler, application sources and packages are unchanged.
An incompatible upstream checker fails patch application instead of skipping checks.
Remove this shim when the SDK supports bounded recovery itself.

`test/check.json` may set `network_recovery_seconds` from 0 to 86400. Zero or an
absent key keeps the SDK's immediate recovery checks. Space Launch uses 7200 seconds
to cover its one-hour endpoint backoff and rotating five-minute request slots.

For the recovery assertions after each network failure, an interrupted response,
HTTP 503, 404 and 304, the checker advances uptime and wall time together. It fires
due alarms and foreground timers, offers successful mock answers every minute to
polls with ready templates, and services explicit requests. Closed polls receive
no artificial callbacks during that window: background preparation failures need
not load the application on the device. Background deliveries still load and unload
a separate instance for each event.

The final frame must match a successful control run advanced through the same
window. Both foreground and background recovery have their own control. The
fragmentation, partial-response, unchanged-answer, persistence, drawing and lifecycle
checks retain their original expectations. No check is disabled.

`tools/checks/test-network-recovery` runs a small gated-poll specimen. Its two-minute
backoff must pass with a three-minute window, fail under the default immediate
contract, and fail with the window when retrying is deliberately broken. CI runs
this regression before checking the catalog.
