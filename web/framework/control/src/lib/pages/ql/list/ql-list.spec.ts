if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { NEVER } from 'rxjs';
import { vi } from 'vitest';
import { HttpErrorResponse } from '@angular/common/http';
import { ComponentPageTitle, HELP_TOPICS, RouteStore } from 'jde-spa';
import { IGRAPHQL } from '../../../services/graphql';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { ListRoute, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { TableSchema } from '../../../model/ql/schema/table-schema';
import { DbScalar, FieldFilter, Operator, View, ViewType } from '../../../model/ql/view';
import { Field } from '../../../model/ql/schema/field';
import { PageProfile } from '../../graphql/model/page-settings';
import { QLList } from './ql-list';

//review3 L6: /access/resources has no 'resources/:slug' route, so every row click there dead-ended in a
//NavigationError - and the try/catch around router.navigate could never report it, navigate being async.
describe( 'QLList.onRowActivate', ()=>{
	let navigate:any;
	let error:any;
	let exception:any;

	const create = ( tableSettings:TableSettings )=>{
		navigate = vi.fn().mockResolvedValue( true );
		error = vi.fn();
		exception = vi.fn();
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error, exception} }
		]});
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing: new ListRoute( {path:'resources', data:{tableSettings} as any} )} as unknown as QLListData );//all onRowActivate reads (ngOnInit never runs)
		return page;
	};

	it( 'navigates for a collection that has a detail route', ()=>{
		create( {} ).onRowActivate( {slug: 'someone'} );
		expect( navigate ).toHaveBeenCalledWith( ['someone'], expect.anything() );
	} );

	//#251:  a resource's slug repeats across schemas, so its page is at schema/slug
	it( "navigates to the route's rowLink where it has one", async ()=>{
		const page = create( {rowLink: (row)=>[`${row["schemaName"]}`, `${row.slug}`]} );
		navigate.mockResolvedValue( false );
		page.onRowActivate( {slug: 'logs', schemaName: 'opc.debug'} );
		expect( navigate ).toHaveBeenCalledWith( ['opc.debug', 'logs'], expect.anything() );
		await Promise.resolve();
		expect( error ).toHaveBeenCalledWith( "Could not navigate to 'opc.debug/logs'." );
	} );

	it( 'does not navigate where canNavigate is off', ()=>{
		create( {canNavigate: false} ).onRowActivate( {slug: 'nodes'} );
		expect( navigate ).not.toHaveBeenCalled();
	} );

	//the try/catch it replaced could not see either outcome: navigate settles after the block has returned.
	it( 'reports a navigation the router refused', async ()=>{
		const page = create( {} );
		navigate.mockResolvedValue( false );
		page.onRowActivate( {slug: 'someone'} );
		await Promise.resolve();
		expect( error ).toHaveBeenCalledWith( "Could not navigate to 'someone'." );
	} );

	it( 'reports a navigation that threw', async ()=>{
		const page = create( {} );
		navigate.mockRejectedValue( new Error("Cannot match any routes") );
		page.onRowActivate( {slug: 'someone'} );
		await Promise.resolve();
		await Promise.resolve();
		expect( exception ).toHaveBeenCalledWith( "Could not navigate to properties", expect.any(Error) );
	} );
} );

