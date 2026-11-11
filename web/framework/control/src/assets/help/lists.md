# Lists and records

Most of the site is a **list** of records that opens onto a page for one **record**. The lists are:

- the users, groups, roles and resources under [Access](/access)
- a gateway's server connections and its historian templates and groups, on the **Connections**, **Templates** and **Groups** tabs of the gateway's page under [Applications](/apps)

They all work the same way, which is described here.

Each section's own topic covers what is particular to it.

A node's **Children**, a gateway's **Watch list** and an application server's **Logs** are not lists of records, but they share the [views](#views), and the first two the [filter box](#filtering).

## Lists

Above a list are its views, a row of tabs, and under them the bar with **Add**, the [filter box](#filtering), the refresh button and, when the list fits on one page, the row count. **Discard** and **Save** join the bar, after the filter box, while the view has unsaved changes.

**Add** starts a new record. A list whose rows are not created by hand has no **Add**.

The refresh button re-reads the list; the rows stay on screen while it runs.

Click a row to open the record. Two kinds of list differ:

- A **Resources** row under [Access](/access) opens a read-only page of who holds rights on it, not an editable record.
- The pick lists on a record's tabs (a group's **Users**, say) have no **Add**. A click ticks the row, and the record's **Save** keeps the picks.

### Sorting

Click a column header to sort by that column, and click it again to sort the other way. The column goes first in the sort, ahead of the columns the view already sorts by.

The header's [menu](#columns) has **Sort ascending** and **Sort descending**, which set the direction outright. On a column the view already sorts by they leave its place in the sort as it is.

