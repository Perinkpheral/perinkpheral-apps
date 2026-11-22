# Keenetic offline tests

Run with wasi-sdk and the supplied OS checkout:

```sh
"$PERINKPHERAL/tools/perinkpheral" app-check apps --app keenetic
```

[check.json](../../apps/keenetic/test/check.json) supplies fixed settings, clock
and [local mocks](../../apps/keenetic/test/mocks.json). The capability checks
use a batch with one optional rate absent: background-only reads intentionally
do not add chart history, so their chart cannot be compared with a foreground
live sample. Dedicated logic regressions exercise full live rates, isolated
dots, cached reopening and OK/page alarm postponement at all three intervals.
[logic.c](../../apps/keenetic/test/logic.c) exercises application
callbacks against the SDK stand-in, including challenge/login, cookie renewal,
restart recovery, blocked sessions, unsolicited detail failures, cached rates,
chart spacing/gaps, checkpoint limits, Network delivery, PPPoE/IPoE, variable
link lists, device navigation and the shared list-buffer boundary.
[fixtures.c](../../apps/keenetic/test/fixtures.c) validates retained JSON inputs,
response links and headers, interface projections, DNS compositions, manifest
picker paths and method/body-sensitive scenario mocks.

The scenarios named `keenetic*` cover setup, challenge, refusal, batch errors,
Internet-down, device-only errors, and four-screen navigation in both languages
and fonts. Every independent hotspot poll has a mock; unmatched requests return
599. The device-only failure scenario keeps the Keenetic header and traffic
while displaying No contact on Devices.

`fixtures/router/` holds redacted inputs used by response, picker and application
assertions; unused investigation captures are removed. `fixtures/replay/`
contains synthetic authentication values and lengths recalculated for saved
bodies. Batch compositions contain system, interface statistics, Internet and
DNS in manifest order. `long-hosts-130.json` exceeds the 8 KiB list buffer with
long Cyrillic names. `v11-long-hosts.json` covers width fitting and unnamed rows.
Addresses are synthetic documentation ranges throughout; mapped LAN rows use
203.0.113.0/24, independently of the router endpoint at 192.0.2.1.

Scenario PNGs are the catalog's reference pictures. They were generated and
visually inspected using native application callbacks and production kit/gfx
rendering. This does not run the simulator or establish simulator parity.
Review-only pictures and working reports are kept outside the repository.
