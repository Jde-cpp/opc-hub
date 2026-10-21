import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { HELP_TOPICS, LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { IGRAPHQL } from '../../../services/graphql';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { ListRoute, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { DbScalar, FieldFilter, Operator, View, ViewType } from '../../../model/ql/view';
import { Field } from '../../../model/ql/schema/field';
import { QLList } from './ql-list';
import { Provider } from '@angular/core';
import { pageTitleStub, provideRoute, routerStub, fakeSessionStorage, flush } from 'jde-spa/testing';
import { id, required, scalar, schemaOf, slugRows, snackbarStub } from 'jde-framework/testing';

//A list with no route data behind it, the router at `url` and the snackbar recorded, `providers` after them.  Reset first:  a test
//builds two.
const configure = ( url?:string, ...providers:Provider[] )=>{
	const router = routerStub( url ), snackbar = snackbarStub();
	TestBed.resetTestingModule();
	TestBed.configureTestingModule({ providers: [
		provideRoute(),
		{ provide: Router, useValue: router },
		pageTitleStub,
		{ provide: IGRAPHQL, useValue: {} },
		{ provide: SnackbarService, useValue: snackbar },
		...providers
	]});
	return { router, snackbar };
};
const viewSet = ( views:View[] )=>{
	const set = QLListResolver.viewSet( "users", TestBed.inject(ProfileStore), TestBed.inject(LocalPrefs), {} );
	set.reset( views );
	return set;
};

//review3 L6: /access/resources has no 'resources/:slug' route, so every row click there dead-ended in a
//NavigationError - and the try/catch around router.navigate could never report it, navigate being async.
describe( 'QLList.onRowActivate', ()=>{
	const create = ( tableSettings:TableSettings )=>{
		const { router: {navigate}, snackbar: {error, exception} } = configure();
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing: new ListRoute( {path:'resources', data:{tableSettings} as any} )} as unknown as QLListData );//all onRowActivate reads (ngOnInit never runs)
		return { page, navigate, error, exception };
	};

	it( 'navigates for a collection that has a detail route', ()=>{
		const { page, navigate } = create( {} );
		page.onRowActivate( {slug: 'someone'} );
		expect( navigate ).toHaveBeenCalledWith( ['someone'], expect.anything() );
	} );

	//#251:  a resource's slug repeats across schemas, so its page is at schema/slug
	it( "navigates to the route's rowLink where it has one", async ()=>{
		const { page, navigate, error } = create( {rowLink: {fields: ["schemaName"], segments: (row)=>[`${row["schemaName"]}`, `${row.slug}`]}} );
		navigate.mockResolvedValue( false );
		page.onRowActivate( {slug: 'logs', schemaName: 'opc.debug'} );
		expect( navigate ).toHaveBeenCalledWith( ['opc.debug', 'logs'], expect.anything() );
		await flush();
		expect( error ).toHaveBeenCalledWith( "Could not navigate to 'opc.debug/logs'." );
	} );

	it( 'does not navigate from a row its rowLink gives no page', ()=>{
		const { page, navigate } = create( {rowLink: {fields: [], segments: ()=>undefined}} );
		page.onRowActivate( {slug: 'nodeIds'} );
		expect( navigate ).not.toHaveBeenCalled();
	} );

	it( 'does not navigate where canNavigate is off', ()=>{
		const { page, navigate } = create( {canNavigate: false} );
		page.onRowActivate( {slug: 'nodes'} );
		expect( navigate ).not.toHaveBeenCalled();
	} );

	//the try/catch it replaced could not see either outcome: navigate settles after the block has returned.
	it( 'reports a navigation the router refused', async ()=>{
		const { page, navigate, error } = create( {} );
		navigate.mockResolvedValue( false );
		page.onRowActivate( {slug: 'someone'} );
		await flush();
		expect( error ).toHaveBeenCalledWith( "Could not navigate to 'someone'." );
	} );

	it( 'reports a navigation that threw', async ()=>{
		const { page, navigate, exception } = create( {} );
		navigate.mockRejectedValue( new Error("Cannot match any routes") );
		page.onRowActivate( {slug: 'someone'} );
		await flush();
		expect( exception ).toHaveBeenCalledWith( "Could not navigate to properties", expect.any(Error) );
	} );
} );

