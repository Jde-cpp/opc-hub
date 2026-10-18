import { Flex, Style, TableSettings } from 'jde-framework';

//The resources list, the 'resources' collection's tableSettings in app.routes.ts - beside its page, as the others are (#351).
//A resource row is the authorizer's per-table switch, not an editable record.  `deleted` is what the switch reads:
//Authorize::Test bails on a deleted resource ("not enabled"), so a DELETED resource is an UNENFORCED one and the
//installation ships every row deleted (ResourceSyncAwait creates each one and immediately deletes it) so a fresh system is
//not locked down.  Hence the shape of this page:  no Add, no Purge - the resources table advertises only Read, Delete and
//Subscribe as grantable rights, so nobody can even hold Update on it - and the one action that matters, enforcement, as a
//switch in the row.  A row opens its read-only page of who holds rights on it, at schema/slug:  the natural key is
//schema+slug+criteria and a slug repeats across schemas (`logs`), so `:slug` alone cannot address it.
//Enforced leads the columns because it is the only thing on the page that DOES anything;  schemaName ("access",
//"opc.default", ...) still groups the list, so it leads the sort.
//One system view, 'Tables':  the rows with no criteria - the per-table switches the page exists for.  A criteria row is a
//node-scoped resource access_role_add mints when a role is granted on a node; it is managed from the node's Access tab and
//read on the user's Effective rights tab, and here it only ever looked like a duplicate of its table (same slug, no
//criteria column, a name that differs by case) - so this page does not list it at all.
export const resourceTableSettings:TableSettings = {
	empty: { title: "No resources registered.", detail: "Every service registers its tables here when it starts - start the hub or the app server against this database.", icon: "lock" },
	canAdd: false,
	canPurge: false,
	rowLink: { fields: ["schemaName", "criteria"], segments: (row)=>row["criteria"] ? undefined : [`${row["schemaName"]}`, `${row.slug}`] },//ResourceResolver's 'resources/:schema/:slug'.  A criteria row, which a saved view without the criteria filter lists, shares its table's schema and slug - its grants are on its node's Access tab.
	viewName: "Tables",
	filters: [ {name: "criteria", value: ["<null>"]} ],
	columns: [
		{ name:"deleted", displayName:"Enforced", style: new Style({flex: new Flex(110), align: "center"}), liveToggle: {
			enable: "Enforce",
			disable: "Stop enforcing",
			enableMessage: "Access to this table will be restricted to users and roles holding an explicit permission on it. Grant yourself one first, or you may lose your own access.",
			disableMessage: "Permissions on this table will stop being checked and everyone, signed in or not, will have full access to it."
		} },
		{ name:"schemaName", displayName:"Schema", style: new Style(150) },
		{ name:"name", style: new Style(300) },
		"description"
	],
	sort: [ {active:"schemaName", direction:"asc"}, {active:"name", direction:"asc"} ]
}