# Application tests

Run from the repository root with Java 21 or later, wasi-sdk (`WASI_SDK_PATH`), a C
compiler and Make. The commands below use the firmware tools from a checkout at
`../perinkpheral-os`.

```sh
../perinkpheral-os/tools/perinkpheral app-check apps
../perinkpheral-os/tools/perinkpheral sim-test tests
```

Both commands discover inputs from their folders; no test-runner application
registry is maintained.

## app-check: every application against the contract

`app-check apps` builds each WebAssembly package, then compiles applicable native
checks against the SDK's stand-in host and runs them under AddressSanitizer and
UndefinedBehaviorSanitizer. Applications with `"system": true` skip native checks.

| Capability | Runs on an application that | Checks |
|---|---|---|
| `sessions` | any non-system application | Restoring a stacked scene preserves its frame and BACK navigation depth. Opening from the menu discards session state and restores the application's stored startup state. Navigation depth must match startup, and repeated opens must agree. Restart and empty-store startup are also checked. |
| `store` | calls `kv_set` or `file_write` | KV values are limited to 1 KiB, with 2 KiB per application including settings and storage overhead. Applications that write files have a separate 32 KiB allowance, including packaged data files, counted in 4 KiB blocks. Checks use maximum-length settings. Unchanged replies, repeated failures and unchanged visits cause no new writes. Refused writes must remain retryable without crashes or loops. |
| `timers` | calls `timer_set` or `alarm_set*` | A timer delivered 5 s late or twice leaves the same pending timers as an on-time delivery. Focus restores timers after unload. Clock changes of one day in either direction preserve timers. Alarms work on and off screen and are restored by `app_on_updated()`. |
| `network` | has `polls` or calls `http_send` | Responses in pieces of 1, 7 and 1000 bytes produce the same frame as a complete response. Partial replies interrupted by unload or `HOST_DATA_FAILED` preserve cached data. A complete response after a failure status or HTTP 503, 404 or 304 produces the same frame as that response alone. Background replies remain available on return. |
| `drawing` | exports `app_on_draw` | Every reached state draws a frame without refused drawing calls. Drawing occurs only in `app_on_draw()`. |
| `notices` | calls `notification_post` | Checks queue refusal, delivery receipts and replay behavior. |
| `clock` | calls `time_*` | Checks clock changes, date transitions and declared event-time probes. |

Native checks compare recorded drawing calls, their arguments, header state and
BACK navigation depth. The clock advances only when a check requests it.

Each report line identifies an application and capability: `ok`, `FAIL` with
expected and observed results, or `-` for an inapplicable capability.
`--app <name>` and `--only <capability>` narrow a run.

### Why a check fails

- **sessions, another picture after the unload**: session state omits a selected
  page, row or partially edited value. Save scene state with `app_session_save`
  in `app_on_blur()` and restore it in `app_on_focus()`.
- **store, written N times**: a handler writes a changing count or timestamp for
  unchanged data. Compare against stored state, including in `app_on_terminate()`.
- **store, not saved when the same answer comes again**: a refused write remains
  only in memory. Track failed saves separately from data changes and retry them.
- **timers, set again in app_on_focus()**: focus does not restore a pending timer.
  Timers stop when the application leaves the screen.
- **network, an answer that stops**: partial data is committed before `HOST_DATA_LAST`.
- **network, drawn anew another picture**: a handler changes visible state without
  calling `screen_redraw()`.
- **network, http_send() answered HOST_ERR_ARGUMENT**: the request goes to a host `hosts` does not
  list, or its headers are malformed LF-separated "Name: value" lines or exceed
  four headers; Perinkpheral OS refuses it when it is queued.
- **network, off the screen another picture**: response handling depends on memory
  from the requesting instance. Each background delivery uses a separate instance.
- **stopped in a handler** with a sanitizer's line: a read or write outside an array, an
  overflow, a shift too far. The line names the source.
- **did not return in 3 s**: the native handler exceeded the three-second watchdog;
  it may be slow or unbounded. Device handlers have a cooperative 3000 ms budget,
  checked at interpreter yields, after imports and on return.

### Check inputs

`apps/<name>/test/check.json` configures test time, settings and mock responses.
It uses the corresponding simulator scenario keys:

```json
{
  "at": 1794832230,
  "tz": "Europe/Moscow",
  "store": { "city": "Saint Petersburg", "lat": "59.94", "lon": "30.31" },
  "mocks": "../../../tests/weather/mocks.json"
}
```

`mocks` is an address-to-response object or a path to a file containing that
object. Native checks and simulator scenarios can share the file. Requests
without a matching mock receive `HOST_FAILED_NO_ANSWER`, which is reported by the
runner. Use `store` to make random or timing-dependent startup state repeatable,
as Life does with its seed.

### Application logic

`apps/<name>/test/*.c` tests application-specific calculations, parsers and row
ordering. `app-check` compiles each file with the same sanitizer flags and
stand-in host as the capability checks, then runs it in its test folder. Exit 0
passes and is reported as `logic <file>`. Tests can include application source
(`#include "../life.c"`), call its handlers and inspect its state and
`standin()` from the firmware repository's `sdk/check/standin.h`. Shared
capability checks do not require duplicate application-specific tests.

## sim-test: the pictures

Simulator scenarios run real WASM applications with local mock responses and compare
frames pixel by pixel. A `mocks.json` file names response files relative to itself;
shared inputs live in [fixtures](fixtures/README.md). Keep those paths valid when
moving a scenario or response: `apps/<name>/test/check.json` names the same files.

Run `sim-test tests --update` only for an intended rendering change and inspect each
changed PNG. Then repeat without `--update`; never regenerate baselines merely to
silence a mismatch. English/Russian and Departure/Jersey variants cover the recent
applications. The normal simulator runner needs permission to listen on localhost.

`app-check` executes native C under sanitizers; `sim-test` executes WebAssembly in
the firmware simulator and compares rendered frames pixel by pixel.

CI runs both with offline fixtures. Passing checks do not establish panel timing, real
button behavior, network endurance or battery life. Those belong to the
[firmware's hardware qualification](https://github.com/Perinkpheral/perinkpheral-os/blob/main/docs/testing/stability.md).

Storage failure scenarios cover Air, Calendar, Earthquakes, HA, Rates, Stocks and
Weather in both languages and font families. Their notices use "Storage error."
and its standard Russian translation. Warning notices split the Russian message
across two main lines. Keenetic does not display a failed-write notice.