//review3 L7: onViewShow/onChangeView and the old Show deleted toggle fired refresh() without awaiting it, and onViewSave/onViewDelete
//awaited reload() with no catch - so a failed re-query left the table empty or stale and said nothing, the rejection
//going to the console as unhandled.  Every one now reports through #refresh/#reload.
describe( 'QLList re-query failures reach the user', ()=>{
	const schema = schemaOf( "User", id(), required("name"), scalar("slug"), scalar("deleted", "DateTime") );

	const create = ()=>{
		const { snackbar: {exception} } = configure();
		const page = TestBed.createComponent( QLList ).componentInstance;
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile: viewSet( [view] )} as unknown as QLListData );
		page.view.set( view );
		//the shared failure: QLListResolver.load is where every re-query path ends up
		vi.spyOn( QLListResolver, 'load' ).mockRejectedValue( new Error("gateway down") );
		return { page, exception };
	};

	afterEach( ()=>vi.restoreAllMocks() );

	it( 'reports a failed view change', async ()=>{
		const { page, exception } = create();
		await page.onChangeView( 0 );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'reports a failed view show', async ()=>{
		const { page, exception } = create();
		await page.onViewShow( page.view() );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'reports a failed reload behind Save', async ()=>{
		const { page, exception } = create();
		await page.onViewSave( page.view() );
		expect( exception ).toHaveBeenCalledWith( "Could not refresh data.", expect.any(Error) );
	} );

	it( 'clears isRefreshing even when the re-query fails', async ()=>{
		const { page } = create();
		await page.onChangeView( 0 );
		expect( page.isRefreshing() ).toBe( false );
	} );
} );

//reviews-issue/web-refactor-review.md #6:  Delete did not await the profile write, said "Could not save view." when it failed, and
//re-queried whatever it was given - a System or Adhoc view, which remove() never takes, re-read the same rows.
describe( 'QLList Delete', ()=>{
	const schema = schemaOf( "User", id(), required("name"), scalar("slug") );
	const create = ()=>{
		const { snackbar: {exception} } = configure();
		const page = TestBed.createComponent( QLList ).componentInstance;
		const system = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		const mine = View.copy( system );
		mine.name = "Mine";
		mine.type = ViewType.User;
		const profile = viewSet( [system, mine] );
		profile.select( 1 );
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile} as unknown as QLListData );
		page.view.set( mine );
		page.isSettings.set( true );
		const load = vi.spyOn( QLListResolver, 'load' ).mockImplementation( async ( _ql:any, data:any )=>({...data, results: {users: []}}) );
		return { page, profile, system, mine, load, exception };
	};
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'takes a saved view, and re-queries under the view left', async ()=>{
		const { page, profile, mine, load, exception } = create();
		await page.onViewDelete( mine );
		expect( profile.views().map( v=>v.name ) ).toEqual( [undefined] );
		expect( load ).toHaveBeenCalledTimes( 1 );
		expect( page.isSettings() ).toBe( false );
		expect( exception ).not.toHaveBeenCalled();
	} );
	it( 'leaves the rows alone for a view it does not take', async ()=>{
		const { page, profile, system, load } = create();
		profile.select( 0 );
		await page.onViewDelete( system );
		expect( profile.views() ).toHaveLength( 2 );
		expect( load ).not.toHaveBeenCalled();
		expect( page.isSettings() ).toBe( false );
	} );
	it( 'says the delete failed, not a save', async ()=>{
		const { page, mine, exception } = create();
		vi.spyOn( TestBed.inject(ProfileStore), 'save' ).mockRejectedValue( new Error( 'offline' ) );
		await page.onViewDelete( mine );
		expect( exception.mock.calls ).toEqual( [["Could not delete view.", expect.any( Error )]] );
	} );
} );

//MVP first-run:  a list with nothing in it, or one the user may not read, showed a bare header.  The page says which now,
//in the route's words for an empty list, and for a 403 the server's line plus what to ask for - with the help topic the
//navbar's ? would open for the same url.
describe( 'QLList empty and refused states', ()=>{
	const create = ( routing:ListRoute, error?:unknown, rows:any[]=[] )=>{
		configure( '/access/users?x=1', { provide: HELP_TOPICS, useValue: [[{id: 'access', title: 'Access', url: 'a.md', routes: ['access']}], [{id: 'navigation', title: 'Navigation', url: 'n.md', routes: ['']}]] } );
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing, schema: {collectionName: routing.collectionName}, profile: viewSet( [] )} as unknown as QLListData );
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
		configure( '/elsewhere', { provide: HELP_TOPICS, useValue: [[{id: 'navigation', title: 'Navigation', url: 'n.md', routes: ['']}]] } );
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing: new ListRoute('users'), schema: {collectionName: 'users'}, profile: viewSet( [] )} as unknown as QLListData );
		expect( page.helpRoute() ).toBeUndefined();
	} );
} );

