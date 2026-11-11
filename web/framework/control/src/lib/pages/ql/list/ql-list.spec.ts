import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { HELP_TOPICS, LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { IGRAPHQL } from '../../../services/graphql';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { ListRoute, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { View } from '../../../model/ql/view';
import { QLList } from './ql-list';
import { Paginator } from '../../../shared/paginator/paginator';
import { Provider } from '@angular/core';
import { SelectionModel } from '@angular/cdk/collections';
import { Subject } from 'rxjs';
import { pageTitleStub, provideRoute, routerStub, fakeSessionStorage, flush, screenSize } from 'jde-spa/testing';
import { id, object, required, scalar, schemaOf, slugRows, snackbarStub } from 'jde-framework/testing';

//A list with no route data behind it, the router at `url` and the snackbar recorded, `providers` after them.  Reset first:  a test
//builds two.
const configure = ( url?:string, ...providers:Provider[] )=>{
	const router = routerStub( url ), snackbar = snackbarStub();
	TestBed.resetTestingModule();
	TestBed.configureTestingModule({ providers: [
		screenSize(),
		provideRoute(),
		{ provide: Router, useValue: router },
		pageTitleStub,
		{ provide: IGRAPHQL, useValue: {} },
		{ provide: SnackbarService, useValue: snackbar },
		...providers
	]});
	return { router, snackbar };
};
//`views` as the route's, the profile unread:  the store has no provider, so the read a change makes first finds no rows
const viewSet = ( views:View[] )=>{
	const set = QLListResolver.viewSet( "users", TestBed.inject(ProfileStore), TestBed.inject(LocalPrefs), {columns: []} );
	set.reset( views, schemaOf("User") );
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
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile: viewSet( [view, View.fromTable( {columns: ["name"], sort: "slug"}, schema )] )} as unknown as QLListData );
		page.view.set( view );
		//the shared failure: QLListResolver.load is where every re-query path ends up
		vi.spyOn( QLListResolver, 'load' ).mockRejectedValue( new Error("gateway down") );
		return { page, exception };
	};

	afterEach( ()=>vi.restoreAllMocks() );

	it( 'reports a failed view change', async ()=>{
		const { page, exception } = create();
		page.profile().select( 1 );
		await page.onChangeView();
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
		page.profile().select( 1 );
		await page.onChangeView();
		expect( page.isRefreshing() ).toBe( false );
	} );
} );

//#589:  View.copy copies each field, so the Deleted view names its column where it found it by identity
describe( 'QLList on the Deleted view', ()=>{
	it( 'still shows the deleted column, on its draft too, and the default view does not', ()=>{
		configure();
		const schema = schemaOf( "User", id(), required("name"), scalar("slug"), scalar("deleted", "DateTime") );
		const profile = viewSet( QLListResolver.systemViews(schema, {columns: ["name"]}) );
		const page = TestBed.createComponent( QLList ).componentInstance;
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile} as unknown as QLListData );
		const shown = ()=>{
			page.view.set( profile.current() );
			return page.displayedFields().map( f=>f.name );
		};
		profile.select( 0 );//whichever view an earlier test left remembered
		expect( shown() ).toEqual( ["name"] );
		profile.select( profile.views().findIndex( v=>QLListResolver.isDeletedView(v) ) );
		expect( shown() ).toEqual( ["name", "deleted"] );
		profile.edit( view=>{ view.limit = 50; } );
		expect( profile.current().isAdhoc ).toBe( true );
		expect( shown() ).toEqual( ["name", "deleted"] );
	} );
} );