**Sort by** in the [view's menu](#views) is the whole sort:

- The columns the view sorts by come first, in their order, each with an arrow for its direction. Click one to turn it the other way.
- Drag one of them to another place in the sort, or use the up and down buttons at its end.
- Click a column under them to add it, ascending, after the others.
- **No sorting** clears the sort.

The menu stays open for the next click.

The header of the first column in the sort shows its arrow. A later one shows its arrow and its place in the sort, 2 or 3.

A column the list cannot be sorted by has no sort in its menu and is not in **Sort by**. **Logs** is always sorted by time, after any other column, so **No sorting** there leaves the time sorted the way it was.

A change to the sort is an [unsaved change](#unsaved-changes) to the view.

### Columns

A column is changed from its header, and each change is an [unsaved change](#unsaved-changes) to the view.

- **Drag a header** along the header row to move its column. On a touch screen, press and hold the header first.
- **Drag a header's right edge** to set the column's width, down to 64 pixels, or to the column's own width where the list draws it narrower. Double-click the edge to put back the width the list gives the column.
- A header shows a menu button when the pointer is over it or the keyboard is on it:
  - **Sort ascending** and **Sort descending** [sort](#sorting) by the column.
  - **Filter by…** lists the column's values, on a column with a few fixed ones. See [Filtering](#filtering).
  - **Move left** and **Move right** move the column a place.
  - **Wider** and **Narrower** change its width a step at a time.
  - **Hide field** takes the column out of the view. The last column cannot be hidden.

**Fields** in the [view's menu](#views) lists every column the list has, with the ones shown ticked and first, in their order. Tick a column to show it, after the others, and untick it to hide it. The menu stays open for the next tick, its list in the order it opened in.

The **Name** column stays at the left edge when a list wider than the page is scrolled sideways. Where a view does not show the name, its first column does.

A list longer than its view's page size is read a page at a time. **Page size** in the view's menu sets it.

The pager under the table steps through the pages. Its inner pair of buttons steps one row at a time. It shows which rows are on screen, and the total once the last page has been reached.

Switching or changing the view returns to the first page, except a change to its columns, which keeps the page you are on. So does the refresh button. So does opening a record and coming back, for as long as the browser tab stays open.

Deleted records are left out of every view that does not filter on **Deleted**. The **Deleted** view, after a list's other built-in views, lists only them, and so does `has:deleted` typed in any view's filter box. That is how to reach one and restore it.

**Resources** under [Access](/access) is the exception. Its **Enforced** switch in each row takes the place of delete, so every row is listed and there is no **Deleted** view. Flipping the switch asks first.

A list with no rows says why:

- An empty list says so.
- A view whose filter matches nothing says *No … match this view*. The rows may only be hidden, so clear the filter box or switch back to the default view to see them.
- A list you hold no right to read says **No access**. Ask an administrator for a role that can read it.
- A list that could not be loaded gives the reason and a **Retry** button.

## Filtering

The box in the middle of the bar filters the list. Type a query and the list follows: on Enter, when you pick a suggestion, and a moment after you stop typing. What you type is an [unsaved change](#unsaved-changes) to the view, so **Save** keeps the filter with it.

| Type | To list the rows where |
| --- | --- |
| `provider:Google` | the column is that value |
| `provider:Google,Key` | it is either value |
| `-provider:Key` | it is not that value; an empty cell counts as not |
| `no:email`, `has:email` | the column is empty, or is not |
| `exponent:>3`, `>=`, `<`, `<=` | a number or a date compares so |
| `created:2026-01-01..2026-01-31` | it lies in the range, both ends included; `*` leaves an end open, `created:2026-01-01..*` |
| `created:2026-01-31` | the date is that day, in this browser's time zone |
| `created:2026-01-31T14:30` | the time is in that minute; add `:05` for the second |
| `created:>=@today-7d` | a date relative to today: `@today`, `@today-7d`, `@today+1d` |
| `name:"Pump 3"` | a value with a space or a comma, in double or single quotes |
| `pump` | the name contains the word; several words must all be there |

A column is named as its header reads, in upper or lower case, and in quotes when it has a space: `"security mode":Sign`. Terms are separated by spaces, and a row must match every one of them. Naming a column twice narrows it further, so `created:>=2026-01-01 created:<2026-02-01` is January.

As you type, the box suggests the columns, then, after the colon, the values: a column's fixed choices where it has them, otherwise the values in the rows on screen. A date column also offers `@today`, and a number or a date column the comparison signs.

A term the box cannot read is underlined, and the line under the box says why. The list is filtered by the other terms meanwhile. The usual reasons:

- a column the list does not have
- a value its column cannot hold, such as a word for a number or a date that does not exist
- `>` or a range on a text column
- a date excluded with `-`, or more than one date in a list
- a column the server works out after it has read the row, such as a connection's **Status**

A chip in a row - a user's provider, say - is a shortcut: click it to add `provider:Google` to the filter, and click it again to take the term out. Click a chip of another value in the same column and it joins the term, `provider:Google,Key`, the rows with either; a click on one of those takes it back out.

**Filter by…** in a column header's menu does the same from a list of the column's values, each ticked while the filter holds it. The menu stays open for the next tick. It is offered on a column of chips, of fixed choices or of yes and no.

Three kinds of list differ:

- On **Resources** under [Access](/access) only `has:enforced` and `no:enforced` are read on the **Enforced** column.
- A node's **Children** and the **Watch list** filter the rows already on screen. There `field:value` must match the cell exactly, a comparison works on any column - as numbers when both sides are numbers - and a word is looked for in every text cell, whatever its case. In the other lists the database decides whether upper and lower case differ.
- The pick lists on a record's tabs never list deleted rows, whatever is typed. **Logs** has no filter box.

## Views

A **view** is the [filter](#filtering) on the rows, which columns are shown and in what order, and how the rows are sorted.

The tabs above the bar are a list's views; click one to switch. Every list comes with a *Default* view and some with more. They are a starting set: the first change you make to them - a save, a rename, a new view, a move or a deletion - keeps the whole set as your own. From then the tabs are your views alone, to change, add to and delete as you like. Two views may share a name.

The view you were last on is remembered in this browser.

**New view**, after the last tab, adds a copy of the view you are on, named *View* and its number, and switches to it.

The active tab carries a chevron, which opens the view's menu:

- **Fields** shows and hides [columns](#columns). Its first entry on a list's own page and on a node's **Children**, **Select**, shows or hides the column of checkboxes that tick rows.
- **Sort by** sets the columns the list is [sorted](#sorting) by, and their order.
- **Page size** is how many rows a page holds: 10, 25, 50, 100, 250 or 500. A list starts at 25 and **Logs** at 21. A node's **Children** are not read a page at a time, so their menu has none.
- **Rename** turns the tab's name into a box. Enter keeps the new name, Escape or a blank box leaves the old one.
- **Duplicate** adds a copy of the view, as it was last saved, after it under the same name.
- **Move left** and **Move right** change the order of the tabs. Dragging a tab does the same.
- **Save changes** and **Discard changes** do what **Save** and **Discard** in the bar do.
- **Delete** removes the view, its unsaved changes with it. Deleting the last one brings the list's own views back. It is greyed out on a list's own view when that is the only one.

The pick lists on a record's tabs have no tabs row: the button at the left of their bar shows the view's name, and its menu's **Views** lists the views to switch between, over **Fields**, **Sort by**, **Page size**, **Save changes** and **Discard changes**. A pick list shares its views, and their unsaved changes, with the list's own page. It leaves out a view saved to list deleted rows, and never lists a deleted row itself, whatever its filter says.

### Unsaved changes

A change to a view - a filter typed, a sort, a column shown, hidden, moved or resized, a page size - applies at once and is not saved. The view's tab shows a dot after its name, and **Discard** and **Save** appear in the bar.

- **Save** keeps the changes as the view. On one of a list's own views it keeps the whole set as yours, as any first change does.
- **Discard** drops them and shows the view as it was saved.

Each view has its own unsaved changes, and they stay while you look at another tab, open a record or reload the page. They are kept in this browser, for you alone, until you save or discard them: another browser does not have them, and nor does anyone else who signs in on this one. A change that puts the view back as it was saved leaves nothing to save, and the dot goes.

**Rename** names the view at once, whether or not it has unsaved changes. **New view** copies the view as it shows, unsaved changes included, and **Duplicate** as it was saved.

Saved views are kept with your profile, so they follow you between browsers. Unsaved changes do not.

## Records

A record's page opens on its properties; a required field is marked with an asterisk. Further tabs - a user's groups and roles, say - appear once the record has been saved.

**Save** writes the changes and returns to the list. It stays disabled until something has changed, and a failure leaves you on the page with the reason. **Cancel** discards the changes and returns to the list.

**Delete** does not remove the record: it marks it deleted, and it leaves the list. Open it from the list's **Deleted** view and the button reads **Restore**. A new record has nothing to delete until it has been saved.

Some records also offer **Purge** once they are deleted. It asks first, because it removes the record for good - a purged record has no **Restore**.