//reviews/m3-closing.md #24:  the empty state came from the route alone, so a view whose filters matched nothing, a selector
//tab's excluded rows, and a selector tab (no Add) all read "No <things> yet … use Add".
describe( 'QLList empty state on a narrowed list', ()=>{
	const view = ( filters:[string, Operator, DbScalar[]][] )=>{
		const v = View.fromConfig( {configColumns: [], sort: []}, schemaOf("User") );
		v.fieldFilters = filters.map( ([name, operator, value])=>({field: {name} as Field, filter: {operator, value}}) );
		return v;
	};
	const create = ( routing:ListRoute, options:{views?:View[], current?:View, fixedFilters?:FieldFilter[], selector?:boolean} = {} )=>{
		configure( '/access/users' );
		const fixture = TestBed.createComponent( QLList );
		if( options.selector )
			fixture.componentRef.setInput( 'selector', true );
		const page = fixture.componentInstance;
		const views = options.views ?? [view( [] )];
		page.resolvedData.set( {routing, schema: {collectionName: routing.collectionName}, profile: viewSet( views ), fixedFilters: options.fixedFilters} as unknown as QLListData );
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
		const resorted = View.copy( tables );
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
	const schema = schemaOf( "User", id(), required("name"), scalar("slug") );
	const rows = ( n:number, from=0 )=>slugRows( n, from, "user" );
	//load:  the resolver, answering a page of `available`.  hold():  makes each load wait until the test settles it.
	const create = ( available:any[] )=>{
		const { snackbar: {exception} } = configure();
		const fixture = TestBed.createComponent( QLList );
		const page = fixture.componentInstance;
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		const profile = viewSet( [view] );
		const serve = ( data:QLListData )=>{
			const skip = data.skip ?? 0;
			const limit = data.profile.current().limit;
			return { ...data, results: {users: available.slice( skip, skip+limit )}, skip, more: available.length>skip+limit };
		};
		const load = vi.spyOn( QLListResolver, 'load' ).mockImplementation( async ( _ql:any, data:any )=>serve(data) as any );
		const hold = ()=>{
			const pending:{ settle:( failure?:unknown )=>void }[] = [];
			load.mockImplementation( ( _ql:any, data:any )=>new Promise<any>( (resolve, reject)=>
				pending.push( {settle: failure=>failure ? reject(failure) : resolve(serve(data))} ) ) );
			return pending;
		};
		page.init( {data: {routing: new ListRoute({path:'users'}), schema, profile, results: {users: available.slice(0, view.limit)}, more: available.length>view.limit} as unknown as QLListData} );
		return { page, fixture, load, exception, hold };
	};

	afterEach( ()=>vi.restoreAllMocks() );

	it( 'shows the pager only when there is more than a page', ()=>{
		expect( create( rows(3) ).page.paged() ).toBe( false );
		expect( create( rows(40) ).page.paged() ).toBe( true );
	} );

	it( 'turns the page through the resolver, and knows the total on the last one', async ()=>{
		const { page, load } = create( rows(40) );
		expect( page.length() ).toBeUndefined();//Next is on; nothing says how many there are yet
		await page.onPage( {startIndex: 25, pageSize: 25} );//the event's offset, not a binding written before it fired (#12)
		expect( load.mock.calls[0][1].skip ).toBe( 25 );
		expect( page.data().map( (r:any)=>r.id ) ).toEqual( rows(15, 25).map( r=>r.id ) );
		expect( page.more() ).toBe( false );
		expect( page.length() ).toBe( 40 );
		expect( page.paged() ).toBe( true );
	} );

	//reviews/angular-refactor.md B3:  a gateway's Connections tab hands the list its Gateway, which a reload goes to - not a
	//url-sniffing IGRAPHQL
	it( "re-queries the host's ql where it gives one, else the route's", async ()=>{
		const { page, fixture, load } = create( rows(40) );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		expect( load.mock.calls[0][0] ).toBe( TestBed.inject(IGRAPHQL) );
		const gateway = {};
		fixture.componentRef.setInput( 'ql', gateway );
		await page.onPage( {startIndex: 0, pageSize: 25} );
		expect( load.mock.calls[1][0] ).toBe( gateway );
	} );

	//reviews-issue/231-paging.md #5:  a gateway's Connections tab - its routing.path is the browse tree's gateways/<instance>
	it( "writes a page turn's sidenav children under the data's childrenKey", async ()=>{
		const { page } = create( rows(40) );
		page.resolvedData.set( {...page.resolvedData(), childrenKey: "apps/gateways/opchub"} );
		const setChildren = vi.spyOn( TestBed.inject(RouteStore), 'setChildren' ).mockImplementation( ()=>{} );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		expect( setChildren ).toHaveBeenCalledWith( "apps/gateways/opchub", expect.any(Array) );
		expect( page.resolvedData().childrenKey ).toBe( "apps/gateways/opchub" );//and still there for the next turn
	} );

	it( 'starts over on a view change', async ()=>{
		const { page, load } = create( rows(40) );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.onChangeView( 0 );
		expect( load.mock.calls[1][1].skip ).toBe( 0 );
		expect( page.skip() ).toBe( 0 );
	} );

	//reviews-issue/231-paging.md #12:  the reset to the first page is refresh()'s alone - Save and Delete go through it too
	it( 'keeps the page on Refresh, and starts over on a saved or deleted view', async ()=>{
		const { page, load } = create( rows(80) );
		await page.onPage( {startIndex: 50, pageSize: 25} );
		await page.onRefresh();
		expect( load.mock.calls[1][1].skip ).toBe( 50 );
		const mine = View.copy( page.view() );
		mine.name = "Mine";
		await page.onViewSave( mine );
		expect( load.mock.calls[2][1].skip ).toBe( 0 );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.onViewDelete( page.profile().current() );//a saved view:  Delete leaves anything else where it is (#6)
		expect( load.mock.calls[4][1].skip ).toBe( 0 );
		expect( page.skip() ).toBe( 0 );
	} );

	//reviews-issue/231-paging.md #8:  the resolver reads it back (QLListResolver.savedSkip), so a record's breadcrumb or Back
	//returns to the page the record was opened from rather than the first
	describe( 'remembers the page it is on', ()=>{
		afterEach( ()=>vi.unstubAllGlobals() );

		it( 'for the resolver, and forgets it on a view change', async ()=>{
			const stored = fakeSessionStorage();
			const { page } = create( rows(40) );
			await page.onPage( {startIndex: 25, pageSize: 25} );
			expect( stored.get('users/skip') ).toBe( '25' );
			await page.onChangeView( 0 );
			expect( stored.has('users/skip') ).toBe( false );
		} );

		it( 'but not as a selector, whose rows are a pick list', async ()=>{
			const stored = fakeSessionStorage();
			const { page, fixture } = create( rows(40) );
			fixture.componentRef.setInput( 'selector', true );
			await page.onPage( {startIndex: 25, pageSize: 25} );
			expect( stored.size ).toBe( 0 );
		} );
	} );

	//reviews-issue/231-paging.md #2:  page turns and view changes don't wait for each other, so two reloads can overlap.  Only
	//the latest lands - an older one finishing after it used to put back its own page, or the profile it was copied from.
	describe( 'overlapping reloads', ()=>{
		const ids = ( list:any[] )=>list.map( (r:any)=>r.id );
		const turn = ( page:QLList, skip:number )=>page.onPage( {startIndex: skip, pageSize: 25} );

		it( 'drops a page turn that finishes after a newer one', async ()=>{
			const { page, hold } = create( rows(80) );
			const pending = hold();
			const older = turn( page, 25 ), newer = turn( page, 50 );
			pending[1].settle();
			await newer;
			pending[0].settle();
			await older;
			expect( ids(page.data()) ).toEqual( ids(rows(25, 50)) );
			expect( page.skip() ).toBe( 50 );
		} );

		it( 'keeps refreshing until the latest reload lands', async ()=>{
			const { page, hold } = create( rows(80) );
			const pending = hold();
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
			const { page, hold } = create( rows(80) );
			const profile = page.profile();
			profile.reset( [...profile.views(), View.fromTable( {columns: ["name"], sort: "slug"}, schema )] );
			const pending = hold();
			const older = turn( page, 25 ), newer = page.onChangeView( 1 );
			pending[1].settle();
			await newer;
			pending[0].settle();
			await older;
			expect( page.view().sort[0].active ).toBe( "slug" );
			expect( page.skip() ).toBe( 0 );
		} );

		it( 'ignores the failure of a reload already overtaken', async ()=>{
			const { page, exception, hold } = create( rows(80) );
			const pending = hold();
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
			const { page, hold } = create( rows(80) );
			const setChildren = vi.spyOn( TestBed.inject(RouteStore), 'setChildren' ).mockImplementation( ()=>{} );
			const pending = hold();
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