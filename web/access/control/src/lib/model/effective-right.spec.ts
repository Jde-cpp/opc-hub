import { Rights } from './permission';
import { Resource } from './resource';
import { EffectiveRight, RightsSource, UserRightsRow } from './effective-right';

const names = { groups: new Map([[7, "Ops"], [3, "Ops-EU"]]), roles: new Map([[9, "Owner"], [11, "Viewer"]]) };
const resources = [
	new Resource( {id: 12, schemaName: "access", slug: "groups", name: "groups", allowed: Rights.Create|Rights.Read|Rights.Update, deleted: undefined} ),
	new Resource( {id: 13, schemaName: "access", slug: "users", name: "users", allowed: Rights.All, deleted: undefined} ),//enforced, no grant
	new Resource( {id: 14, schemaName: "access", slug: "roles", name: "roles", allowed: Rights.All, deleted: "2026-09-01T00:00:00Z"} ),//unenforced, no grant
];
const source = ( allowed:Rights, denied:Rights, path:RightsSource["path"]=[] ):RightsSource=>({ permissionId: 1, allowed, denied, path });
const nested:RightsSource["path"] = [ {id:7, type:"group"}, {id:3, type:"group"}, {id:9, type:"role"}, {id:11, type:"role"} ];
const row:UserRightsRow = { resource: {id: 12, schemaName: "access", slug: "groups", criteria: "", deleted: null}, allowed: Rights.Read|Rights.Update, denied: Rights.Update, effective: Rights.Read,
	sources: [ { permissionId: 42, allowed: Rights.Read|Rights.Update, denied: Rights.None, path: nested }, { permissionId: 43, allowed: Rights.None, denied: Rights.Update, path: [] } ] };

describe( 'EffectiveRight.describe', ()=>{
	it( 'names a bare grant', ()=>{
		expect( EffectiveRight.describe(source(Rights.Read, Rights.None)) ).toBe( "direct grant" );
	} );
	it( 'reads roles then groups, top-down, by name', ()=>{
		const s = source( Rights.Read, Rights.None, nested.map( step=>({...step, name: (step.type=="group" ? names.groups : names.roles).get(step.id)}) ) );
		expect( EffectiveRight.describe(s) ).toBe( "role Owner › Viewer via group Ops › Ops-EU" );
	} );
	it( 'a role granted to the user directly has no via', ()=>{
		expect( EffectiveRight.describe(source(Rights.Read, Rights.None, [{id:11, type:"role", name:"Viewer"}])) ).toBe( "role Viewer" );
	} );
	it( 'falls back to the pk when the name is unknown', ()=>{
		expect( EffectiveRight.describe(source(Rights.Read, Rights.None, [{id:99, type:"group"}])) ).toBe( "group #99" );
	} );
} );

describe( 'EffectiveRight.fromRows', ()=>{
	const rights = EffectiveRight.fromRows( [row], resources, names );

	it( 'joins the resource name and available rights from the cache and the path names from the lists', ()=>{
		const groups = rights.find( r=>r.resource.id==12 )!;
		expect( groups.resource.name ).toBe( "groups" );
		expect( groups.resource.availableRights ).toBe( Rights.Create|Rights.Read|Rights.Update );
		expect( groups.effective ).toBe( Rights.Read );
		expect( groups.sources[0].path.map(s=>s.name) ).toEqual( ["Ops", "Ops-EU", "Owner", "Viewer"] );
	} );
	it( 'adds a lockout row for an enforced resource with no grant, and none for an unenforced one', ()=>{
		expect( rights.map(r=>r.resource.id) ).toEqual( [12, 13] );//schema, then name: groups < users; roles (unenforced, ungranted) absent
		const users = rights.find( r=>r.resource.id==13 )!;
		expect( users.isLockout ).toBe( true );
		expect( users.effective ).toBe( Rights.None );
	} );
	const node:UserRightsRow = { resource: {id: 30, schemaName: "opc.debug", slug: "nodeIds", criteria: "ns=4;i=6020", deleted: null}, allowed: Rights.Read, denied: Rights.None, effective: Rights.Read, sources: [ {permissionId: 5, allowed: Rights.Read, denied: Rights.None, path: []} ] };
	it( 'keeps a node resource the cache never had, top-level and named by its slug when its table row is absent', ()=>{
		const r = EffectiveRight.fromRows( [node], resources, names ).find( r=>r.resource.id==30 )!;
		expect( r.resource.name ).toBe( "nodeIds" );
		expect( r.resource.criteria ).toBe( "ns=4;i=6020" );
		expect( r.resource.schema ).toBe( "opc.debug" );
		expect( r.enforced ).toBe( true );
		expect( r.parent ).toBeUndefined();
	} );
	//A node row replaces its table's row for its subtree (OpcAuthorize resolves a node to the nearest configured ancestor), so it
	//nests under it - and takes its name:  its own is the QL slug access_role_add coalesced, "nodeIds" under the synced "node_ids".
	it( 'nests a node resource under its table row, named after it, in criteria order', ()=>{
		const table = new Resource( {id: 20, schemaName: "opc.debug", slug: "nodeIds", name: "node_ids", allowed: Rights.All, deleted: undefined} );
		const second:UserRightsRow = { ...node, resource: {...node.resource, id: 31, criteria: "ns=4;i=6010"} };
		const rights = EffectiveRight.fromRows( [node, second], [...resources, table], names );
		expect( rights.map(r=>r.resource.id) ).toEqual( [12, 13, 20] );//the two enforced access tables as lockouts, then the node table; no node at the top level
		const root = rights.find( r=>r.resource.id==20 )!;
		expect( root.isLockout ).toBe( true );//the table itself is enforced with nothing granted on it - the node grants do not reach the rest of the tree
		expect( root.children.map(c=>c.resource.criteria) ).toEqual( ["ns=4;i=6010", "ns=4;i=6020"] );
		expect( root.children.every(c=>c.parent===root && c.resource.name=="node_ids") ).toBe( true );
	} );
} );

