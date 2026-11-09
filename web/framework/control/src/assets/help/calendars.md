# Shift calendars

A plant does not run around the clock. A **calendar** says when it is planned to run: a time zone, the start of its production day, its shifts, and the holidays and extra days laid over them.

Calendars belong to the application server and are shared. Every gateway that connects to it plans by the same ones.

They are edited under [Applications](/apps), on the **Calendars** tab of the application server's page. On a hub that is the hub's own page.

## Properties

Press **Add** on the Calendars tab, name the calendar, and set:

- **Time zone** - the plant's wall clock. A new calendar starts on the server's own zone.
- **Day start** - when the production day begins.

Saving a new calendar makes its first pattern, ready for its shifts.

Shift times are wall-clock times in the zone, so a shift that starts at 06:00 starts at 06:00 on both sides of a clock change. The shift that spans the change is an hour short or long, as the crew's night was.

A value the server refuses, a zone it does not know, shows under the form.

### The production day

With a day start of 06:00, Monday's production day runs from 06:00 Monday to 06:00 Tuesday.

A shift belongs to the day it starts on. A night shift that starts at 22:00 Monday is Monday's, though most of it falls on Tuesday.

Every date on these pages is a production day, and so is a day in a report.

Changing the zone or the day start keeps each pattern's date and each exception at the local time it was entered as. Shifts are held from the day's start, so moving the day start moves every shift with it.

## Pattern

A **pattern** is the calendar's shifts from a date. It is in effect from that date until the next pattern's. The first pattern has no date: it is in effect from the beginning.

The **Pattern** tab lists the patterns, with the one in effect today selected. Pick another to see its shifts.

### Shifts

**Add shift** and the edit button open the shift's form:

- **Name** - *Day*, *Night*. A name is used once in a pattern.
- **Days** - the days of the cycle the shift runs.
- **Start** and **End** - on the calendar's wall clock. An end at or before the start is the next day's, so the same time twice is 24 hours.
- **Breaks** - each a start and a length in minutes, inside the shift.

A pattern's shifts may not overlap, since the shift at any instant must have one answer. That includes a shift that runs past the day's end into the next day's first shift. The server refuses an overlap and the form says which two shifts meet.

Each save is applied at once, on every gateway.

The remove button takes a shift out for good. A shift has no restore.

### The cycle

A pattern's shifts repeat over its **cycle**.

The default cycle is the week, Monday to Sunday. The **Days** column shows the week's letters, with a dot for a day off: `M T W T F · ·`.

A rotation is a cycle of another length, from 1 to 31 days, anchored on a date that was its first day. A 2-2-3 pattern is 14 days and four-on-four-off is 8. Its days are numbered from 1, and the column shows the numbers: `1-2, 5-7`.

A rotation counts in dates, not in hours, so a clock change does not shift it.

### Pattern changes

A change of hours from a date is a **new pattern** from that date. Last month's figures are then still computed under last month's shifts.

A correction is an **edit** of the pattern in effect. It changes past reports as well, at their next read.

A new pattern is made as a copy of an existing one from a date, with the `copyCalendarPattern` mutation on the application server's `/graphql`. The copy then shows in the list for its shifts to be edited.

### Deleting a pattern

The delete button deletes a pattern, and the pattern before it applies again from its date. A calendar's only pattern cannot be deleted.

**Show deleted** lists the deleted patterns too, each with **Restore** and **Purge**.

A deleted pattern keeps its date until it is purged. A new pattern on that date is refused until the old one is purged or restored.

## Exceptions

An **exception** is a dated range that changes planned time:

- **Closed** - a holiday or a shutdown. Nothing is planned in the range, and a shift it cuts into is shortened.
- **Open** - an extra day, an extra Saturday. The range is planned time with no shift, under the exception's name.

Where a closed and an open exception meet, closed wins.

**Add exception** and the edit button open its form. The range is entered one of two ways:

- **Whole days** - a first and a last day. Each is a production day, from its day start to the next day's.
- A **start** and an **end** to the minute, for a half day.

Both are the calendar's wall clock, whatever zone your browser is in. The line under the fields shows the range as it will be saved.

The delete button deletes an exception. **Show deleted** lists the deleted ones, each with **Restore** and **Purge**.

## Deleting a calendar

**Delete** and **Purge** ask first, because calendars are shared. The application server cannot see which machines plan by a calendar.

A deleted calendar leaves every gateway. A machine on it is planned around the clock until it is pointed at another calendar.

A deleted calendar is restored from the Calendars tab's *Deleted* view. **Purge** removes it for good, with its patterns, shifts and exceptions.

## Access

The four tables are resources of the application server on the [Resources](/access/resources) page: `calendars`, `calendarPatterns`, `calendarShifts` and `calendarExceptions`.

Unenforced, anyone may read and edit them. Once one is enforced, reading it needs **Read** and editing it **Create**, **Update**, **Delete** or **Purge**.

Adding a calendar makes its first pattern, so it needs **Create** on `calendarPatterns` too. Purging a calendar or a pattern purges its children, and needs **Purge** on their tables.

A stand-alone gateway reads the calendars as its own user. A site that enforces the tables grants that user **Read**, or the gateway holds no calendar. A hub needs no grant.