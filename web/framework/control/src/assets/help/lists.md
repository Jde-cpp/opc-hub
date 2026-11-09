# Lists and records

Most of the site is a **list** of records that opens onto a page for one **record**. The lists are:

- the users, groups, roles and resources under [Access](/access)
- a gateway's server connections and its historian templates and groups, on the **Connections**, **Templates** and **Groups** tabs of the gateway's page under [Applications](/apps)

They all work the same way, which is described here.

Each section's own topic covers what is particular to it.

A node's **Children** and an application server's **Logs** are not lists of records - nothing is added or deleted there - but they share the [views](#views).

## Lists

The bar above a list carries **Add**, the refresh button, the views and, when the list fits on one page, the row count.

**Add** starts a new record. A list whose rows are not created by hand has no **Add**.

The refresh button re-reads the list; the rows stay on screen while it runs.

Click a row to open the record. Two kinds of list differ:

- A **Resources** row under [Access](/access) opens a read-only page of who holds rights on it, not an editable record.
- The pick lists on a record's tabs (a group's **Users**, say) have no **Add**. A click ticks the row, and the record's **Save** keeps the picks.

Click a column header to sort by that column, which counts as an unsaved change to the view.

A list longer than its view's **Page Size** is read a page at a time.

The pager under the table steps through the pages. Its inner pair of buttons steps one row at a time. It shows which rows are on screen, and the total once the last page has been reached.

Switching or editing the view returns to the first page. The refresh button keeps the page you are on. So does opening a record and coming back, for as long as the browser tab stays open.

Deleted records are left out of every view that does not filter on **Deleted**. The **Deleted** view, after a list's other built-in views, lists only them. It is the only way to reach one and restore it.

**Resources** under [Access](/access) is the exception. Its **Enforced** switch in each row takes the place of delete, so every row is listed and there is no **Deleted** view. Flipping the switch asks first.

A list with no rows says why:

- An empty list says so.
- A view whose filters match nothing says *No … match this view*. The rows may only be hidden, so switch back to the default view to see them.
- A list you hold no right to read says **No access**. Ask an administrator for a role that can read it.
- A list that could not be loaded gives the reason and a **Retry** button.

## Views

A **view** is which rows are filtered out, which columns are shown and in what order, and how the rows are sorted.

The buttons in the middle of the bar switch between a list's views. Every list comes with a *Default* view and some with more. They are a starting set: the first view you save keeps the whole set as your own. From then the buttons show your views alone, to change, add to and delete as you like.

The view you were last on is remembered in this browser.

The tune button beside the views edits the current one, over three tabs.

- **Filter** - add a column, then pick an operator and the values. *In* and *Not In* take one or more values, typed or picked from those already in the list, with `<null>` and `<not null>` offered where a column can be empty. `<` and `>` compare against one. A date column offers only `<` and `>`, with an **End Date** or **Start Date** to match. A date column that can be empty also has *null* and *not null* boxes, which on **Deleted** read *Not deleted* and *Deleted*.
- **Display** - tick the columns to show and drag them into order. **Page Size** is how many rows a page holds: 25 on a list and 21 on **Logs**, unless the view says otherwise. A node's **Children** are not read a page at a time, so their views have no Page Size.
- **Sort** - the columns to sort by, in order, each ascending or descending.

The bar above the tabs:

- **Save** keeps the view. Under the name it had, it writes over that view; under another name, it is saved as a new view after the others. Two views may share a name.
- **Show** applies the changes without saving them, and the view's button reads *(edited)*.
- **Delete** removes the view. Deleting the last one brings the list's own views back.
- **Cancel** returns to the list unchanged.

Saved views are kept with your profile, so they follow you between browsers.

## Records

A record's page opens on its properties; a required field is marked with an asterisk. Further tabs - a user's groups and roles, say - appear once the record has been saved.

**Save** writes the changes and returns to the list. It stays disabled until something has changed, and a failure leaves you on the page with the reason. **Cancel** discards the changes and returns to the list.

**Delete** does not remove the record: it marks it deleted, and it leaves the list. Open it from the list's **Deleted** view and the button reads **Restore**. A new record has nothing to delete until it has been saved.

Some records also offer **Purge** once they are deleted. It asks first, because it removes the record for good - a purged record has no **Restore**.