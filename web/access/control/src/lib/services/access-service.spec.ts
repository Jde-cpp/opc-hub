import { TestBed } from '@angular/core/testing';
import { HttpErrorResponse } from '@angular/common/http';
import { AccessService } from './access-service';
import { EffectiveRight } from '../model/effective-right';
import { Rights } from '../model/permission';
import { silence } from 'jde-spa/testing';
import { protoServiceProviders, qlAnswering } from 'jde-framework/testing';

//reviews/m3-closing.md #13:  the resources list was cached for the page's life, and it carries each resource's enforcement
//(`deleted`).  Effective rights builds its lockout rows from it, so after the Enforced toggle the tab said "nothing
//restricts this user" about a table that now refused them - or kept a lockout row on one that was open to all - until a reload.
describe( 'AccessService resources', ()=>{
	//server:  the `users` resource's state on the server, and the node-scoped resource rows
	const create = ()=>{
		TestBed.configureTestingModule({ providers: protoServiceProviders() });
		const service = TestBed.inject( AccessService );
		const server = { enforced: false, nodes: [] as object[] };
		//userRights - the user holds no grant - and the group and role names answer nothing
		const table = { get resources(){ return [{id: 7, schemaName: "access", slug: "users", name: "users", allowed: 255, deleted: server.enforced ? null : "2026-09-21T00:00:00Z"}, ...server.nodes]; } };
		vi.spyOn( service, 'queryArray' ).mockImplementation( qlAnswering(table, {unwrapped: true}) as any );
		vi.spyOn( service as any, 'postQL' ).mockImplementation( (async ( ql:string )=>{
			server.enforced = ql.includes( "restoreResource" );
			return {};
		}) as any );
		return {
			service, server,
			queries: ()=>vi.mocked( service.queryArray ).mock.calls.map( c=>(c[0] as string).split( /[({]/ )[0].trim() ),
			lockouts: async ()=>(await service.effectiveRights( 2 )).rights.map( r=>r.resource.slug )//the user holds no grant, so every row is a lockout row
		};
	};

	it( 'shows a lockout once a resource is enforced, in the same session', async ()=>{
		const { service, lockouts } = create();
		expect( await lockouts() ).toEqual( [] );//unenforced:  nothing restricts the user
		await service.mutate( `restoreResource( id:7 )` );
		expect( await lockouts() ).toEqual( ["users"] );
	} );

	it( 'drops the lockout once it is not', async ()=>{
		const { service, server, lockouts } = create();
		server.enforced = true;
		expect( await lockouts() ).toEqual( ["users"] );
		await service.mutate( `deleteResource( id:7 )` );
		expect( await lockouts() ).toEqual( [] );
	} );

	it( 'reads the resources fresh for the tab, whoever changed them', async ()=>{//another admin, another tab
		const { service, server, queries, lockouts } = create();
		await service.effectiveRights( 2 );
		server.enforced = true;
		expect( await lockouts() ).toEqual( ["users"] );
		expect( queries().filter(q=>q=="resources") ).toHaveLength( 2 );
	} );

	it( 'still keeps one load for the pages that share it', async ()=>{
		const { service, queries } = create();
		await Promise.all( [service.loadResources(), service.loadResources()] );
		expect( queries().filter(q=>q=="resources") ).toHaveLength( 1 );
	} );

	it( 'never keeps a failed load, fresh or not', async ()=>{//angular-review2 #14 / angular-review3 L5
		const { service } = create();
		vi.mocked( service.queryArray ).mockRejectedValueOnce( new Error("down") );
		await expect( service.effectiveRights(2) ).rejects.toThrow( "down" );
		expect( await service.loadResources() ).toHaveLength( 1 );
	} );

	//reviews/m3-closing.md #14:  the tab's resources were the table rows only (criteria:null), so a node the user is locked out
	//of never showed - "nothing restricts this user" while the OpcServer denied the subtree.
	it( 'shows a lockout on a node resource the user holds nothing on', async ()=>{
		const { service, server } = create();
		server.nodes = [{id: 32, schemaName: "opc.install", slug: "nodeIds", name: "nodeIds", criteria: "ns=4;i=6030", deleted: null}];
		expect( (await service.effectiveRights(2)).rights.map(r=>[r.resource.slug, r.resource.criteria, r.isLockout]) ).toEqual( [["nodeIds", "ns=4;i=6030", true]] );
	} );

	//reviews/m3-closing.md #28:  the server lets a user read their own rights whatever they hold, but the tab also asked for the
	//group and role names - and a 403 on `roles` (enforced, no Read) failed the whole tab, the one place that would say why.
	//The names only label the paths;  without them a path reads by id.  The rights and the resources stay fatal.
	it( "still shows the user's rights when the role names are refused", async ()=>{
		const { service, server } = create();
		server.enforced = true;
		const grant = {resource: {id: 7, schemaName: "access", slug: "users", criteria: "", deleted: null}, allowed: 1, denied: 0, effective: 1, sources: [{permissionId: 5, allowed: 1, denied: 0, path: [{id: 11, type: "role"}]}]};
		const answer = vi.mocked( service.queryArray ).getMockImplementation()!;
		vi.mocked( service.queryArray ).mockImplementation( (async ( ql:string, vars:any )=>{
			if( ql.startsWith("roles") )
				throw new HttpErrorResponse( {status: 403, error: "[bob]User does not have 'Read' access to 'roles'."} );
			return ql.startsWith( "userRights" ) ? [grant] : answer( ql, vars );
		}) as any );
		const warn = silence();
		const {rights} = await service.effectiveRights( 2 );
		expect( rights.map(r=>EffectiveRight.describe(r.sources[0])) ).toEqual( ["role #11"] );
		expect( warn ).toHaveBeenCalled();//said, not swallowed
		warn.mockRestore();
	} );

	it( 'still fails the tab when the rights themselves cannot be read', async ()=>{
		const { service } = create();
		const answer = vi.mocked( service.queryArray ).getMockImplementation()!;
		vi.mocked( service.queryArray ).mockImplementation( (async ( ql:string, vars:any )=>{
			if( ql.startsWith("userRights") )
				throw new HttpErrorResponse( {status: 500} );
			return answer( ql, vars );
		}) as any );
		await expect( service.effectiveRights(2) ).rejects.toBeInstanceOf( HttpErrorResponse );
	} );
} );

//#252:  the group page's Effective rights - groupRights in userRights' shape, joined the same way, but no lockout rows:  a
//resource the group gives nothing on may be one its members hold elsewhere.
describe( 'AccessService.groupRights', ()=>{
	const create = ()=>{
		TestBed.configureTestingModule({ providers: protoServiceProviders() });
		const service = TestBed.inject( AccessService );
		const grant = {resource: {id: 7, schemaName: "access", slug: "users", criteria: "", deleted: null}, allowed: 1, denied: 0, effective: 1, sources: [{permissionId: 5, allowed: 1, denied: 0, path: [{id: 21, type: "group"}, {id: 11, type: "role"}]}]};
		vi.spyOn( service, 'queryArray' ).mockImplementation( qlAnswering( {
			resources: [{id: 7, schemaName: "access", slug: "users", name: "users", allowed: 255, deleted: null}, {id: 8, schemaName: "access", slug: "roles", name: "roles", allowed: 255, deleted: null}],//both enforced
			groups: [{id: 21, name: "Ops"}],
			roles: [{id: 11, name: "Viewer"}],
			groupRights: [grant]
		}, {unwrapped: true} ) as any );
		return service;
	};

	it( 'asks groupRights for the group and names the path', async ()=>{
		const service = create();
		const {rights} = await service.groupRights( 6 );
		const [ql, vars] = vi.mocked( service.queryArray ).mock.calls.find( c=>(c[0] as string).startsWith("groupRights") )!;
		expect( ql ).toMatch( /^groupRights\( id:\$id \)/ );
		expect( vars ).toEqual( {id: 6} );
		expect( rights.map(r=>[r.resource.slug, EffectiveRight.describe(r.sources[0])]) ).toEqual( [["users", "role Viewer via group Ops"]] );
	} );

	it( 'adds no lockout row for an enforced resource the group gives nothing on', async ()=>{
		const service = create();
		expect( (await service.groupRights( 6 )).rights.map(r=>r.resource.slug) ).toEqual( ["users"] );
		expect( (await service.effectiveRights( 2 )).rights.map(r=>[r.resource.slug, r.isLockout]) ).toEqual( [["roles", true], ["users", true]] );//userRights answers nothing here - a user's tab keeps them
	} );

	//reviews-issue/252-effective-rights-group.md #12:  each record the sidenav opened read every resource and both name lists again
	const asked = ( service:AccessService )=>vi.mocked( service.queryArray ).mock.calls.map( c=>(c[0] as string).split(/[({]/)[0].trim() + ((c[0] as string).includes("criteria:null") ? "(criteria:null)" : "") );
	it( 'reads only the tables the pages share - each row carries its own enforcement', async ()=>{
		const service = create();
		await service.groupRights( 6 );
		expect( asked(service) ).toContain( "resources(criteria:null)" );
		expect( asked(service) ).not.toContain( "resources" );
	} );
	it( 'reuses the names it is handed', async ()=>{
		const service = create();
		const first = await service.groupRights( 6 );
		vi.mocked( service.queryArray ).mockClear();
		const second = await service.groupRights( 6, first.lists );
		expect( asked(service) ).toEqual( ["groupRights"] );//the tables are the cached list's
		expect( EffectiveRight.describe(second.rights[0].sources[0]) ).toBe( "role Viewer via group Ops" );
	} );
	it( 'reads the names again for a path step they lack', async ()=>{
		const service = create();
		const {rights, lists} = await service.groupRights( 6, {groups: [], roles: [{id: 11, name: "Viewer"}]} );
		expect( asked(service).filter( q=>q=="groups" || q=="roles" ) ).toEqual( ["groups", "roles"] );
		expect( lists.groups ).toEqual( [{id: 21, name: "Ops"}] );
		expect( EffectiveRight.describe(rights[0].sources[0]) ).toBe( "role Viewer via group Ops" );
	} );
	it( 'does not read a refused list again', async ()=>{
		const service = create();
		const {rights} = await service.groupRights( 6, {roles: [{id: 11, name: "Viewer"}]} );
		expect( asked(service) ).toEqual( ["groupRights", "resources(criteria:null)"] );
		expect( EffectiveRight.describe(rights[0].sources[0]) ).toBe( "role Viewer via group #21" );
	} );
} );

//#251:  the resource page's holders - the server's resourceRights joined to the three lists' names and slugs, which only label
//and link the rows, so a list the user may not read leaves ids rather than failing the page.
describe( 'AccessService.resourceRights', ()=>{
	const answer = {
		resource: {id: 7, deleted: null},
		roles: [{id: 11, allowed: 2, denied: 0, sources: [{permissionId: 5, allowed: 2, denied: 0, path: []}]}],
		groups: [{id: 21, allowed: 2, denied: 0, sources: [{permissionId: 5, allowed: 2, denied: 0, path: [{id: 11, type: "role"}]}]}],
		users: [{id: 31, allowed: 2, denied: 0, effective: 2, sources: [{permissionId: 5, allowed: 2, denied: 0, path: [{id: 21, type: "group"}, {id: 11, type: "role"}]}]}]
	};
	const create = ()=>{
		TestBed.configureTestingModule({ providers: protoServiceProviders() });
		const service = TestBed.inject( AccessService );
		vi.spyOn( service, 'querySingle' ).mockResolvedValue( answer );
		vi.spyOn( service, 'queryArray' ).mockImplementation( qlAnswering( {
			roles: [{id: 11, name: "Viewer", slug: "viewer"}],
			groups: [{id: 21, name: "Ops", slug: "ops"}],
			users: [{id: 31, name: "alice", slug: "alice"}]
		}, {unwrapped: true} ) as any );
		return service;
	};

	it( 'asks for the one resource, and names and links every row', async ()=>{
		const service = create();
		const holders = await service.resourceRights( 7 );
		expect( vi.mocked(service.querySingle).mock.calls[0][0] ).toMatch( /^resourceRights\( id:\$id \)/ );
		expect( vi.mocked(service.querySingle).mock.calls[0][1] ).toEqual( {id: 7} );
		expect( holders.enforced ).toBe( true );
		expect( [...holders.roles, ...holders.groups, ...holders.users].map( h=>`${h.kind}:${h.name}:${h.slug}` ) ).toEqual( ["role:Viewer:viewer", "group:Ops:ops", "user:alice:alice"] );
		expect( holders.users[0].tooltip(Rights.Read) ).toBe( "Allowed - role Viewer via group Ops" );
	} );

	it( 'shows ids where a list is refused', async ()=>{
		const service = create();
		const lists = vi.mocked( service.queryArray ).getMockImplementation()!;
		vi.mocked( service.queryArray ).mockImplementation( (async ( ql:string, vars:any )=>{
			if( ql.startsWith("users") )
				throw new HttpErrorResponse( {status: 403, error: "[bob]User does not have 'Read' access to 'users'."} );
			return lists( ql, vars );
		}) as any );
		const warn = silence();
		const holders = await service.resourceRights( 7 );
		expect( holders.users.map( u=>[u.name, u.slug] ) ).toEqual( [["#31", undefined]] );
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	} );

	it( 'fails when the rights themselves cannot be read', async ()=>{
		const service = create();
		vi.mocked( service.querySingle ).mockRejectedValue( new HttpErrorResponse({status: 403}) );
		await expect( service.resourceRights(7) ).rejects.toBeInstanceOf( HttpErrorResponse );
	} );
	//reviews-issue/251-resource-page.md #9:  each resource the sidenav opened read the three full lists again
	it( 'reuses the lists it is handed', async ()=>{
		const service = create();
		const first = await service.resourceRights( 7 );
		vi.mocked( service.queryArray ).mockClear();
		const second = await service.resourceRights( 7, first.lists );
		expect( service.queryArray ).not.toHaveBeenCalled();
		expect( second.users[0].name ).toBe( "alice" );
	} );
	it( 'reads the lists again for a row they lack', async ()=>{
		const service = create();
		const holders = await service.resourceRights( 7, {roles: [{id: 11, name: "Viewer", slug: "viewer"}], groups: [{id: 21, name: "Ops", slug: "ops"}], users: []} );
		expect( vi.mocked(service.queryArray).mock.calls.map( c=>c[0] ) ).toEqual( ["roles{ id name slug }", "groups{ id name slug }", "users{ id name slug }"] );
		expect( holders.users[0].name ).toBe( "alice" );
	} );
	it( 'does not read a refused list again, and labels its rows by id', async ()=>{
		const service = create();
		const holders = await service.resourceRights( 7, {roles: [{id: 11, name: "Viewer", slug: "viewer"}], groups: [{id: 21, name: "Ops", slug: "ops"}]} );
		expect( service.queryArray ).not.toHaveBeenCalled();
		expect( holders.users[0].name ).toBe( "#31" );
	} );
} );