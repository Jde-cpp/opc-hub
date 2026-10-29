import { TestBed } from '@angular/core/testing';
import { provideRouter, Router, Routes } from '@angular/router';
import { signal, Provider } from '@angular/core';
import { NavigationFocusService } from '../navigation-focus/navigation-focus-service';
import { SearchService } from '../../services/search/search-service';
import { SearchResult } from '../../services/search/search-provider';
import { Favorite, NavBar } from './navbar';
import { IPROFILE_SERVICE } from '../../services/profile/profile-service';
import { RecentVisits } from '../../services/recent-visits';
import { RouteStore } from '../../services/route-store';
import { HELP_TOPICS, HelpAnchor } from '../../services/help/help-topic';
import { Blank, flush } from 'jde-spa/testing';

//The bar over the router `routes` builds, with no search and nothing to focus.
const configure = ( routes:Routes, ...providers:Provider[] )=>{
	TestBed.configureTestingModule({ providers: [
		provideRouter( routes ),
		{ provide: NavigationFocusService, useValue: {} },
		{ provide: SearchService, useValue: {search: ()=>Promise.resolve([])} },
		...providers
	]});
};

//review3 L9: onSearch called event.preventDefault() and THEN tested event.defaultPrevented - the flag it had just set - so
//the Enter fallback below it was unreachable and Enter with nothing highlighted did nothing.
describe( 'NavBar.onSearch', ()=>{
	const first:SearchResult = {title:'Alice', route:'/access/users/alice'} as SearchResult;

	//NavBar's searchTrigger/searchInput are viewChild() and searchResults is a read-only toSignal, so a rendered fixture
	//would need the whole autocomplete; the handler under test reads only the four members overridden here.
	class TestNavBar extends NavBar{
		override searchTrigger = signal<any>( undefined );
		override searchResults = signal<SearchResult[]>( [first] );
		override searchInput = signal<any>( {nativeElement: {blur: ()=>{}}} );
		selected = vi.fn<( result:SearchResult )=>void>();
		override onSearchSelected( result:SearchResult ){ this.selected( result ); }
	}

	const create = ()=>{
		configure( [{path: 'x', children: [], title: 'X'}] );
		return TestBed.runInInjectionContext( ()=>new TestNavBar() );//NavigationFocusService is inject()ed since C5
	};

	const enter = ()=>new KeyboardEvent( 'keydown', {key:'Enter', cancelable:true} );

	it( 'navigates to the first result when nothing is highlighted', ()=>{
		const navbar = create();
		const event = enter();
		navbar.onSearch( event );
		expect( navbar.selected ).toHaveBeenCalledExactlyOnceWith( first );
		expect( event.defaultPrevented ).toBe( true );//still swallowed - the input must never submit
	} );

	it( 'leaves the highlighted option to the autocomplete', ()=>{
		const navbar = create();
		navbar.searchTrigger.set( {panelOpen: true, activeOption: {}} );
		navbar.onSearch( enter() );
		expect( navbar.selected ).not.toHaveBeenCalled();
	} );

	it( 'honours an event another handler already consumed', ()=>{
		const navbar = create();
		const event = enter();
		event.preventDefault();//as the autocomplete trigger does once it has selected the active option
		navbar.onSearch( event );
		expect( navbar.selected ).not.toHaveBeenCalled();
	} );

	it( 'does nothing when there are no results', ()=>{
		const navbar = create();
		navbar.searchResults.set( [] );
		navbar.onSearch( enter() );
		expect( navbar.selected ).not.toHaveBeenCalled();
	} );

	//reviews/opc-server-search.md #11:  a disabled notice ("Search is unavailable for …") routes to [] - the app root - if picked.
	const notice:SearchResult = {title:'Search is unavailable for External', route:[], disabled:true, source:'nodes'};
	it( 'skips a disabled notice for the first real result', ()=>{
		const navbar = create();
		navbar.searchResults.set( [notice, first] );
		navbar.onSearch( enter() );
		expect( navbar.selected ).toHaveBeenCalledExactlyOnceWith( first );
	} );

	it( 'does nothing when the only result is a disabled notice', ()=>{
		const navbar = create();
		navbar.searchResults.set( [notice] );
		navbar.onSearch( enter() );
		expect( navbar.selected ).not.toHaveBeenCalled();
	} );
} );

