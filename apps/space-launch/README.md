# Space Launch

Orbital and suborbital launches from [Launch Library 2](https://thespacedevs.com/llapi) by
The Space Devs, and orbital launch counts since 1957. No account or key is needed.

## Screens

Held UP and DOWN switch the four screens; the count at the upper right, `1/4`, says
which one is shown.

1. **Launches.** The next ten launches with the local date, or the time for a launch today,
   the rocket and the status. UP moves into the past: the last ten launches with their
   results. The list opens at the first launch without a result. OK opens the launch's page:
   mission, rocket, date, local time, status, how soon, provider and launch site. UP and DOWN
   go to the previous and next launch there; BACK returns to the list.
2. **Countries.** This year's completed orbital launches by the provider's country, the bars
   scaled to the leader. Others is the year's total less the six countries.
3. **Months.** A year's launches by month, with the launches, successes and failures under the
   chart. UP and DOWN choose the year, 1957 to this one.
4. **History.** Orbital launches per year since 1957. UP and DOWN move the cursor; the line under
   the chart gives the chosen year's count.

Statuses are those of the library: Go, TBD, TBC, Hold, In flight, Success, Failure, Partial
failure, Payload deployed. A launch whose time passed without a result reads Awaiting result,
never Success. The time is shown to the precision the library gives: to the minute, to the
hour (`11:--`, Time unconfirmed), a day (Time unknown), a month (`Dec 2026`) or a year (`2027`). A launch
whose time moved by a minute or more says Schedule changed for a day. Unknown precision
never supplies a clock, countdown or reminder. Coarse dates retain the supplied calendar period.

## Settings

**Reminder**: Off (default), 15 min or 1 h before a launch. A reminder is a notification with
the local time and the mission. It comes for a launch that is Go and timed to the minute, from
a list taken within three hours, and once per launch: a launch whose time moves is not
reminded of again. A full notification queue is tried again after a minute while the launch is
less than five minutes late. Reminders are set again after a restart, a clock change and an
update.

## Requests

All requests go to `ll.thespacedevs.com`, version 2.3.0, through application-driven
`http_send`. There are no manifest polls. One request is outstanding at a time. The initial
upcoming/previous pair can complete together; subsequent requests are spaced five minutes
apart, including annual pages and country counts.

| Request | Address | Interval |
|---|---|---|
| Upcoming | `launches/upcoming/`, 10, `mode=normal`, including those of the last day | 30 min |
| Previous | `launches/previous/`, 10, `mode=normal` | 3 h |
| This year | `launches/previous/`, `mode=list`, completed (status 3, 4, 7), orbital, `year`; pages of 100 until `count` is covered | 1 day |
| Countries | the same, `limit=1`, `lsp__id` of each country's providers; the `count` field | in the annual round |
| Verification | the unfiltered annual count, `limit=1` | after all countries |

For a year with 271 launches this is about 66 requests a day, spread across the day. The free
service allows 15 requests an hour from one address, shared by every device and program behind
it. A 429 retains cached data and closes a persisted application-wide gate until `Retry-After`
passes (one hour when absent). Startup, reconnect, settings saves, queued pages and other
requests all use that gate. The initial twelve-request burst is gone. An interrupted raw
response is discarded and recovered after its request lease; failed writes are retried.
The header shows `Updated 19:20` once the upcoming list is 40 minutes old.

Annual results are staged separately from the displayed snapshot. Every page must contain
its expected number of records, match the requested year and report the same total. All six
country counts must complete in that round, and the final total must still match. Only then
are Months, History, Countries and Others published together. Failed or inconsistent rounds
retain the last complete snapshot. Missing history is shown as No data.

The library has no working country filter. The countries are lists of provider ids in the
hidden `lsp` setting, from the library's agencies with launches:

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

- `cache-a` and `cache-b` (about 4.5 KB each, two file blocks per bank): versioned,
  checksummed snapshots with generation numbers. Each retains both launch lists, published
  annual statistics, the staged round, list freshness and the preceding year. The inactive bank is written
  and verified before it becomes current; a damaged bank leaves the last valid one intact.
- Store: `state` (response statuses, scheduler deadlines and request/year binding, service
  cooldown, reminder reservations and the last 16 reminded launch IDs). A reminder reservation is committed before posting. Recovery
  consumes an interrupted reservation to prevent duplicate delivery; a definite queue
  rejection releases it for retry. Reminder IDs from the previous state format are retained.
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
`test/persistence.c` (torn banks, write retries and reminder reservations), `test/reminders.c`
and `test/screens.c` run with `app-check`. The simulator
scenarios `tests/space-launch-*` use the answers in
[tests/fixtures/space-launch](../../tests/fixtures/space-launch).
