# OEE

Overall equipment effectiveness is the share of its planned time a machine spent making good parts at its ideal rate. It is three factors multiplied:

- **Availability** - the time it ran over the time it was planned to
- **Performance** - the parts it made at the ideal cycle time over the time it ran
- **Quality** - the good parts over all parts

The gateway works it out from what its historian already keeps. A **machine** is a [historian group](/help/gateways/groups) whose members are given roles: its run state, its counts, its stop reason. The gateway turns the group's records into stops and per-interval rows, and computes the figures when they are read.

Machines and reasons are edited from the gateway's page under [Applications](/apps), on its **Machines** and **Reasons** tabs.

OEE runs on a gateway whose settings have an `oee` block and a historian.

## Machines

Press **Add** on the Machines tab. A machine's **Properties** tab holds, under its name:

- **Count unit** - what a count is, such as bottles or cases
- **Small stop** - a stop shorter than this is a small stop, which lowers performance, not availability. In milliseconds: 300000 is five minutes.
- **Idle timeout** - for a machine with no State signal: how long without a count before it is stopped. In milliseconds.
- **Group** - the historian group whose members are the machine's signals. A group has one machine, and the group is fixed once the machine is saved.
- **Ideal** - how fast the machine makes a unit at its best, as a cycle time in seconds or a rate a minute. Either is stored as the cycle. Blank measures no performance.
- **Interval** - the width of the rows the gateway writes: 1, 5, 15 or 30 minutes. A change takes effect at the next boundary.
- **Calendar** - the [shift calendar](/help/calendars) the machine is planned by, with a link to its page. With none, the machine is planned around the clock.

A save the gateway refuses says why under the form.

### Signals

Once the machine is saved, **Signals** gives the group's members their roles:

- **State** - running or stopped. A Boolean runs on true. For a code, type the values that mean running, such as `2, 3`.
- **Total** - a counter of every unit made
- **Rate** - a rate the gateway integrates into the count, with its time unit. A machine has a Total or a Rate, not both.
- **Good** and **Reject** - counters. With one of them, quality is measured.
- **Reason** - the stop's reason code, which a reason's **Code** names

A machine needs a State signal or a count. With no State signal, its counts are its state, and it needs an idle timeout.

Each pick applies as it is made. The gateway starts reading the group again a few seconds after the last one.

A pick the gateway refuses is said under the list, and the select goes back.

A warning under a role is the gateway's:

- A count member with a deviation band stores a change late, so a count lands in a later interval. Clear the band on the group's **Members** tab.
- A member with a minimum interval longer than the gateway waits for an interval's records stores each value late.

The line above the roles says how the machine's engine stands: live, starting, waiting, or why it can't run.

### Recompute

The figures use the calendar, the reasons, the ideal and the small-stop threshold as they are when read. Changing one changes past reports with no recompute.

A State signal's running values, a Rate's time unit, and the idle timeout of a machine with no State signal are different: the rows already written were made with the old value. The page says so when one changes.

**Recompute**, on the Signals tab, makes the machine's rows over a range again from the group's history. It answers once the range is queued. It needs Update on the machines and Read on the group.

### Deleted machines

A deleted machine keeps its rows and still holds its group, so the group can't be deleted or purged. **Purge** removes the machine with its stops, intervals, codings and signals.

## Reasons

A reason is what a stop is coded with. One list serves every machine of the gateway.

Press **Add** on the Reasons tab. A reason has:

- **Category** - *Unplanned* counts against availability. *Planned* is a planned stop, such as a changeover. *Excluded* takes the stop's time out of planned time, such as no orders.
- **Code** - the value a machine's Reason signal reports for it. A stop that begins with that value takes the reason. Blank for a reason only operators pick.

A code names one reason, deleted reasons included.

A deleted reason keeps resolving the stops it coded and leaves the pickers. One that a coding names can't be purged.

## Access

The machines and reasons are tables of the gateway, switched on the [Resources](/access/resources) page as its other tables are.

A machine's figures and stops need Read on its group, as the group's history does.