import { ChipTones, Style, TableSettings, ViewFieldSettings } from 'jde-framework';

//The users list, the 'users' collection's tableSettings in app.routes.ts.  Its own file, not user-detail.ts (#351):  the site imports it
//by name, which puts the file it lives in into the initial bundle - with the page, had they shared one.
//the provider as a chip:  the sign-in providers (access_provider_types) in the primary tone, a certificate identity in the
//resting one;  an OpcServer-provided identity shows its server's slug, which no list can name, and gets the default chip
const providerTones:ChipTones = { Google:"ok", Facebook:"ok", Amazon:"ok", Microsoft:"ok", VK:"ok", Key:"neutral" };
const identityColumns:(string|ViewFieldSettings)[] = [ { name:"name", style: new Style(300) }, { name:"provider", style: new Style(100), chip: providerTones } ];
//three system views:  'all' (the default), the people - password/Google logons - and the certificate identities with their key columns
//No Add (reviews/m3-closing.md #32):  an identity becomes a user by signing in - Google and OPC-server logons on their first
//sign-in, a Key identity when the AppServer enrolls it from access.trustedCertDirs.
export const userTableSettings:TableSettings = {
	empty: { title: "No users yet.", detail: "An identity becomes a user the first time it signs in." },
	canAdd: false,
	excludedColumns: ["isGroup"],
	viewName: "All",
	columns: [ ...identityColumns, "description" ],
	views: [
		{ name: "Users", columns: [ ...identityColumns, "email", "loginName", "description" ], filters: [ {name: "issuer", value: ["<null>"]} ] },
		{ name: "Certs", columns: [ "slug", "modulus", "issuer", "distinguished", "subjectAlt", "expiration", "description" ], sort: "slug", filters: [ {name: "provider", value: ["Key"]} ] }
	]
}