//reviews-issue/web-refactor-review.md #6:  Delete did not await the profile write, said "Could not save view." when it failed, and
//re-queried whatever it was given, the view on screen left as it was or not.
describe( 'QLList Delete', ()=>{
	const schema = schemaOf( "User", id(), required("name"), scalar("slug") );
	const create = ()=>{
		const { snackbar: {exception} } = configure();
		const page = TestBed.createComponent( QLList ).componentInstance;
		const system = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		const mine = View.copy( system );
		mine.name = "Mine";
		const profile = viewSet( [system, mine] );
		profile.select( 1 );
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema, profile} as unknown as QLListData );
		page.view.set( mine );
		page.isSettings.set( true );
		const load = vi.spyOn( QLListResolver, 'load' ).mockImplementation( async ( _ql:any, data:any )=>({...data, results: {users: []}}) );
		return { page, profile, system, mine, load, exception };
	};
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'takes the view on screen, and re-queries under the view left', async ()=>{
		const { page, profile, mine, load, exception } = create();
		await page.onViewDelete( mine );
		expect( profile.views().map( v=>v.name ) ).toEqual( [undefined] );
		expect( load ).toHaveBeenCalledTimes( 1 );
		expect( page.isSettings() ).toBe( false );
		expect( exception ).not.toHaveBeenCalled();
	} );
	it( 'leaves the rows alone where the view on screen stays', async ()=>{
		const { page, profile, system, mine, load } = create();
		profile.select( 0 );
		await page.onViewDelete( mine );
		expect( profile.views() ).toEqual( [system] );
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

//#591:  a chip cell clicked adds `field:value` to the filter, and clicked again takes it out
describe( 'QLList chip filters', ()=>{
	const things = schemaOf( "User", id(), required("name"), {name: "provider", type: {kind: "ENUM", name: "Provider"}}, object("connectionStatus", "ConnectionStatus") );
	const create = ()=>{
		configure();
		const page = TestBed.createComponent( QLList ).componentInstance;
		const view = View.fromTable( {columns: ["name", {name: "provider", chip: {}}, {name: "connectionStatus", displayName: "Status", selection: "name", chip: {}}], sort: "name"}, things );
		page.resolvedData.set( {routing: new ListRoute({path:'users'}), schema: things, profile: viewSet( [view] )} as unknown as QLListData );
		page.view.set( view );
		const changed = vi.spyOn( page, 'onChangeView' ).mockResolvedValue();
		return { page, changed };
	};
	const held = ( page:QLList )=>[...page.chipFilters()].map( ([column, values])=>[column, [...values]] );

	it( 'offers the chip columns the box can filter, and not one the gateway grafts on', ()=>{
		expect( held( create().page ) ).toEqual( [["provider", []]] );
	} );

	//reviews-issue/591-views-filter-box.md #2:  a second value was a second term, which intersected with the first to nothing
	it( 'adds the term on a click, another value to its list, and takes each out on the next, each a change to the draft', async ()=>{
		const { page, changed } = create();
		await page.onChipClick( {column: "provider", value: "Google"} );
		expect( page.profile().current().filter ).toBe( "Provider:Google" );
		expect( page.profile().current().isAdhoc ).toBe( true );
		expect( held( page ) ).toEqual( [["provider", ["Google"]]] );
		await page.onChipClick( {column: "provider", value: "Key"} );
		expect( page.profile().current().filter ).toBe( "Provider:Google,Key" );
		expect( held( page ) ).toEqual( [["provider", ["Google", "Key"]]] );
		await page.onChipClick( {column: "provider", value: "Google"} );
		expect( page.profile().current().filter ).toBe( "Provider:Key" );
		await page.onChipClick( {column: "provider", value: "Key"} );
		expect( page.profile().current().filter ).toBe( "" );
		expect( page.profile().current().isAdhoc ).toBe( false );//as it was saved:  no draft
		expect( changed ).toHaveBeenCalledTimes( 4 );
	} );

	it( "gathers the rows' values for the box's suggestions", ()=>{
		const { page } = create();
		page.data.set( [{id: 2, name: "b", provider: "Key"}, {id: 1, name: "a", provider: "Google", connectionStatus: {name: "Idle"}}, {id: 3, name: "a", provider: null}] );
		expect( page.filterValues() ).toMatchObject( {name: ["a", "b"], provider: ["Google", "Key"], connectionStatus: []} );
	} );
} );

//reviews/m3-closing.md #24:  the empty state came from the route alone, so a view whose filters matched nothing, a selector
//tab's excluded rows, and a selector tab (no Add) all read "No <things> yet … use Add".
describe( 'QLList empty state on a narrowed list', ()=>{
	const view = ( filter="" )=>View.fromConfig( {configColumns: [], sort: [], filter}, schemaOf("User", id(), scalar("provider"), scalar("criteria")) );
	const create = ( routing:ListRoute, options:{views?:View[], current?:View, fixedFilter?:string, selector?:boolean} = {} )=>{
		configure( '/access/users' );
		const fixture = TestBed.createComponent( QLList );
		if( options.selector )
			fixture.componentRef.setInput( 'selector', true );
		const page = fixture.componentInstance;
		const views = options.views ?? [view()];
		page.resolvedData.set( {routing, schema: {collectionName: routing.collectionName}, profile: viewSet( views ), fixedFilter: options.fixedFilter} as unknown as QLListData );
		page.view.set( options.current ?? views[0] );
		return page;
	};
	const users = new ListRoute( {path: 'users', data: {tableSettings: {empty: {title: "No users yet.", detail: "Sign in first."}}}} as any );

	it( 'a view whose filters match nothing says so, not "yet"', ()=>{
		const certs = view( "provider:Key" );
		const page = create( users, {views: [view(), certs], current: certs} );
		expect( page.showEmpty() ).toBe( true );
		expect( page.emptyState().title ).toBe( "No users match this view." );
		expect( page.emptyState().detail ).toBe( "" );
	} );

	it( "the default view's own filter still reads as the whole list, re-sorted or not", ()=>{
		const resources = new ListRoute( {path: 'resources', data: {tableSettings: {canAdd: false, empty: {title: "No resources registered.", detail: "Start the hub."}}}} as any );
		const tables = view( "no:criteria" );
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
		const page = create( new ListRoute('roles'), {selector: true, fixedFilter: "-id:7 no:deleted"} );
		expect( page.emptyState().title ).toBe( "No other roles." );
		expect( page.emptyState().detail ).toBe( "" );
	} );

	//#587:  the first tab is whichever view the user put there, so the test is against the route's default view
	it( "compares with the route's default view, not the first tab", async ()=>{
		const all = view(), certs = view( "provider:Key" );
		const page = create( users, {views: [all, certs]} );
		await page.profile().move( certs, 0 );
		expect( page.profile().views() ).toEqual( [certs, all] );
		page.view.set( certs );
		expect( page.emptyState().title ).toBe( "No users match this view." );
		page.view.set( all );
		expect( page.emptyState().title ).toBe( "No users yet." );
		expect( page.addFilled() ).toBe( true );
	} );

	it( 'fills Add only on a genuinely empty list', ()=>{
		expect( create(users).addFilled() ).toBe( true );
		const certs = view( "provider:Key" );
		expect( create(users, {views: [view(), certs], current: certs}).addFilled() ).toBe( false );
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
	const schema = schemaOf( "User", id(), required("name"), scalar("slug"), scalar("email") );
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
	//another view, sorted otherwise, picked as the toolbar picks it before it says the view changed
	const another = ( page:QLList )=>{
		const profile = page.profile();
		profile.reset( [...profile.views(), View.fromTable( {columns: ["name"], sort: "slug"}, schema )], schema );
		profile.select( 1 );
	};

	//#588 review #11:  a pick list has no tabs row, so a panel there was a tab panel no tab controls
	it( 'puts its rows in a tab panel only under a tabs row', ()=>{
		const list = create( rows(3) ).fixture;
		list.detectChanges();
		expect( list.nativeElement.querySelector( 'mat-tab-nav-panel ql-table' ) ).not.toBeNull();
		const pick = create( rows(3) ).fixture;//configure() resets:  a second list in one test
		pick.componentRef.setInput( 'selector', true );
		pick.detectChanges();
		expect( pick.nativeElement.querySelector( 'ql-table' ) ).not.toBeNull();
		expect( pick.nativeElement.querySelector( 'mat-tab-nav-panel' ) ).toBeNull();
	} );

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

	//reviews-issue/329-sticky-table-header.md #6
	it( 'turns the page from the list\'s first row, from the page it was on', async ()=>{
		const { page, fixture } = create( rows(40) );
		const show = vi.spyOn( Paginator, 'showFirstRow' );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		expect( show ).toHaveBeenCalledTimes( 1 );
		expect( show.mock.calls[0][0] ).toBe( fixture.nativeElement );
		expect( show.mock.calls[0].slice(1) ).toEqual( [0, 25] );
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
		another( page );
		await page.onChangeView();
		expect( load.mock.calls[1][1].skip ).toBe( 0 );
		expect( page.skip() ).toBe( 0 );
	} );

	//#588 review #5:  a rename put the list back on its first page
	it( 'stays on its page for a view that shows the rows it did, renamed or copied', async ()=>{
		const { page, load } = create( rows(40) );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.profile().rename( page.profile().current(), "Mine" );
		await page.onChangeView();
		expect( load ).toHaveBeenCalledTimes( 1 );
		expect( page.skip() ).toBe( 25 );
		expect( page.view().name ).toBe( "Mine" );//the editor's, and Delete's
	} );

	//#592:  the rows on screen hold a column hidden, moved or resized, so the page stays as it is;  one shown is asked for
	it( 'asks again for a column shown, not for one hidden, moved or resized', async ()=>{
		const { page, load } = create( rows(40) );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		const edit = async ( change:( view:View )=>void )=>{
			page.profile().edit( change );
			await page.onChangeView();
		};
		await edit( view=>view.setDisplayed("email", true, "Email") );
		expect( load ).toHaveBeenCalledTimes( 2 );
		expect( [load.mock.calls[1][1].skip, page.skip()] ).toEqual( [25, 25] );//reviews-issue/592-views-fields.md #6:  on the page it was on
		expect( page.displayedFields().map( f=>f.name ) ).toEqual( ["name", "email"] );
		await edit( view=>view.moveField("email", 0) );
		await edit( view=>view.setWidth("name", 200) );
		await edit( view=>view.setDisplayed("name", false) );
		expect( load ).toHaveBeenCalledTimes( 2 );
		expect( page.displayedFields().map( f=>f.name ) ).toEqual( ["email"] );
		await edit( view=>view.setDisplayed("name", true) );//the rows still hold it
		expect( load ).toHaveBeenCalledTimes( 2 );
	} );

	//reviews-issue/592-views-fields.md #9:  the columns waited for the read, while the headers' drop places and widths followed the draft
	it( 'takes a column resized while a read is out, its rows the read', async ()=>{
		const { page, hold } = create( rows(40) );
		const pending = hold();
		another( page );
		const read = page.onChangeView();
		page.profile().edit( view=>view.setWidth("name", 200) );
		await page.onChangeView();
		const width = ()=>page.view().fields.find( f=>f.name=="name" )!.flex;
		expect( [width(), pending.length] ).toEqual( ["0 0 200px", 1] );
		pending[0].settle();
		await read;
		expect( width() ).toBe( "0 0 200px" );
	} );

	//reviews-issue/592-views-fields.md #11:  the view of a read that failed counted as read
	it( 'reads again on any change once a read has failed', async ()=>{
		const { page, load, hold } = create( rows(40) );
		const pending = hold();
		page.profile().edit( view=>view.setDisplayed("email", true, "Email") );
		const failed = page.onChangeView();
		pending[0].settle( new Error("gateway down") );
		await failed;
		expect( [page.error(), page.data()] ).toEqual( [expect.any( Error ), []] );
		page.profile().edit( view=>view.setDisplayed("email", false) );
		const again = page.onChangeView();
		expect( load ).toHaveBeenCalledTimes( 2 );
		pending[1].settle();
		await again;
		expect( [page.error(), page.data().length] ).toEqual( [undefined, 25] );
	} );

	//reviews-issue/592-views-fields.md #10:  a list just routed compared with the view on screen, which a hidden column narrowed
	it( 'holds the rows the route resolved for a column hidden and shown again', async ()=>{
		const resolved = new Subject<object>();
		configure( undefined, provideRoute( {data: resolved} ) );
		const view = View.fromTable( {columns: ["name", "email"], sort: "name"}, schema );
		const profile = viewSet( [view] );
		const page = TestBed.createComponent( QLList ).componentInstance;
		const load = vi.spyOn( QLListResolver, 'load' ).mockImplementation( async ( _ql:any, data:any )=>({...data, results: {users: rows(3)}}) );
		await page.ngOnInit();
		resolved.next( {data: {routing: new ListRoute({path:'users'}), schema, profile, results: {users: rows(3)}}} );
		for( const shown of [false, true] ){
			profile.edit( v=>v.setDisplayed("email", shown) );
			await page.onChangeView();
		}
		expect( load ).not.toHaveBeenCalled();
		expect( page.displayedFields().map( f=>f.name ) ).toEqual( ["name", "email"] );
	} );

	//#592:  the Fields checklist's Select, where the rows on screen stay
	it( 'lets its rows be ticked once the view shows the selector, those ticked kept', async ()=>{
		const { page, load } = create( rows(3) );
		page.selections.set( new SelectionModel( false, [page.data()[1]] ) );
		page.profile().edit( view=>{ view.showSelector = true; } );
		await page.onChangeView();
		expect( [page.selections().isMultipleSelection(), page.selections().selected] ).toEqual( [true, [page.data()[1]]] );
		page.profile().edit( view=>{ view.showSelector = false; } );
		await page.onChangeView();
		expect( page.selections().isMultipleSelection() ).toBe( false );
		expect( load ).not.toHaveBeenCalled();
	} );

	//reviews-issue/591-views-filter-box.md #3:  each pause in a term the box cannot read asked for the same rows again
	it( 'asks again only for a filter that asks for other rows', async ()=>{
		const { page, load } = create( rows(40) );
		const typed = async ( filter:string )=>{
			page.profile().edit( view=>{ view.filter = filter; } );
			await page.onChangeView();
		};
		await typed( "nope:" );
		await typed( "nope:a" );
		expect( load ).not.toHaveBeenCalled();
		expect( page.view().filter ).toBe( "nope:a" );//the view on screen, which the chips read
		await typed( "nope:a name:b" );
		expect( load ).toHaveBeenCalledTimes( 1 );
	} );

	//reviews-issue/231-paging.md #12:  the reset to the first page is refresh()'s alone - Save and Delete go through it too
	it( 'keeps the page on Refresh, and starts over on a saved or deleted view', async ()=>{
		const { page, load } = create( rows(80) );
		await page.onPage( {startIndex: 50, pageSize: 25} );
		await page.onRefresh();
		expect( load.mock.calls[1][1].skip ).toBe( 50 );
		const mine = View.copy( page.view() );
		mine.name = "Mine";
		mine.number = 0;//as the settings panel emits a view under another name:  a new one
		await page.onViewSave( mine );
		expect( load.mock.calls[2][1].skip ).toBe( 0 );
		await page.onPage( {startIndex: 25, pageSize: 25} );
		await page.onViewDelete( page.profile().current() );//the view on screen:  Delete leaves the rows where it stays (#6)
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
			another( page );
			await page.onChangeView();
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
			const pending = hold();
			const older = turn( page, 25 );
			another( page );
			const newer = page.onChangeView();
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