//reviews/m3-closing.md #14:  a node resource is enforced the moment a role is granted on it (access_role_add mints it that
//way), and the OpcServer resolves the node's subtree to it alone - so a user holding nothing on it is locked out there,
//whatever they hold on the table.  The tab never showed it:  its resources were the table rows only.
describe( 'EffectiveRight.fromRows on node resources', ()=>{
	const table = ( deleted?:string )=>new Resource( {id: 20, schemaName: "opc.install", slug: "nodeIds", name: "node_ids", allowed: Rights.Read|Rights.Update, deleted} );
	//as the server lists a node row:  its slug coalesced as the name, and no `allowed` - access_role_add inserts none
	const nodeRow = ( id:number, criteria:string )=>new Resource( {id, schemaName: "opc.install", slug: "nodeIds", name: "nodeIds", criteria, deleted: undefined} );
	const onTable:UserRightsRow = { resource: {id: 20, schemaName: "opc.install", slug: "nodeIds", criteria: "", deleted: null}, allowed: Rights.Read, denied: Rights.None, effective: Rights.Read, sources: [ {permissionId: 5, allowed: Rights.Read, denied: Rights.None, path: [{id: 11, type: "role"}]} ] };

	it( 'marks a node the user holds nothing on as a lockout under its table, named and offering what the table offers', ()=>{
		const rights = EffectiveRight.fromRows( [onTable], [table(), nodeRow(32, "ns=4;i=6030")], names );
		const root = rights.find( r=>r.resource.id==20 )!;
		expect( root.effective ).toBe( Rights.Read );
		expect( root.children.map(c=>c.resource.id) ).toEqual( [32] );
		const node = root.children[0];
		expect( node.isLockout ).toBe( true );
		expect( node.resource.name ).toBe( "node_ids" );
		expect( node.resource.availableRights ).toBe( Rights.Read|Rights.Update );//else no lockout mark would render
	} );

	it( 'shows it at the top when the table itself is unenforced and ungranted - the fresh-install default', ()=>{
		const rights = EffectiveRight.fromRows( [], [table("2026-09-01T00:00:00Z"), nodeRow(32, "ns=4;i=6030")], names );
		expect( rights.map(r=>r.resource.id) ).toEqual( [32] );
		expect( rights[0].isLockout ).toBe( true );
		expect( rights[0].resource.criteria ).toBe( "ns=4;i=6030" );
	} );

	it( 'names a granted node after its table even though the list now holds the node row itself', ()=>{
		const granted:UserRightsRow = { resource: {id: 32, schemaName: "opc.install", slug: "nodeIds", criteria: "ns=4;i=6030", deleted: null}, allowed: Rights.Read, denied: Rights.None, effective: Rights.Read, sources: [ {permissionId: 6, allowed: Rights.Read, denied: Rights.None, path: []} ] };
		const node = EffectiveRight.fromRows( [onTable, granted], [table(), nodeRow(32, "ns=4;i=6030")], names ).find( r=>r.resource.id==20 )!.children[0];
		expect( node.isLockout ).toBe( false );
		expect( node.resource.name ).toBe( "node_ids" );
		expect( node.resource.availableRights ).toBe( Rights.Read|Rights.Update );
	} );
} );

describe( 'EffectiveRight.tooltip', ()=>{
	const groups = EffectiveRight.fromRows( [row], resources, names ).find( r=>r.resource.id==12 )!;
	it( 'lists the sources touching one right, allows first', ()=>{
		expect( EffectiveRight.tooltip(groups, Rights.Update) ).toBe( "Allowed - role Owner › Viewer via group Ops › Ops-EU\nDenied - direct grant" );
	} );
	it( 'says nothing for a right no source touches', ()=>{
		expect( EffectiveRight.tooltip(groups, Rights.Create) ).toBe( "" );
	} );
	it( 'explains a lockout row', ()=>{
		const users = EffectiveRight.fromRows( [row], resources, names ).find( r=>r.resource.id==13 )!;
		expect( EffectiveRight.tooltip(users, Rights.Read) ).toContain( "every action is denied" );
	} );
} );