//reviews/m3-closing.md #8:  a favorite's route is the serialized - percent-encoded - url it was saved on, and a string
//routerLink encodes its '%' again, so a favorite saved on a node under a browse name with a space never reopened it.
describe( 'NavBar.favoriteMenus', ()=>{
	it( "links each favorite to the url it was saved on, encoded segments and all", ()=>{
		configure( [{path: 'x', children: [], title: 'X'}] );
		const navbar = TestBed.runInInjectionContext( ()=>new NavBar() );
		const route = '/gateways/gw/kep/Simulation%20Examples/Functions';
		navbar.favorites.set( [{name: 'Functions', route}, {name: 'Lamp', folderName: 'Plant', route: '/gateways/gw/kep/A%2BB'}] );
		const router = TestBed.inject( Router );
		const [top, folder] = navbar.favoriteMenus() as any[];
		expect( top.link && router.serializeUrl(top.link) ).toBe( route );
		expect( top.route ).toBe( route );//the stored string stays what `existing` and onFavoriteChange compare
		expect( folder.items[0].link && router.serializeUrl(folder.items[0].link) ).toBe( '/gateways/gw/kep/A%2BB' );
	} );
} );

//reviews/m3-closing.md #17:  the navbar loaded favorites once per document, so a sign-in, a re-login or another user signing in
//on the same browser kept the list it had - and the next star saved that list over the signed-in user's own.
describe( 'NavBar favorites follow the signed-in user', ()=>{
	const lists:Record<string,Favorite[]> = { alice: [{name: "Alice's", route: "/access/users"}], bob: [{name: "Bob's", route: "/gateways"}] };
	const create = ()=>{
		const userKey = signal<string|undefined>( "alice" );
		const save = vi.fn( async ()=>{} );
		configure( [{path: 'x', children: [], title: 'X'}], { provide: IPROFILE_SERVICE, useValue: {userKey, save, load: async ( key:string )=>key=="favorites" && userKey() ? JSON.stringify(lists[userKey()!]) : null} } );
		const navbar = TestBed.runInInjectionContext( ()=>new NavBar() );
		return { navbar, userKey, save };
	};
	const settle = async ()=>{ TestBed.tick(); await flush(); };

	it( "loads the next user's list when the user changes", async ()=>{
		const { navbar, userKey } = create();
		navbar.ngOnInit();
		await settle();
		expect( navbar.favorites()?.map(f=>f.name) ).toEqual( ["Alice's"] );
		userKey.set( "bob" );
		await settle();
		expect( navbar.favorites()?.map(f=>f.name) ).toEqual( ["Bob's"] );
	} );

	it( "saves a star into the signed-in user's own list, not the one the bar had", async ()=>{
		const { navbar, userKey, save } = create();
		navbar.ngOnInit();
		await settle();
		userKey.set( "bob" );
		navbar.route.set( "/apps" );
		await navbar.onFavoriteChange( {name: "Apps", route: "/apps"} );
		const saved = save.mock.calls.at( -1 ) as unknown as [string, string];
		expect( JSON.parse(saved[1]).map((f:Favorite)=>f.name) ).toEqual( ["Bob's", "Apps"] );
	} );
} );

//reviews/m3-closing.md #38:  the site's not-found catch-all is a titled top-level route with no children, so it passed every
//filter the default favorites use;  and the url that reached it is a mistake, not a page to put on Recently visited.
describe( 'NavBar and the not-found route', ()=>{
	const create = ()=>{
		const visit = vi.fn( async ()=>{} );
		configure( [
			{path: 'x', title: 'X', children: [{path: 'y', component: Blank}]},
			{path: 'z', title: 'Z', component: Blank},
			{path: '**', title: 'Page not found', component: Blank, data: {notFound: true}}
		], { provide: RecentVisits, useValue: {visit} } );
		return { navbar: TestBed.runInInjectionContext( ()=>new NavBar() ), visit };
	};

	it( 'leaves it out of the default favorites', ()=>{
		const { navbar } = create();
		expect( navbar.defaultFavorites.map(f=>f.route) ).toEqual( ['/z'] );
	} );

	it( 'does not record a not-found url as visited', async ()=>{
		const { navbar, visit } = create();
		navbar.ngOnInit();
		const router = TestBed.inject( Router );
		await router.navigateByUrl( '/x/y' );
		expect( visit ).toHaveBeenCalledTimes( 1 );
		await router.navigateByUrl( '/no/such/page' );
		expect( visit ).toHaveBeenCalledTimes( 1 );
		expect( navbar.crumbs().slice(1).every(c=>!c.path) ).toBe( true );//no crumb links to the catch-all
	} );
} );

