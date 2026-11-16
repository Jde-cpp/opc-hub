# Floor map

A line's floor is a drawing of the hall its machines stand in, live. Each machine is an outline coloured by its state, with its OEE, availability, performance and quality and its state inside; each line is a dashed outline with its figures and how many of its machines are running. The page is `lines/<line>/floor` under the gateway's page: **Floor** on the line's [OEE page](/help/gateways/oee#the-lines-oee-page) and on its dashboard tile open it, for a line that has a floor.

A floor and its shapes are rows of the gateway's `oeeFloors` and `oeeFloorShapes` tables, in metres from the floor's top-left. A machine's shape is drawn while the floor's line reaches the machine and it stands; a deleted machine's, or one the line no longer holds, is left out until it is restored or held again. A line the floor's line reaches that has no shape of its own is drawn as the box around its machines' shapes, half a grid out.

## The map

Drag to pan, wheel or pinch to zoom, and **Fit** shows the whole floor again. The outline of a shape keeps its width at any zoom; a shape too small on the screen to hold its figures and its state keeps its name and its OEE.

Hovering, focusing or tapping a shape fills the side panel beside the map: the shift's figures with the loss split, the stop in progress with its reason and start, the time of the last count, and links to the machine's OEE page, its group's page and its PLC's node in the browser, whose **History** tab draws the group's trend. A line's panel shows its figures and links to its OEE page. Shapes take focus in drawing order; each is named by its machine, its state and its OEE.

Below 600 pixels of width the map gives way to a list: the dashboard's tiles for the same machines and lines, in the floor's order.

## The colours and the dashes

The outline is the state, in the colours the OEE charts use, so the floor and the Gantt chart agree. The state is also in the text and the dash, never in the colour alone.

| Outline | State |
| --- | --- |
| Green, solid | Running, and since when |
| Red, blue or violet, solid | Stopped, in the reason's category:  Unplanned, Planned or Excluded, and since when |
| Amber, solid | Stopped and uncoded, since when |
| Pink, solid | A short stop, under the machine's small-stop threshold.  It turns to the reason's colour once the stop passes it |
| Grey, dashed | No data:  the feed is down, or no engine runs the machine |
| Neutral, solid | Not planned:  outside the machine's shifts, or in a break |
| Neutral, dotted | No access:  the gateway refused the subscription, as it does where Read on the machine's group is withheld |

A line's outline is always dashed: green when every machine it reaches is running, red when every one is stopped, and amber for any other mix. Its text, drawn over its machines, says how many are running.

## The figures

The figures are the shift's so far, by the machine's calendar, or the UTC day's for a machine with none. **OEE** is availability times performance times quality; the panel's loss split says where the planned time went. The header carries the floor's line's shift and the time left in it, the plant's clock in the line's calendar's zone so a visitor elsewhere reads the shift right, UTC for a line with no calendar, and the line's own figures.

Everything is pushed over the gateway's socket as it changes; the "since" times tick in the browser. Nothing is read on a timer. Should the socket drop, the map says so and shows the last the gateway said until each machine's next status.

## Access

A visitor sees what its role reads: a floor needs Read on the floor tables, a machine's figures Read on its historian group, and a line's on every group it reaches. A machine the role may not read is drawn **No access**; a line it may not read is too, and its machines the role may read keep their figures.