//review3 L7: onViewShow/onChangeView and the old Show deleted toggle fired refresh() without awaiting it, and onViewSave/onViewDelete
//awaited reload() with no catch - so a failed re-query left the table empty or stale and said nothing, the rejection
//going to the console as unhandled.  Every one now reports through #refresh/#reload.
describe( 'QLList re-query failures reach the user', ()=>{
	let exception:any;

	const schema = new TableSchema( { name: "User", fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "SCALAR", name: "String" } },
		{ name: "deleted", type: { kind: "SCALAR", name: "DateTime" } }
	] } );

	const create = ()=>{
		exception = vi.fn();
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn().mockResolvedValue(true)} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception} }
		]});
		const page = TestBed.createComponent( QLList ).componentInstance;
		const view = new View( {name: "All", columns: ["name"], sort: "name"}, schema );
		const profile = new PageProfile();
		profile.views = [view];
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile} as unknown as QLListData );
		page.view.set( view );
		//the shared failure: QLListResolver.load is where every re-query path ends up
		vi.spyOn( QLListResolver, 'load' ).mockRejectedValue( new Error("gateway down") );
		return page;
	};

	afterEach( ()=>vi.restoreAllMocks() );

	it( 'reports a failed view change', async ()=>{
		await create().onChangeView( 0 );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'reports a failed view show', async ()=>{
		const page = create();
		await page.onViewShow( page.view() );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'reports a failed reload behind Save', async ()=>{
		const page = create();
		await page.onViewSave( page.view() );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'clears isRefreshing even when the re-query fails', async ()=>{
		const page = create();
		await page.onChangeView( 0 );
		expect( page.isRefreshing() ).toBe( false );
	} );
} );

//reviews-issue/317-deleted-rows.md #10:  the names the settings dialog will not save under.
describe( 'QLList system view names', ()=>{
	it( 'lists the named system views, not the default or a user view', ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn()} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception: vi.fn()} }
		]});
		const page = TestBed.createComponent( QLList ).componentInstance;
		const named = ( name:string|undefined, type=ViewType.System )=>{ const v = new View( {} as any ); v.name = name; v.type = type; return v; };
		const profile = new PageProfile();
		profile.views = [named( undefined ), named( "Users" ), named( "Deleted" ), named( "Mine", ViewType.User )];
		page.resolvedData.set( {profile} as unknown as QLListData );
		expect( page.systemViewNames() ).toEqual( ["Users", "Deleted"] );
	} );
} );

//MVP first-run:  a list with nothing in it, or one the user may not read, showed a bare header.  The page says which now,
//in the route's words for an empty list, and for a 403 the server's line plus what to ask for - with the help topic the
//navbar's ? would open for the same url.
describe( 'QLList empty and refused states', ()=>{
	const create = ( routing:ListRoute, error?:unknown, rows:any[]=[] )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn(), url: '/access/users?x=1'} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception: vi.fn()} },
			{ provide: HELP_TOPICS, useValue: [[{id: 'access', title: 'Access', url: 'a.md', routes: ['access']}], [{id: 'navigation', title: 'Navigation', url: 'n.md', routes: ['']}]] }
		]});
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing, schema: {collectionName: routing.collectionName}, profile: {}} as unknown as QLListData );
		page.data.set( rows );
		page.error.set( error );
		return page;
	};

	it( "says the list is empty, in the route's words, with the page's help topic", ()=>{
		const page = create( new ListRoute({path: 'users', data: {tableSettings: {empty: {title: "No users yet.", detail: "Sign in."}}} as any}) );
		expect( page.showEmpty() ).toBe( true );
		expect( page.failure() ).toBeUndefined();
		expect( page.emptyState() ).toEqual( {title: "No users yet.", detail: "Sign in.", icon: "inbox"} );
		expect( page.helpRoute() ).toEqual( ['/help', 'access'] );
	} );

	it( 'is not empty while rows are shown', ()=>{
		expect( create(new ListRoute('users'), undefined, [{id: 1}]).showEmpty() ).toBe( false );
	} );

	it( 'reads a 403 as no access, quoting the server and pointing at a role', ()=>{
		const page = create( new ListRoute('users'), new HttpErrorResponse({status: 403, error: "[bob]User does not have 'Read' access to 'users'."}) );
		expect( page.showEmpty() ).toBe( false );
		expect( page.failure() ).toEqual( {kind: "forbidden", title: "No access to users.", detail: "[bob]User does not have 'Read' access to 'users'.  Ask an administrator for a role that can read users."} );
	} );

	//reviews/m3-closing.md #6:  embedded in a detail page's tab the route title is the page's - "gateways/opchub.debug" - and
	//"No access to gateways/opchub.debug" names nothing a user could ask for.  A route that says what its rows are is read first.
	it( "names the rows by the route's noun where it gives one", ()=>{
		const routing = new ListRoute( {path: 'gateways/opchub', data: {tableSettings: {noun: "server connections"}}} as any );
		const page = create( routing, new HttpErrorResponse({status: 403, error: "no"}) );
		expect( page.failure()?.title ).toBe( "No access to server connections." );
		expect( page.failure()?.detail ).toContain( "a role that can read server connections" );
		page.error.set( undefined );
		expect( page.emptyState().title ).toBe( "No server connections yet." );
	} );

	it( 'reads any other failure as could-not-load', ()=>{
		expect( create(new ListRoute('users'), new Error("gateway down")).failure() ).toEqual( {kind: "failed", title: "Could not load users.", detail: "gateway down"} );
	} );

	it( 'leaves the help link off where no topic covers the url', ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn(), url: '/elsewhere'} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception: vi.fn()} },
			{ provide: HELP_TOPICS, useValue: [[{id: 'navigation', title: 'Navigation', url: 'n.md', routes: ['']}]] }
		]});
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing: new ListRoute('users'), schema: {collectionName: 'users'}, profile: {}} as unknown as QLListData );
		expect( page.helpRoute() ).toBeUndefined();
	} );
} );

