# Space Launch

Orbital and suborbital launches from [Launch Library 2](https://thespacedevs.com/llapi) by
The Space Devs, and orbital launch counts since 1957. No account or key is needed.

## Screens

On a main screen, hold UP/DOWN to switch among four screens. Close launch details
with BACK first. The header shows a screen count such as `1/4` next to the
connection icon, before the `Updated` caption when it appears. The screen count
is hidden in details.

1. **Launches.** Up to ten upcoming or recent launches, plus up to ten earlier launches,
   with duplicates removed. Each row shows the local date, or the time for a launch today,
   the rocket and the status. UP moves into the earlier launches and their results.
   The list opens at the first launch without a result. OK opens the launch's page:
   mission, rocket, date, local time, status, how soon, provider and launch site. UP and DOWN
   go to the previous and next launch there; BACK returns to the list.
2. **Countries.** This year's completed orbital launches by the provider's country, the bars
   scaled to the leader. Others is the year's total less the six countries.
3. **Months.** A year's launches by month, with the launches, successes and failures under the
   chart. UP and DOWN choose the year, 1957 to this one.
4. **History.** Orbital launches per year since 1957. UP and DOWN move the cursor; the line under
   the chart gives the chosen year's count.

Statuses are those of the library: Go, TBD, TBC, Hold, In flight, Success, Failure, Partial
failure, Payload deployed. Go or TBC launches with hour-or-finer precision show
Awaiting result after their scheduled time. The time uses the library's precision:
to the minute, to the hour (`11:--`, Time unconfirmed), a day (Time unknown), a month
(`Dec 2026`) or a year (`2027`). A launch
with second/minute precision and no result shows Schedule changed for one day
after a time change of at least one minute. Unknown precision
never supplies a clock, countdown or reminder. Coarse dates retain the supplied calendar period.

## Settings

**Reminder**: Off (default), 15 min or 1 h before a launch. A reminder is a notification with
the local time and the mission. It comes for a launch that is Go and timed to the minute, from
a list taken within three hours. The last 16 delivered launch IDs suppress repeat
reminders, including after schedule changes. Delivery can repeat after an ID
leaves that history or if no delivery receipt becomes durable. A full notification
queue is retried once a minute, for up to five minutes after the reminder's
scheduled time, while the launch remains in the future and the list remains
fresh. Reminders are set again after a restart, a clock change and an update.

## Requests

All requests go to `ll.thespacedevs.com`, version 2.3.0, through three picked manifest
polls. One persisted binding is outstanding at a time, without an expiring clock lease.
Required header templates keep every other poll closed, including automatic reconnects.
The application closes the gate after a successful delivery and spaces new requests five
minutes apart. Installation followed by `started` in the same boot retains its queued binding; a new boot
clears it. The fetch alarm reschedules an outstanding poll under the same binding because a
background poll that cannot start may have no callback. Perinkpheral OS coalesces pending
polls; any queued repeat reads the closed gate after success. Due endpoints take turns, and
the scheduler waits an hour before requesting a failed endpoint again. Until another endpoint
is chosen, the failed binding remains ready for a Perinkpheral OS retry: a complete answer is accepted
immediately, including across an unload. A complete answer for the last binding is also
accepted during cooldown, without shortening the wait or enabling a new request.

| Request | Address | Interval |
|---|---|---|
| Upcoming | `launches/upcoming/`, 10, `mode=normal`, including those of the last day | 30 min |
| Previous | `launches/previous/`, 10, `mode=normal` | 3 h |
| This year | `launches/previous/`, completed orbital launches, `year`, ordered by UUID; pages of 10 until `count` is covered | 1 day |

For a year with 271 launches this is about 84 requests a day, spread across the day. The free
service permits 15 requests an hour from one address, shared by every device and program behind
it. Every 429, including a late response for another binding, persists a global cooldown and
closes every gate. `Retry-After` accepts delta seconds or an HTTP date; absence means an hour.
A late refusal does not release another outstanding request. Clock corrections rebase every
scheduler deadline and list freshness, including across restarts. Within one boot elapsed uptime
also protects the cooldown against forward jumps. The header shows `Updated 19:20` once the
upcoming list is 40 minutes old. The existing fetch alarm also updates the visible countdown
and freshness caption, without a separate foreground timer.

Annual results are staged separately from the displayed snapshot. Each page must have the
expected row count, year, total, completed statuses and strictly increasing UUIDs, including
across the page boundary. UUID ordering does not move a row when its NET is corrected.
Country counts are folded from these same rows, with unknown providers under Others.
Only the last complete page publishes Months, History and Countries together. Failed or
inconsistent rounds retain the preceding complete snapshot; paging has no 400-launch ceiling.
List responses must contain exactly the smaller of `count` and ten mandatory records;
missing whole rows invalidate the answer and preserve the cached list. On an offline start,
empty statistics show the known network failure while their own paced request is pending.
These checks prevent duplicate/missing boundary rows; the service provides no transactional
snapshot of fields edited while a round is being fetched. Missing History is shown as No data.

At year rollover, the preceding snapshot is retained with a year-specific Incomplete notice
on Months and History. History keeps the notice while that year is plotted, even when its
cursor selects another year. Packaged historical data supersedes the provisional snapshot
and removes the notice for that year.

The library has no working country filter. The countries are lists of provider ids in the
application's provider table, from the library's agencies with launches:

| Country | Providers |
|---|---|
| USA | SpaceX, ULA, Blue Origin, Rocket Lab, Northrop Grumman, Firefly, NASA, Relativity, Astra, ABL |
| China | CASC and its predecessors, CNSA, CASIC, ExPace, LandSpace, OneSpace, China Rocket, i-Space, Galactic Energy, CAS Space, Space Pioneer, Orienspace |
| Russia | Roscosmos, Russian Space Forces, Khrunichev, RKK Energiya, Progress, Aerospace Defence Forces, Polyot, Kosmotras |
| Europe | Arianespace, ESA, Avio, Isar Aerospace, PLD Space, HyImpulse, CNES |
| India | ISRO, Antrix, Skyroot |
| Japan | MHI, JAXA, Space One, Interstellar Technologies, ISAS |

A provider outside these lists counts under Others until the list is extended in an update.

## Kept data

- `cache-a` and `cache-b` (about 4.5 KB each, two file blocks per bank): checksummed,
  versioned snapshots with generation numbers. They retain both launch lists, published
  annual statistics, the staged round and the preceding year. An inactive bank is written
  and read back before becoming current; a damaged bank leaves the last valid one intact.
- Store: `state` (statuses, scheduling deadlines, boot/uptime anchors, request binding,
  cooldown, a list revision/freshness pair and the last 16 reminded launch IDs), poll gates,
  and annual URL parameters. Unchanged list payloads update freshness here without rewriting
  cache files. A mismatched revision prevents freshness following a fallback to an older list.
- `told`: a complemented delivery receipt, used when posting succeeded but saving the reminded
  ID failed. A rejected post or failed preflight never consumes an ID. Normal unloads run
  handlers to completion. A power loss before either receipt becomes durable, or
  failed receipt writes through termination, can repeat a reminder because
  notification delivery and application storage are separate operations.
- Cache and state formats are version 3. Incompatible stored data is discarded;
  launch data is fetched again.
- `files/history.bin`: orbital launches per month and failures per year, 1957 to 2025: two
  `uint16` (first year, count of years), then per year twelve `uint16` monthly counts and a
  `uint16` count of failures, little-endian. The same numbers are in
  [assets/history.tsv](assets/history.tsv).

## Sources

- Launches, this year's counts: [Launch Library 2](https://thespacedevs.com/llapi), The Space
  Devs, used under its free tier.
- 1957 to 2025: [GCAT](https://planet4589.org/space/gcat/) by Jonathan C. McDowell,
  `launch.tsv` of 2026-11-23, licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
  A launch is counted when its `LaunchCode` begins with `OS` (orbital, success) or `OF` (orbital,
  failure); pad aborts and explosions on the pad (`OA`, `OE`) are not. Months are those of the
  UTC launch date.

The current year counts what the library records as completed orbital launches, so the last
point of History and the current year's months are the year so far and may differ slightly from
GCAT's count.

## Tests

`test/network.c` (validation, global cooldown, complete paging beyond 400 and year bindings),
`test/persistence.c` (torn banks, write retries, rejected posts and delivery receipts), `test/reminders.c`
and `test/screens.c` run with the unmodified SDK's `app-check apps`. The simulator
scenarios `tests/space-launch-*` use the answers in
[tests/fixtures/space-launch](../../tests/fixtures/space-launch).

The SDK's generic network checks require a complete answer after any failure to show
exactly what that answer alone would show. `test/network.c` also compares frames after
transport failures, incomplete picked JSON, HTTP errors and 429, on screen and across
an unload, without advancing time or manufacturing a new request binding. It verifies
that accepting the answer does not bypass the scheduler or clear a global cooldown.
Other regressions cover delayed responses, clock corrections, endpoint fairness,
complete annual paging, global cooldown and unchanged payloads.
`test/list_validation.c`, `test/rollover.c`, `test/offline.c` and `test/unknown_time.c`
cover incomplete lists, year rollover, offline startup and unknown launch-time precision.
The offline and rollover scenarios cover both languages and fonts, including unknown-time
details with a single uncertainty caption.

Historical measurements for Space Launch 0.1.2 on 2026-11-24 used the former
64 KiB linear-memory/80 KiB interpreter layout. They do not measure current
builds, whose linear memory is sized to stack and data, at most 64 KiB, within a
shared 112 KiB application area.

The 32-bit QEMU arena harness measured a 73,540-byte (71.82 KiB) interpreter peak across all
15 simulator scenarios and six supplementary paths, with no failed allocations in the
80 KiB arena. The compile-all probe also measured 73,540 bytes, 188 bytes below the 72 KiB
limit used for that measurement. That module required firmware 0.1.95.
[Measurement details and hashes](test/arena.json) record the harness, scenarios and limitations.
