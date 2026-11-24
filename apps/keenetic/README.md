# Keenetic

A four-screen dashboard for Keenetic 5.01: Traffic, System, Network and Devices.
English and Russian work with Departure Mono and Jersey fonts.
Version 0.4.10 requires Perinkpheral OS 0.1.95 or later. Failure and offline
scenarios can exhaust the interpreter arena on earlier firmware.

Set Router address, Username, Password, Refresh and WAN interface (default
`PPPoE0`). WAN port and radios accepts one to three interface names: the WAN
carrier first, followed by any radio interfaces. Defaults are `GigabitEthernet1`,
`WifiMaster0` and `WifiMaster1`. Names must contain 1–31 ASCII letters, digits,
underscores, hyphens or dots. Choose the WAN and carrier used by the router's
selected route. Both PPPoE and IPoE are supported; IPoE uses the batch gateway
when the interface has no PPP peer.

Short UP/DOWN changes screen, or scrolls one device while within the list.
A short press past either end of Devices leaves that screen. Long UP/DOWN
scrolls three devices and stops at the ends. OK refreshes; BACK uses OS navigation.

Traffic displays the router's `rxspeed`/`txspeed` in Mbit/s and occupied conntrack
slots. A rate appears on the first successful answer; reopening displays the
last cached rate and recent traffic points while refreshing; missing rates
show `-` independently. The chart's common-scale fill joins adjacent valid
samples. An isolated sample is a dot. It retains up to 30 observations from
the configured router, username and WAN, including real samples from the
existing 15-minute background reads. Page changes and repeated OK presses
update the numbers without compressing the selected sample spacing. Failed
or substantially late observations and restarts break the line. Foreground
gaps use 2.5 times the selected interval. Consecutive background reads at
15-minute cadence join, with a gap when a background interval is missed.
The horizontal spacing represents observation order. A gap separates
foreground and background observations when the interval is too long for
the foreground cadence. On upgrade an older snapshot can supply one real
pair of rates.

System shows CPU, RAM, uptime in hours and the higher available radio temperature.
Network shows interface, Internet reachability, WAN address, gateway, DNS, WAN
uptime in hours, speed and carrier. Pending fields show Loading; completed
missing fields show `-`. DNS remains available when the configured WAN does not
match the selected route. Devices shows the first 32 retained active addressed
rows in router order, with three two-line devices per screen. Missing names use
hostname fallback or Unknown / Без имени. A failed device refresh displays
No contact / Нет связи even when cached devices remain visible.

The header describes login and the main batch: Access denied when blocked,
No contact on login/batch failure, No Internet when the batch succeeds but
Internet is down, and Keenetic otherwise. Detail failures affect their page.

Visible refresh options are 1, 5 and 15 minutes. Background refresh runs every
15 minutes, matching the snapshot checkpoint interval. The router may expire
its session at about five idle minutes, so the 5- and 15-minute visible choices
can require login on each refresh. The app renews after 401, blocks repeated
refusal until OK or settings changes, and recovers interrupted logins after a
restart. An unconfigured app does not schedule recurring refreshes. Retry-After gates login and
read requests across OK, settings changes and instance replacement, using uptime
so a wall-clock edit cannot waive the deadline. The control-record migration
preserves an existing authentication refusal. Refused session saves are logged.

Only GET/POST `/auth` uses `http_send`; reads use four manifest polls: the
system/stat/Internet/DNS batch, selected WAN, carrier/radios and hotspot list.
The cookie is stored only when it changes. Non-secret readings and history
share a checkpoint at the first reading and every 15 minutes of successful
reads, or when leaving a scene with uncheckpointed graph changes. Very long
settings can leave room for fewer historical points in the shared store, in
which case the newest are kept. Passwords, cookies and login bodies are not logged.
HTTPS uses SDK certificate validation.

Hotspot columns share the SDK's 8192-byte list buffer. The application stops
before a possible partial boundary row, preserving names and IP identity for
complete-field responses. The count describes retained active rows and can be
lower than the full router count. Arbitrarily sparse columns cannot be safely
joined by the SDK; captured complete columns are the supported shape. Names
retain up to 63 UTF-8 bytes before kit width fitting.

[Offline tests and fixtures](../../tests/keenetic/README.md) describe validation.
