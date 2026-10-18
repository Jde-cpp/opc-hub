import { Style } from 'jde-framework';

//The groups list, the 'groups' collection's tableSettings in app.routes.ts.
export const groupTableSettings = {
	empty: { title: "No groups yet.", detail: "A group collects users so a role is granted once for all of them.", add: "Use Add to create one." },
	excludedColumns: ["isGroup", "members"],
	columns: [ //without this the list falls back to ListRoute's name/created/updated/slug default.
		{ name:"name", style: new Style(300) },
		"slug",
		"description"
	],
	collectionName: "groups"
}