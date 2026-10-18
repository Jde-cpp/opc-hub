import { Style, TableSettings } from 'jde-framework';

//The roles list, the 'roles' collection's tableSettings in app.routes.ts.
export const roleTableSettings:TableSettings = {
	empty: { title: "No roles yet.", detail: "An install seeds Viewer, System Administrator, Owner, Engineer, Operator and Maintenance Technician - an empty list means that seed has not run against this database.", add: "Use Add to create one." },
	excludedColumns:["permissions"],
	columns: [
		{ name:"name", style: new Style(300) },
		"description"
	]
}