//reviews-issue/251-resource-page.md #13:  a crumb is named from the children RouteStore keeps under its parent url, and the bare
//last segment was tried too - so the access schema's `roles` resource took the section's Roles list's name.  Every key is the
//absolute url now (reviews/angular-refactor.md B12), so there is no bare one to try.
describe( 'NavBar crumbs', ()=>{
	const create = ()=>{
		configure( [{path: 'access', children: [
			{path: 'users/:slug', component: Blank},
			{path: 'resources/:schema/:slug', component: Blank},
			{path: ':collectionDisplay', component: Blank}
		]}], { provide: RecentVisits, useValue: {visit: vi.fn( async ()=>{} )} } );
		const store = TestBed.inject( RouteStore );
		store.setChildren( "access", [{path: "users", title: "Users"}, {path: "roles", title: "Roles"}] );
		store.setChildren( "/access/users", [{path: "alice", title: "Alice Ng"}] );//a writer's leading slash is the same key
		store.setChildren( "access/resources", [{path: "access/roles", title: "role table"}] );
		const navbar = TestBed.runInInjectionContext( ()=>new NavBar() );
		navbar.ngOnInit();
		return navbar;
	};
	const titles = ( navbar:NavBar )=>navbar.crumbs().map( c=>c.title );

	it( "names a ':param' segment from a farther parent, not a bare key spelled the same", async ()=>{
		const navbar = create();
		await TestBed.inject( Router ).navigateByUrl( '/access/resources/access/roles' );
		expect( titles(navbar).at(-1) ).toBe( "role table" );
	} );

	it( "names a row from its list's entry, under the list's url", async ()=>{
		const navbar = create();
		await TestBed.inject( Router ).navigateByUrl( '/access/users/alice' );
		expect( titles(navbar).slice(-2) ).toEqual( ["Users", "Alice Ng"] );
	} );
} );

//The ? button opens the section the page names (HelpAnchor) - a section of the url's own topic, never of the help index.
describe( 'NavBar help', ()=>{
	const create = ()=>{
		configure( [], { provide: RecentVisits, useValue: {visit: vi.fn( async ()=>{} )} }, { provide: HELP_TOPICS, multi: true, useValue: [{id: 'gateways', title: 'Gateways', url: 'gateways.md', routes: ['apps/gateways/:instance']}, {id: 'gateways/groups', title: 'Historian groups', url: 'groups.md', routes: ['apps/gateways/:instance/groups']}] } );
		const navbar = TestBed.runInInjectionContext( ()=>new NavBar() );
		TestBed.inject( HelpAnchor ).fragment.set( 'endpoint' );
		return navbar;
	};

	it( "opens the section the page names", ()=>{
		const navbar = create();
		navbar.route.set( '/apps/gateways/OpcHub.debug' );
		expect( navbar.helpRoute() ).toEqual( ['/help', 'gateways'] );
		expect( navbar.helpFragment() ).toBe( 'endpoint' );
	} );

	it( "opens a page split out of another's by its segments", ()=>{
		const navbar = create();
		navbar.route.set( '/apps/gateways/OpcHub.debug/groups/pump0' );
		expect( navbar.helpRoute() ).toEqual( ['/help', 'gateways', 'groups'] );//not 'gateways/groups', which a link carries as %2F
	} );

	it( "names no section on the help index", ()=>{
		const navbar = create();
		navbar.route.set( '/help/gateways' );
		expect( navbar.helpRoute() ).toEqual( ['/help'] );
		expect( navbar.helpFragment() ).toBeUndefined();
	} );
} );