//reviews/m3-closing.md #24:  the empty state came from the route alone, so a view whose filters matched nothing, a selector
//tab's excluded rows, and a selector tab (no Add) all read "No <things> yet … use Add".
describe( 'QLList empty state on a narrowed list', ()=>{
	const view = ( filters:[string, Operator, DbScalar[]][] )=>{
		const v = new View( {} as any );
		v.fieldFilters = filters.map( ([name, operator, value])=>({field: {name} as Field, filter: {operator, value}}) );
		return v;
	};
	const create = ( routing:ListRoute, options:{views?:View[], current?:View, fixedFilters?:FieldFilter[], selector?:boolean} = {} )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn(), url: '/access/users'} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception: vi.fn()} }
		]});
		const fixture = TestBed.createComponent( QLList );
		if( options.selector )
			fixture.componentRef.setInput( 'selector', true );
		const page = fixture.componentInstance;
		const views = options.views ?? [view( [] )];
		page.resolvedData.set( {routing, schema: {collectionName: routing.collectionName}, profile: {views}, fixedFilters: options.fixedFilters} as unknown as QLListData );
		page.view.set( options.current ?? views[0] );
		return page;
	};
	const users = new ListRoute( {path: 'users', data: {tableSettings: {empty: {title: "No users yet.", detail: "Sign in first."}}}} as any );

	it( 'a view whose filters match nothing says so, not "yet"', ()=>{
		const certs = view( [["provider", Operator.In, ["Key"]]] );
		const page = create( users, {views: [view([]), certs], current: certs} );
		expect( page.showEmpty() ).toBe( true );
		expect( page.emptyState().title ).toBe( "No users match this view." );
		expect( page.emptyState().detail ).toBe( "" );
	} );

	it( "the default view's own filter still reads as the whole list, re-sorted or not", ()=>{
		const resources = new ListRoute( {path: 'resources', data: {tableSettings: {canAdd: false, empty: {title: "No resources registered.", detail: "Start the hub."}}}} as any );
		const tables = view( [["criteria", Operator.In, [null]]] );
		const resorted = new View( tables );
		resorted.sort = [{active: "schemaName", direction: "desc"}];
		expect( create(resources, {views: [tables]}).emptyState().title ).toBe( "No resources registered." );
		expect( create(resources, {views: [tables], current: resorted}).emptyState().title ).toBe( "No resources registered." );
	} );

	it( 'a selector tab drops the Add pointer - the tab has no Add', ()=>{
		const page = create( new ListRoute('groups'), {selector: true} );
		expect( page.emptyState().title ).toBe( "No groups yet." );
		expect( page.emptyState().detail ).toBe( "" );
	} );

	it( 'a selector tab whose only rows are excluded reads "No other …"', ()=>{
		const excluded:FieldFilter[] = [{field: {name: "id"} as Field, filter: {operator: Operator.NotIn, value: [7]}}];
		const page = create( new ListRoute('roles'), {selector: true, fixedFilters: excluded} );
		expect( page.emptyState().title ).toBe( "No other roles." );
		expect( page.emptyState().detail ).toBe( "" );
	} );

	it( 'fills Add only on a genuinely empty list', ()=>{
		expect( create(users).addFilled() ).toBe( true );
		const certs = view( [["provider", Operator.In, ["Key"]]] );
		expect( create(users, {views: [view([]), certs], current: certs}).addFilled() ).toBe( false );
		const refused = create( users );
		refused.error.set( new HttpErrorResponse({status: 403}) );
		expect( refused.addFilled() ).toBe( false );
		const shown = create( users );
		shown.data.set( [{id: 1}] );
		expect( shown.addFilled() ).toBe( false );
	} );
} );

