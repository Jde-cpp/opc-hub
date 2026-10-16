import { Rights } from './permission';
import { ResourceHolders, ResourceRightsRow } from './resource-holder';

//#251:  the resource page's rows - Authorize::Holders' answer, each path relative to its row.
const row:ResourceRightsRow = {
	resource: {id: 7, deleted: null},
	roles: [
		{id: 12, allowed: Rights.Read|Rights.Update, denied: 0, sources: [{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: 0, path: [{id: 11, type: "role"}]}]},
		{id: 11, allowed: Rights.Read|Rights.Update, denied: 0, sources: [{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: 0, path: []}]}
	],
	groups: [
		{id: 21, allowed: Rights.Read|Rights.Update, denied: 0, sources: [{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: 0, path: [{id: 12, type: "role"}, {id: 11, type: "role"}]}]},
		{id: 22, allowed: Rights.Read|Rights.Update, denied: 0, sources: [{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: 0, path: [{id: 21, type: "group"}, {id: 12, type: "role"}, {id: 11, type: "role"}]}]}
	],
	users: [
		{id: 31, allowed: Rights.Read|Rights.Update, denied: Rights.Update, effective: Rights.Read, sources: [
			{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: 0, path: [{id: 21, type: "group"}, {id: 22, type: "group"}, {id: 12, type: "role"}, {id: 11, type: "role"}]},
			{permissionId: 6, allowed: 0, denied: Rights.Update, path: []}
		]}
	]
};
const lists = {
	roles: [{id: 11, name: "Viewer", slug: "viewer"}, {id: 12, name: "Owner", slug: "owner"}],
	groups: [{id: 21, name: "Ops", slug: "ops"}, {id: 22, name: "Ops-EU", slug: "ops-eu"}],
	users: [{id: 31, name: "alice", slug: "alice"}]
};

describe( 'ResourceHolders', ()=>{
	const holders = new ResourceHolders( row, lists );

	it( 'names each row, sorted by name', ()=>{
		expect( holders.roles.map( r=>r.name ) ).toEqual( ["Owner", "Viewer"] );
		expect( holders.groups.map( g=>g.slug ) ).toEqual( ["ops", "ops-eu"] );
		expect( holders.enforced ).toBe( true );
		expect( new ResourceHolders( {...row, resource: {id: 7, deleted: "2026-10-01T00:00:00Z"}}, lists ).enforced ).toBe( false );
	} );

	it( "says how a role holds it - directly or through its member roles", ()=>{
		const [owner, viewer] = holders.roles;
		expect( viewer.tooltip(Rights.Read) ).toBe( "Allowed - held directly" );
		expect( owner.tooltip(Rights.Read) ).toBe( "Allowed - via role Viewer" );
	} );

	it( "says how a group holds it - granted itself or through its parent", ()=>{
		const [ops, eu] = holders.groups;
		expect( ops.tooltip(Rights.Read) ).toBe( "Allowed - role Owner › Viewer" );
		expect( eu.tooltip(Rights.Read) ).toBe( "Allowed - role Owner › Viewer via group Ops" );
	} );

	it( "marks a user's denied right as denied, whatever else allows it", ()=>{
		const [alice] = holders.users;
		expect( alice.isAllowed(Rights.Read) ).toBe( true );
		expect( alice.isDenied(Rights.Update) ).toBe( true );
		expect( alice.isAllowed(Rights.Update) ).toBe( false );
		expect( alice.tooltip(Rights.Update) ).toBe( "Allowed - role Owner › Viewer via group Ops › Ops-EU\nDenied - direct grant" );
	} );

	it( 'shows an id for a row the lists do not name', ()=>{
		const unnamed = new ResourceHolders( row, {roles: [], groups: [], users: []} );
		expect( unnamed.users[0].name ).toBe( "#31" );
		expect( unnamed.users[0].slug ).toBeUndefined();
		expect( unnamed.roles.map( r=>r.tooltip(Rights.Read) ) ).toEqual( ["Allowed - held directly", "Allowed - via role #11"] );
	} );
} );