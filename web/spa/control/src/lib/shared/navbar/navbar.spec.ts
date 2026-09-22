if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { provideRouter, Router } from '@angular/router';
import { signal } from '@angular/core';
import { vi } from 'vitest';
import { NavigationFocusService } from '../navigation-focus/navigation-focus-service';
import { SearchService } from '../../services/search/search-service';
import { SearchResult } from '../../services/search/search-provider';
import { Favorite, NavBar } from './navbar';
import { IPROFILE_SERVICE } from '../../services/profile/profile-service';

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
		selected:SearchResult[] = [];
		override onSearchSelected( result:SearchResult ){ this.selected.push( result ); }
	}

	const create = ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			provideRouter( [{path: 'x', children: [], title: 'X'}] ),
			{ provide: NavigationFocusService, useValue: {} },
			{ provide: SearchService, useValue: {search: ()=>Promise.resolve([])} }
		]});
		return TestBed.runInInjectionContext( ()=>new TestNavBar() );//NavigationFocusService is inject()ed since C5
	};

	const enter = ()=>new KeyboardEvent( 'keydown', {key:'Enter', cancelable:true} );

	it( 'navigates to the first result when nothing is highlighted', ()=>{
		const navbar = create();
		const event = enter();
		navbar.onSearch( event );
		expect( navbar.selected ).toEqual( [first] );
		expect( event.defaultPrevented ).toBe( true );//still swallowed - the input must never submit
	} );

	it( 'leaves the highlighted option to the autocomplete', ()=>{
		const navbar = create();
		navbar.searchTrigger.set( {panelOpen: true, activeOption: {}} );
		navbar.onSearch( enter() );
		expect( navbar.selected ).toEqual( [] );
	} );

	it( 'honours an event another handler already consumed', ()=>{
		const navbar = create();
		const event = enter();
		event.preventDefault();//as the autocomplete trigger does once it has selected the active option
		navbar.onSearch( event );
		expect( navbar.selected ).toEqual( [] );
	} );

	it( 'does nothing when there are no results', ()=>{
		const navbar = create();
		navbar.searchResults.set( [] );
		navbar.onSearch( enter() );
		expect( navbar.selected ).toEqual( [] );
	} );
} );

//reviews/m3-closing.md #8:  a favorite's route is the serialized - percent-encoded - url it was saved on, and a string
//routerLink encodes its '%' again, so a favorite saved on a node under a browse name with a space never reopened it.
describe( 'NavBar.favoriteMenus', ()=>{
	it( "links each favorite to the url it was saved on, encoded segments and all", ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			provideRouter( [{path: 'x', children: [], title: 'X'}] ),
			{ provide: NavigationFocusService, useValue: {} },
			{ provide: SearchService, useValue: {search: ()=>Promise.resolve([])} }
		]});
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
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			provideRouter( [{path: 'x', children: [], title: 'X'}] ),
			{ provide: NavigationFocusService, useValue: {} },
			{ provide: SearchService, useValue: {search: ()=>Promise.resolve([])} },
			{ provide: IPROFILE_SERVICE, useValue: {userKey, save, load: async ( key:string )=>key=="favorites" && userKey() ? JSON.stringify(lists[userKey()!]) : null} }
		]});
		const navbar = TestBed.runInInjectionContext( ()=>new NavBar() );
		return { navbar, userKey, save };
	};
	const settle = async ()=>{ TestBed.tick(); for( let i=0; i<5; ++i ) await Promise.resolve(); };

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
