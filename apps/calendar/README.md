# Calendar

Shows upcoming events from up to four iCalendar (ICS) subscriptions. The device
downloads each feed directly from its provider. Reminders can appear over another application.

## Setting it up

If your calendar provider offers an ICS subscription or publishing URL, paste it
into **Calendar 1** on the device's page (Installed apps, Calendar settings).
Use **Calendar 2**, **Calendar 3** and **Calendar 4** for additional subscriptions.
Unused addresses can stay empty. Examples:

- **Google Calendar**: Settings, the calendar, "Secret address in iCal format".
- **iCloud**: share the calendar as a public calendar and copy its link.
- **Outlook**: Settings, Calendar, Shared calendars, Publish a calendar, the ICS link.
- **Nextcloud and others**: the calendar's "subscription" or "export" link.

The device page converts `webcal://` and `webcals://` links to `https://`.
Treat a private subscription URL as a secret: anyone with it can read the feed.

**Refresh** sets the download interval. **Reminder** sets how long before a timed
event to show a notice. All-day events have no reminders. Choose Off to disable reminders.

Accepted limitation: reminders are delivered at most once; queue refusal combined with both receipt stores refusing rollback through termination may lose a reminder.

## On the device

The list shows events for the next two weeks, with a name and time on each line.
Times appear as "15:00" today, "Tmrw 09:30", "Sat 12:00", "21 Nov" beyond a week,
or "Now" during an event. UP and DOWN select events. OK opens the selected event's
details; BACK returns to the list. With multiple sources, a caption at the bottom
names the event's calendar (`X-WR-CALNAME`, or Calendar 1-4 when unnamed).
Hold OK to refresh all configured feeds. A reminder shows
the event's time and name. OK or BACK dismisses it and restores the previous screen.

## What it reads

The parser supports timed and all-day events, UTC, floating times and time zones
defined in the feed, including daylight-saving rules. Recurrence supports daily,
weekly, monthly and yearly intervals, counts, end dates, weekdays, ordinal weekdays
such as the second Tuesday, exclusions, and moved or cancelled occurrences.
Responses are parsed incrementally.

Parser limits are six embedded time zones, 24 relevant exclusions per event,
32 occurrence overrides, 200 bytes per unfolded line and 63 bytes per event
title. Longer lines and titles are truncated. Recurrence expansion is limited
to 40,000
daily or weekly periods (4,000 for monthly or yearly rules). Larger `COUNT`
values clamp to 280,000, the maximum occurrences those loops can expand.
`INTERVAL` clamps to 3,660,000; expansion stops at year 9999. Invalid dates, times and
selectors are ignored. Duration totals beyond signed 32-bit seconds are ignored.

Recurrence uses `FREQ`, `INTERVAL`, `COUNT`, `UNTIL`, `BYDAY` and a single `BYMONTHDAY`.
Event rules do not implement `BYMONTH`, `BYWEEKNO`, `BYYEARDAY`, `BYHOUR`, `BYMINUTE`,
`BYSECOND`, `WKST` or `BYSETPOS`. Most unsupported parts are ignored. The presence of
`BYSETPOS` disables non-ordinal `BYDAY` expansion in monthly and yearly rules.
Supported parts may still affect the result. Each source retains its 24 nearest
events; the merged list shows the nearest 24 across all sources. Events with the
same UID and start appear once, using the first configured source in setting order.
Calendar names are limited to 127 UTF-8 bytes and truncated between characters.

Text uses the font selected in Perinkpheral OS and installed glyph packs. Missing
glyphs fall back to Perinkpheral OS's spelling rules; install the appropriate pack
for Cyrillic or Greek.

`test/test_ics.c` tests the parser on the host computer.