//#231:  the list pages with `skip` through the shared resolver.  A page turn reloads at the pager's offset;  a view change is a
//new set of pages and starts over;  a page the rows have left behind falls back to the first (QLListResolver.load).
describe( 'QLList paging', ()=>{
	const schema = new TableSchema( { name: "User", fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "SCALAR", name: "String" } }
	] } );
	const rows = ( n:number, from=0 )=>Array.from( {length: n}, (_, i)=>({id: from+i+1, name: `user ${from+i+1}`, slug: `user${from+i+1}`}) );
	let load:any;
	let serve:( data:QLListData )=>QLListData;//what the resolver answers for a page of `available`
	let exception:any;
	let fixture:ComponentFixture<QLList>;

	const create = ( available:any[] )=>{
		exception = vi.fn();
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER, routeConfig: {}} },
			{ provide: Router, useValue: {navigate: vi.fn().mockResolvedValue(true)} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: IGRAPHQL, useValue: {} },
			{ provide: SnackbarService, useValue: {error: vi.fn(), exception} }
		]});
		fixture = TestBed.createComponent( QLList );
		const page = fixture.componentInstance;
		const view = new View( {name: "All", columns: ["name"], sort: "name"}, schema );
		const profile = new PageProfile();
		profile.views = [view];
		serve = ( data:QLListData )=>{
			const skip = data.skip ?? 0;
			const limit = data.profile.view.limit;
			return { ...data, results: {users: available.slice( skip, skip+limit )}, skip, more: available.length>skip+limit };
		};
		load = vi.spyOn( QLListResolver, 'load' ).mockImplementation( async ( _ql:any, data:any )=>serve(data) );
		page.init( {data: {routing: new ListRoute({path:'users'}), schema, profile, results: {users: available.slice(0, view.limit)}, more: available.length>view.limit} as unknown as QLListData} );
		return page;
	};

	afterEach( ()=>vi.restoreAllMocks() );

	it( 'shows the pager only when there is more than a page', ()=>{
		expect( create( rows(3) ).paged() ).toBe( false );
		expect( create( rows(40) ).paged() ).toBe( true );
	} );

	it( 'turns the page through the resolver, and knows the total on the last one', async ()=>{
		const page = create( rows(40) );
		expect( page.length() ).toBeUndefined();//Next is on; nothing says how many there are yet
		await page.onPage( {startIndex: 25, pageSize: 25} );//the event's offset, not a binding written before it fired (#12)
		expect( load.mock.calls[0][1].skip ).toBe( 25 );
		expect( page.data().map( (r:any)=>r.id ) ).toEqual( rows(15, 25).map( r=>r.id ) );
		expect( page.more() ).toBe( false );
		expect( page.length() ).toBe( 40 );
		expect( page.paged() ).toBe( true );
	} );

	//reviews-issue/231-paging.md #5:  a gateway's Connections tab - its routing.path is the browse tree's gateways/<instance>
	it( "writes a page turn's sidenav children under the data's childrenKey", async ()=>{
		const page = create( rows(40) );
		page.resolvedData.set( {...page.resolvedData(), childrenKey: "apps/gateways/opchub"} );
		const setChildren = vi.spyOn( TestBed.inject(RouteStore), 'setChildren' ).mockImplementation( ()=>{} );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		expect( setChildren ).toHaveBeenCalledWith( "apps/gateways/opchub", expect.any(Array) );
		expect( page.resolvedData().childrenKey ).toBe( "apps/gateways/opchub" );//and still there for the next turn
	} );

	it( 'starts over on a view change', async ()=>{
		const page = create( rows(40) );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.onChangeView( 0 );
		expect( load.mock.calls[1][1].skip ).toBe( 0 );
		expect( page.skip() ).toBe( 0 );
	} );

	//reviews-issue/231-paging.md #12:  the reset to the first page is refresh()'s alone - Save and Delete go through it too
	it( 'keeps the page on Refresh, and starts over on a saved or deleted view', async ()=>{
		const page = create( rows(80) );
		await page.onPage( {startIndex: 50, pageSize: 25} );
		await page.onRefresh();
		expect( load.mock.calls[1][1].skip ).toBe( 50 );
		await page.onViewSave( page.view() );
		expect( load.mock.calls[2][1].skip ).toBe( 0 );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.onViewDelete( page.view() );
		expect( load.mock.calls[4][1].skip ).toBe( 0 );
		expect( page.skip() ).toBe( 0 );
	} );

	//reviews-issue/231-paging.md #8:  the resolver reads it back (QLListResolver.savedSkip), so a record's breadcrumb or Back
	//returns to the page the record was opened from rather than the first
	describe( 'remembers the page it is on', ()=>{
		let stored:Map<string,string>;
		beforeEach( ()=>{
			stored = new Map();
			vi.stubGlobal( 'sessionStorage', { getItem: (k:string)=>stored.get(k) ?? null, setItem: (k:string, v:string)=>{ stored.set(k, v); }, removeItem: (k:string)=>{ stored.delete(k); } } );
		} );
		afterEach( ()=>vi.unstubAllGlobals() );

		it( 'for the resolver, and forgets it on a view change', async ()=>{
			const page = create( rows(40) );
			await page.onPage( {startIndex: 25, pageSize: 25} );
			expect( stored.get('users/skip') ).toBe( '25' );
			await page.onChangeView( 0 );
			expect( stored.has('users/skip') ).toBe( false );
		} );

		it( 'but not as a selector, whose rows are a pick list', async ()=>{
			const page = create( rows(40) );
			fixture.componentRef.setInput( 'selector', true );
			await page.onPage( {startIndex: 25, pageSize: 25} );
			expect( stored.size ).toBe( 0 );
		} );
	} );

	//reviews-issue/231-paging.md #2:  page turns and view changes don't wait for each other, so two reloads can overlap.  Only
	//the latest lands - an older one finishing after it used to put back its own page, or the profile it was copied from.
	describe( 'overlapping reloads', ()=>{
		let pending:{ settle:( failure?:unknown )=>void }[];
		const hold = ()=>{//each load waits until the test settles it
			pending = [];
			load.mockImplementation( ( _ql:any, data:QLListData )=>new Promise<QLListData>( (resolve, reject)=>
				pending.push( {settle: failure=>failure ? reject(failure) : resolve(serve(data))} ) ) );
		};
		const ids = ( list:any[] )=>list.map( (r:any)=>r.id );
		const turn = ( page:QLList, skip:number )=>page.onPage( {startIndex: skip, pageSize: 25} );

		it( 'drops a page turn that finishes after a newer one', async ()=>{
			const page = create( rows(80) );
			hold();
			const older = turn( page, 25 ), newer = turn( page, 50 );
			pending[1].settle();
			await newer;
			pending[0].settle();
			await older;
			expect( ids(page.data()) ).toEqual( ids(rows(25, 50)) );
			expect( page.skip() ).toBe( 50 );
		} );

		it( 'keeps refreshing until the latest reload lands', async ()=>{
			const page = create( rows(80) );
			hold();
			const older = turn( page, 25 ), newer = turn( page, 50 );
			pending[0].settle();
			await older;
			expect( page.isRefreshing() ).toBe( true );
			expect( ids(page.data()) ).toEqual( ids(rows(25)) );//the older page is never shown on the way
			pending[1].settle();
			await newer;
			expect( page.isRefreshing() ).toBe( false );
		} );

		it( 'does not bring back the view a page turn was copied from', async ()=>{
			const page = create( rows(80) );
			page.resolvedData().profile.views.push( new View( {columns: ["name"], sort: "slug"}, schema ) );
			hold();
			const older = turn( page, 25 ), newer = page.onChangeView( 1 );
			pending[1].settle();
			await newer;
			pending[0].settle();
			await older;
			expect( page.view().sort[0].active ).toBe( "slug" );
			expect( page.skip() ).toBe( 0 );
		} );

		it( 'ignores the failure of a reload already overtaken', async ()=>{
			const page = create( rows(80) );
			hold();
			const older = turn( page, 25 ), newer = turn( page, 50 );
			pending[1].settle();
			await newer;
			pending[0].settle( new Error("gateway down") );
			await older;
			expect( exception ).not.toHaveBeenCalled();
			expect( page.error() ).toBeUndefined();
			expect( ids(page.data()) ).toEqual( ids(rows(25, 50)) );
		} );

		it( 'gives the sidenav the page that lands, not the one overtaken', async ()=>{
			const page = create( rows(80) );
			const setChildren = vi.spyOn( TestBed.inject(RouteStore), 'setChildren' ).mockImplementation( ()=>{} );
			hold();
			const older = turn( page, 25 ), newer = turn( page, 50 );
			pending[1].settle();
			await newer;
			pending[0].settle();
			await older;
			expect( setChildren ).toHaveBeenCalledTimes( 1 );
			expect( setChildren ).toHaveBeenCalledWith( 'users', rows(25, 50).map( r=>({title: r.name, path: r.slug}) ) );
		} );
	} );
} );