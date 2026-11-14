# Flows

A **flow** is a graph of nodes on a canvas. A message enters at a source, passes along the wires through processing nodes, and ends at a sink:

- a **source** - a node's data change, a historian group's records, a timer, a shift boundary
- a **sink** - a write to a node, a derived variable, a log line, an HTTP request

The shape is Node-RED's, and a Function body written for Node-RED runs here as it is.

Two things come with it that Node-RED does not have:

- Every message's path is recorded. The canvas shows what a flow is doing now, and what it did at any past time.
- A flow runs over the historian's records as readily as over live values. It can be checked against last week before it is let loose on the line.

A flow monitors, derives, forwards and alerts. Safety, interlocks and control logic stay in the PLC.

Flows are edited from the gateway's page under [Applications](/apps), on its **Flows** tab. They run on a gateway whose settings have a `flows` block; see [Settings](#settings).

To start from a working flow, import [the sample](#the-sample).

## The Flows tab

The tab lists the gateway's flows. Above the list:

- **Create** asks a name, a slug and a description, makes the flow and opens its page.
- **Import** reads a flow's json, from a file or pasted: a flow exported from the editor, or a bare graph, `{ "nodes": [ … ] }`.

The slug is the flow's url segment and names its trace's directory. It never changes.

An import makes the flow and opens the editor with the graph on the canvas, **not deployed**. Nothing of a file runs until it is read and deployed.

What the gateway would refuse of an imported graph is said in the dialog, and does not stop the import. The editor marks the node.

An import is held by the page until its first deploy. Reloading the page before then loses it.

A Node-RED export, a list of nodes, is not imported yet.

## The message

A message is a json object. The gateway sets:

- `_id` - a number unique within the gateway
- `_parent` - the id of the message this one was cloned or split from. A source's message has none.
- `_time` - when the source made it
- `topic` - text. A data change's is its node id, a timer's its name.
- `payload` - the value

A data change adds `node`, the connection's slug and the node's id, `status`, the status code, and `sourceTime` and `serverTime`.

Anything else on a message is the flow's own.

An output with several wires sends the message down the first and a clone down each of the others. A clone has a new `_id`, with the original's as its `_parent`.

A flow handles one message at one node at a time, in the order they came. Flows run beside each other.

Each node's input holds up to `queue` messages, 1000 by default. Past that the oldest is dropped, and counted on the node.

## Nodes

The editor's palette lists them in three groups.

### Sources

- **opc in** - a node of a connection. Each data change is a message. It takes the [watch list's parameters](/help/gateways#watching): the sampling interval, the queue and the data change filter.
- **group in** - a [historian group](/help/gateways/groups)'s records from a time on, then each record as it is stored. See [Restarts](#restarts).
- **timer** - a tick every so often, as `PT30S`, or at local times of day on chosen weekdays
- **calendar** - a [shift calendar](/help/calendars)'s boundaries: shift start, shift end, break start, break end
- **connection** - a connection's loss and its return
- **inject** - a message sent by hand, from the node's button
- **link in** - what a **link out** of its name sends, from this flow or another

An **opc in** or an **opc out** names its node by id, `ns=2;s=Pump1.Rpm`, or by its namespace's URI, which holds on any server with the model: `nsu=urn:…;s=Pump1.Rpm`, or as json, `{"nsu":"urn:…","s":"Pump1.Rpm"}`.

A data change is a change: a value that holds still sends nothing.

An **opc in** shares its node's item with the historian and with other flows, under the watch list's rule: strictest wins.

When the connection is lost, an **opc in** sends one message with the status `Bad_NoCommunication`. Its next message is the first value after the return.

### Processing

- **switch** - routes a message by rules over a property, an output for each rule. Nothing is coerced: text is never a number.
- **change** - sets, deletes or moves properties and context values
- **filter** - passes a message whose value changed, or changed by more than a band
- **range** - scales a number from one range to another
- **delay** - holds each message for a time, or limits their rate
- **trigger** - sends a value when a message arrives and another after a wait, unless reset. With **Each message starts the wait again** it is a watchdog: *no value for 30 s*.
- **window** - a window's count, sum, min, max, mean, first, last and delta, sent at its end. By duration or by count, tumbling or sliding.
- **split** and **join** - an array or an object as a sequence of messages, and back
- **template** - text from the message's properties, `{{payload}}`
- **function** - JavaScript on the message. See [The Function node](#the-function-node).
- **debug** - marks a point for the debug panel. With a level, each message goes to the log too.
- **catch**, **status** and **complete** - see [Errors and status](#errors-and-status)
- **link out** - sends to the **link in** of its name
- **comment** - text on the canvas

A message with a `reset` property empties a **window** and disarms a **trigger**, and goes no further.

A template takes Mustache variables alone: `{{payload}}`, `{{payload.mean}}`, `{{flow.limit}}` for a context value. It has no sections or partials.

### Sinks

- **opc out** - writes a node and waits for the server's answer. The answer's status goes on from its output. Each write is logged with its flow, node and answer.
- **variable** - a derived variable. See [Derived variables](#derived-variables).
- **log** - a line in the gateway's log, from a template, at a level
- **http request** - a request to an allowed host. Its response goes on from its output.

An **http request** reaches only the hosts listed under `gateway.flows.http.allow` in the gateway's settings. The list is empty as installed, so no request is sent until an administrator names a host.

The host is checked at each request and at each redirect. A deploy also refuses a node whose **Url** names a host the list lacks.

## The Function node

A **function** node runs JavaScript on each message. Its **On message** field is the body of an `async` function of `msg`, so a body may `await`.

What the body returns goes on:

- `return msg;` - the message, from the first output
- an array - one entry for each output. An entry that is itself an array sends several messages.
- `null` - nothing

The API is Node-RED's:

- `node.send( msg )` sends from inside the body, and `node.done()` finishes the message after an asynchronous send.
- `node.error( err, msg )` raises an error a **catch** node receives.
- `node.warn( text )` and `node.log( text )` write to the gateway's log.
- `node.status( { fill, shape, text } )` sets the badge under the node.
- `context`, `flow` and `global` each have `get`, `set` and `keys`, for the node's, the flow's and the gateway's values. See [Context](#context).
- `env.get( name )` answers what the gateway's settings list under `gateway.flows.env`, and `NR_NODE_ID`, `NR_NODE_NAME` and `NR_FLOW_NAME`. It never reads the gateway's own environment.
- `RED.util.cloneMessage( msg )` copies a message.
- `setTimeout`, `setInterval` and `Date` run on the flow's clock.

**On start** and **On stop** run when the flow starts and stops.

A body has no `import` and no `require`, and reaches no file, socket or process. It reaches the plant only through the flow's sinks.

A number too large for JavaScript to hold exactly crosses as a `BigInt`.

### Limits

A flow's Function nodes share one sandbox, with the limits under `gateway.flows.function` in the settings:

- **memory** - 16 MiB
- **stack** - 256 KiB
- **timeout** - a tenth of a second for one call

A body past a limit ends with an error, which a **catch** node receives. A runaway body costs its own flow and no other.

The timeout is a guard, not a cap. The gateway checks it between the body's steps, so one long built-in call, such as `JSON.parse` of a huge text, runs to its end first. The memory and stack limits bound what such a call can do.

A body whose promise never settles holds its message. A node holds up to `queue` of them, then drops the oldest.

## The flow's page

A flow's page has seven tabs.

### Editor

Drag a node from the palette onto the canvas. Drag from a node's output port to another node to wire them.

Select a node for its form at the side. A field left blank takes its default, which the placeholder names.

A node at fault carries a warning, and its form says why. It does not stop a deploy: the gateway decides.

Above the canvas:

- **Deploy** asks for a comment, stores the canvas as the next version and starts the flow from it.
- **Revert** puts the deployed version back on the canvas.
- **Disable** stops the flow at once, and **Enable** starts it from its current version.
- **Export** writes the canvas as the json file **Import** reads.

A deploy restarts this flow alone. Its nodes stop, the messages in flight are dropped and counted, and the new graph starts.

A deploy the gateway refuses says why above the canvas, and marks and selects the node it names.

An **inject** node's button sends its message. It is live for the nodes of the version that is running.

### Activity

The canvas, read-only and live while the tab is shown:

- A wire's width follows its rate. Its label is the rate a second, or its count once it is quiet. A wire carrying messages has a moving dash.
- A node's status is a badge under it. A node that has raised an error is drawn in the error colour, with its drops and errors counted beside it.
- Select a wire for its last message, or a node for its figures and its last error.

The **debug panel** beside it lists the trace's records as the flow makes them, newest first. Filter it by node and by kind, pause it, clear it, and open a row for its message.

**Hops** asks the gateway for those records. A flow at the `counts` or `off` level sends none, and one at `errors` sends a failing message's path.

A flow that is not running has no activity, and the tab says why the subscription ended. **Subscribe again** asks anew.

### History

The canvas over a past range:

- **From** and **To**, or **Hour**, **Day** and **Week** for the last of each
- **Trace** - the live flow's, or a replay's

Each wire shows how many messages it carried over the range.

The scrubber under the canvas picks a time. The canvas draws the version that was running then, and each deploy in the range is marked on it.

The message list reads the range's records, oldest first, by node, kind or message id. **Load more** reads the next page.

Select a record to light its message's path on the canvas, with its ancestors' and descendants' dashed. The panel lists the message at each node where a node changed it.

**Play** steps the range's records through the canvas from the scrubber's time, at 1, 10 or 100 times.

### Versions

Each deploy is a version, kept with who deployed it, when and why.

- **Open in the editor** puts a version's graph back on the canvas, where **Deploy** makes it the next version.
- **Compare** sets two versions side by side by node: added, removed and changed, with what changed.
- **Replay** runs the current version over history. See [Replay](#replay).

### Context

**Context** lists what the flow's nodes keep between messages, in three scopes:

- a node's own values
- the flow's, which its nodes share
- the gateway's, which every flow shares

A Function body's `context.set`, `flow.set` and `global.set` make them, as does a **group in** node's mark.

The values are saved, so a counter survives a restart.

**Add a value** and the edit button set a value, as json. A running flow reads the new value at its next `get`. **Remove** takes the value away.

### Properties

**Properties** holds the flow's name, its description and its **Trace** level, with **Delete**, **Restore** and **Purge**.

A deleted flow stops, and keeps its versions and its trace. **Purge** removes it with its versions, its replays, its context values and its trace.

## Errors and status

A node that fails a message raises an error: a write the server refused, a value that does not convert, a Function body that threw or ran out of time.

A **catch** node receives the message with `error`, the error's text and the node it came from. A catch may take every node's errors, chosen nodes', or only those no other catch takes.

A message that has been caught nine times is dropped, so a flow that feeds its errors back into itself ends.

An error no catch takes is logged, counted on its node, and the message ends there.

A **status** node receives each change of a node's status. A **complete** node receives each message a chosen node finishes.

Every error and status change is on the Activity tab as it happens, and in the trace.

## Trace

The trace is the record of what a flow did. It is what the History tab reads.

Each flow has a level, **Trace** on its Properties tab. Blank takes the gateway's `trace` setting, which is `hops` as installed:

- `off` - nothing is written. The Activity tab still counts.
- `counts` - a minute's counts for each node
- `errors` - the counts, and the whole path of each message that ends in an error
- `hops` - everything: a record for each node a message passes, with the message where a node changed it

Changing the level restarts the flow.

### What it costs

A hop record is some fifty bytes without the message's json. A message writes one at its source and one at each node it passes.

So a flow passing a hundred messages a second through five nodes writes six hundred records a second. That is thirty kilobytes a second, or two and a half gigabytes a day.

Two settings bound it, across every flow of the gateway:

- `retention` - 30 days
- `maxSize` - 10 GiB

Each midnight, the oldest days past either bound are removed.

Set a busy flow to `counts` or `errors`.

A record keeps up to 4096 bytes of a message's json, `valueLimit` in the settings. A longer message is cut, and its record says so.

Nothing in the trace is hidden. A password a body puts in a message is in the trace, for anyone with Read on the flow.

## Restarts

An enabled flow starts with the gateway, from its current version.

An **opc in** starts live. The values its node took while the flow was stopped are not sent.

A **group in** set to `resume` takes up where it left off. Its mark is the source time of the last record whose message the flow finished, and is one of the node's context values.

After a restart or a deploy it reads from its mark, less its **Lateness**, and skips the records it had finished. So a record that reached the historian late is still delivered.

Delivery is **at least once**. After a crash, a sink may fire again for a record the flow had finished just before it.

A sink that must not act twice tells a repeat by the message's `node` and `sourceTime`, which are the record's own. Its `_id` is new on each run.

A write to a PLC is a setpoint, and writing it twice does no harm.

## Replay

**Replay**, on the Versions tab, runs the flow's current version over its sources' history from one time to another. The flow may be enabled or not, so a flow can be replayed before it first runs.

The run is beside the live flow and does not disturb it. One run at a time for each flow.

The run's clock is the records' own. A **delay**, a **trigger**'s wait, a **window**'s end and a body's `setTimeout` fire at the times they would have, however fast the run goes.

The same records give the same trace twice. The one exception is `Math.random`, which a replay does not repeat.

The sources in a run:

- A **group in** reads its group's records over the range.
- An **opc in** reads the records of the group that holds its node. On a node no group holds, the replay is refused and names the node.
- A **timer** and a **calendar** tick at their times in the range.
- A **link in**, a **connection** and an **inject** are silent.

### What a replay writes

A **dry** run writes its trace and nothing else. Each sink is captured into the trace, as a record of what it would have sent:

- an **opc out**'s value
- a **variable**'s value
- a **log**'s line
- an **http request**'s method, url and body

A run reads a copy of the flow's context and writes none of it back.

A **committed** run differs in one thing: a **variable** writes its values into history, at the records' times. That is how a derived variable is filled in over a year of its source, or recomputed after a fix.

A committed run clears each variable's history over the range as it goes. What the historian stored there, and what an earlier run wrote, is gone. The history's **Modifications** view shows each deletion.

Every other sink stays captured in a committed run. A write of last week's value to a PLC is never right.

A committed run needs each variable to be in a historian group, and the flow's deployer to hold Update on it.

The Versions tab lists the runs with how each ended. **Its trace, in History** opens a run on the History tab, and **Cancel** ends one under way.

## Derived variables

A **variable** node makes an OPC UA variable out of a flow's messages. Each message that reaches it writes the variable: its `payload` as the value, the message's status or Good, and the message's time.

The node sets the variable's name, its data type, such as `Double` or `Double[]`, and optionally a description, engineering units and a range.

The variables live on the gateway's own **flow server**, behind the `flow-server` connection, which the gateway makes. Under its Objects folder each flow has a folder, by its slug, holding:

- the flow's variables
- `_Flow`, with the flow's `Version`, whether it is `Enabled`, and its `Messages` and `Errors` since it started

From there a derived variable is a node like any other. It is browsed, put in a [historian group](/help/gateways/groups), trended, served by the gateway's endpoint and used as an [OEE](/help/gateways/oee) signal.

It is read-only to every client, since its flow is what writes it.

A variable no message has reached reads `Bad_WaitingForInitialData`. One a later deploy removed reads `Bad_ObjectDeleted`.

## Access

A flow does what its **deployer** may do. It runs under the gateway's own credential, so the deploy is the gate:

- An **opc out** needs the deployer's Update on an enforced resource of a [historian group](/help/gateways/groups#access) that holds the node. A node no group holds is refused.
- An **opc in** or a **group in** needs Read on the group that holds its node. For an **opc in** on a node no group holds, the gateway reads the node once as the deployer, and refuses the deploy where the server refuses the read.
- A **link out** to another flow needs Update on that flow.
- A **log** needs nothing. An **http request** answers to the allowed hosts, which are a setting and not a right.

The gateway checks a running flow again whenever a grant or a group changes. A flow whose deployer has lost a right stops, and its status names the node. It stays stopped until someone who holds the rights deploys it.

Who may see and change the flow itself is the `flows` table's resource, switched on the [Resources](/access/resources) page:

- **Read** - the flow, its versions, its trace, its replays and its activity
- **Update** - editing, Deploy, Enable and Disable, an inject, a context value and Replay
- **Delete** and **Purge**

A flow's **Access** tab grants roles those rights on the flow alone. It works as a [group's Access tab](/help/gateways/groups#access) does.

## Settings

The `flows` block under `gateway` in the gateway's settings switches flows on. With no block the gateway runs none.

Its keys, with what each is as installed:

- `trace` - the level of a flow that names none: `hops`
- `retention` and `maxSize` - how much trace is kept: `P30D` and `10GiB`
- `valueLimit` - the bytes of a message's json a trace record keeps: 4096
- `queue` - the messages a node's input holds: 1000
- `live` - how often at most the Activity tab hears from a flow: `PT0.2S`
- `function` - a flow's sandbox: `memory` 16 MiB, `stack` 256 KiB, `timeout` `PT0.1S`
- `env` - what a body's `env.get` answers: nothing
- `http.allow` - the hosts an **http request** may reach, names or patterns such as `*.example.com`: none
- `server.port` - the flow server's port on this machine: 4860

A **group in** and a replay need the gateway's historian.

## The sample

<a href="assets/jde-opc/samples/pump-rpm.json" download>pump-rpm.json</a> is a flow over the bundled OPC server's pumps demo. It turns `pump1`'s `motorRpm` into a mean for each minute, watches the feed and raises an alert.

The pump's values move while the PLC emulator runs.

### Import it

1. Download the file above.
2. On the gateway's **Flows** tab, press **Import** and choose the file.
3. The dialog says the graph has 11 nodes, and fills in the name and the slug, `pump1`. Press **Import**.

The editor opens with the flow on the canvas, not deployed.

### What it does

Three strands meet at the window.

The mean:

1. **pump1 motorRpm**, an **opc in**, sends each change of the pump's speed.
2. **a minute**, a **window**, collects them. At the end of each minute that had a change, it sends the minute's figures as its payload.
3. **the mean**, a **function**, keeps the mean, to a tenth.
4. **rpmMean**, a **variable**, writes it to `rpmMean`, in rpm.

The function's body:

```js
msg.payload = Math.round( msg.payload.mean*10 )/10;
return msg;
```

The alert:

1. **over 1400**, a **switch**, passes a mean above 1400.
2. **alert**, a **log**, writes a warning with the mean.
3. **webhook**, an **http request**, posts the mean to a url.

The watchdog:

1. **no value for 30 s**, a **trigger**, takes the same changes as the window. Each one starts its wait again.
2. When 30 seconds pass with no change, it sends `no value for 30 s`, which **silent**, a **log**, writes as a warning.

The first wire has two ends, the window and the trigger. The window gets the message and the trigger a clone, as the History tab's paths show.

The reset:

1. **shift start**, a **calendar**, ticks when a shift of the calendar starts.
2. **reset**, a **change**, sets `reset` on the tick, which empties the window.

A window of a minute ends on the minute, as a shift starts, so here the reset does little: at most it drops the minute that ended with the shift's start. With a window of an hour, a shift's first mean then holds nothing from before the shift.

### Before the first deploy

Three nodes name things that are your site's:

- **pump1 motorRpm** - its **Connection** is `OpcServer`, the bundled server's. Its **Node** is `pump1`'s `motorRpm`, named by its namespace's URI.
- **shift start** - its **Calendar** is `plant`. Name one of your [calendars](/help/calendars) by its slug. With none, replace the node by a **timer** at each shift's start.
- **webhook** - its **Url** is a placeholder. Set your webhook's, and have an administrator list its host under `gateway.flows.http.allow`. Or delete the node.

Then press **Deploy**. A refusal names the node to put right.

### Watch it run

`pump1` runs up to 1450 rpm and holds it. A value that holds still is no data change, so a flow deployed over a pump at speed gets one message, the speed as it stands:

- At the top of the minute the mean goes on to the variable, and the alert is written, since 1450 is above 1400.
- Thirty seconds after the message the watchdog fires. The gateway's **Logs** tab has the warning, `[pump1.silent]pump1 motorRpm:  no value for 30 s`.

Then the flow is quiet. A plant's speed signal is rarely that still.

To give the flow work, stop the pump: write `false` to `pump1`'s `status`. The speed falls away, and for a good half hour each second is a change.

On **Activity**, a message a second goes down each of the first two wires. At the top of each minute the mean goes on to the variable and the switch.

Once a minute's mean is under 1400 the switch passes nothing, and the alert stops.

Write `true` to `status` and the pump runs up again. Two minutes later it holds its speed, and thirty seconds after its last change the watchdog fires.

On **History**, pick **Hour** once a few minutes have passed. Select one of **rpmMean**'s records for the path of its message, back to the last change of its minute. **Play** runs the minutes again.

### The derived mean

Browse the `flow-server` connection: **Objects**, then `pump1`, then `rpmMean`.

To keep its history, put `rpmMean` in a [historian group](/help/gateways/groups) on the `flow-server` connection. The folder's **History** tab then trends it.

The [watch list](/help/gateways#watching)'s **Trend** tab draws it live beside `motorRpm`.

### Replay it

A replay reads `motorRpm` from history, so put `pump1`'s `motorRpm` in a historian group on the `OpcServer` connection first, and let it collect for a while.

Then on **Versions** press **Replay**, pick the last hour and leave the mode **Dry**. When the run has ended, **Its trace, in History** shows what the flow would have done over the hour: each mean, and each alert and each watchdog warning as a captured log line.

A replay reads what the group stored, which its thresholds thin. So its means may differ a little from the live flow's.