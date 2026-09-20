# Lists and records

Most of the site is a **list** of records that opens onto a page for one **record**: the users, groups and roles under [Access](/access), and a gateway's server connections under [Gateways](/gateways). They all work the same way, which is described here; each section's own topic covers what is particular to it.

A node's **Children** and an application server's **Logs** are not lists of records - nothing is added or deleted there - but they share the [views](#views).

## Lists

The bar above a list carries **Add**, the refresh button, the views, the row count and **Show deleted**.

**Add** starts a new record. A list whose rows are not created by hand has no **Add**.

The refresh button re-reads the list; the rows stay on screen while it runs.

Click a row to open the record. Click a column header to sort by that column, which counts as an unsaved change to the view.

**Show deleted** returns deleted records to the list, which is the only way to reach one and restore it. It is remembered in this browser.

An empty list says so. A list you hold no right to read says **No access** rather than looking empty - ask an administrator for a role that can read it - and a list that could not be loaded gives the reason and a **Retry** button.

## Views

A **view** is which rows are filtered out, which columns are shown and in what order, and how the rows are sorted. The buttons in the middle of the bar switch between a list's views: every list has a *default* view, some come with more, and the ones you save are added beside them. The view you were last on is remembered in this browser.

The tune button beside the views edits the current one, over three tabs.

- **Filter** - add a column, then pick an operator and the values. *In* and *Not In* take one or more values, typed or picked from those already in the list, with `<null>` and `<not null>` offered where a column can be empty; `<` and `>` compare against one. A date column takes a start or an end date instead.
- **Display** - tick the columns to show and drag them into order. **Page Size** is how many rows the list loads, 25 unless the view says otherwise; a list does not page beyond it, so raise it or filter when the row count stops there.
- **Sort** - the columns to sort by, in order, each ascending or descending.

**Show** applies the changes without saving them, and the view's button reads *(edited)*. **Save** keeps the view under the name in the box. A view the list came with cannot be overwritten: give it a new name and it is saved as your own. **Delete** removes a view you saved, and **Cancel** returns to the list unchanged.

Saved views are kept with your profile, so they follow you between browsers.

## Records

A record's page opens on its properties; a required field is marked with an asterisk. Further tabs - a user's groups and roles, say - appear once the record has been saved.

**Save** writes the changes and returns to the list. It stays disabled until something has changed, and a failure leaves you on the page with the reason. **Cancel** discards the changes and returns to the list.

**Delete** does not remove the record: it marks it deleted, and it leaves the list. Open it again with **Show deleted** and the button reads **Restore**. A new record has nothing to delete until it has been saved.

Some records also offer **Purge** once they are deleted. It asks first, because it removes the record for good - a purged record has no **Restore**.
