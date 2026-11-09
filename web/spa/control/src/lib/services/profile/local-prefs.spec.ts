//reviews/angular-refactor.md B14:  ProfileStore also kept the browser-local half - a static reader and an instance writer for the
//navbar, and the tab and view indexes under two key schemes, a page's bare key and `<collection>/viewIndex` (since `viewNumber`).  LocalPrefs is that
//half on its own, every entry `<owner>/<name>` as JSON.
import { TestBed } from '@angular/core/testing';
import { LocalPrefs, persistedTabIndex } from './local-prefs';

describe( 'LocalPrefs', ()=>{
	const prefs = new LocalPrefs();
	beforeEach( ()=>localStorage.clear() );

	it( 'keeps a value under <owner>/<name> as JSON, and forgets it on null', ()=>{
		expect( prefs.get('navbar', 'showBreadcrumbs', true) ).toBe( true );
		prefs.set( 'navbar', 'showBreadcrumbs', false );
		expect( localStorage.getItem('navbar/showBreadcrumbs') ).toBe( 'false' );
		expect( prefs.get('navbar', 'showBreadcrumbs', true) ).toBe( false );
		prefs.set( 'navbar', 'showBreadcrumbs', null );
		expect( localStorage.getItem('navbar/showBreadcrumbs') ).toBeNull();
	} );

	//a value edited by hand threw in the reader's field initialiser, and the page failed to open (420-clients-live-trend #13)
	it( 'answers the default for a value that is not JSON', ()=>{
		vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		localStorage.setItem( 'gatewayWatch/trendWindow', '10m' );
		expect( prefs.get( 'gatewayWatch', 'trendWindow', 10 ) ).toBe( 10 );
		vi.restoreAllMocks();
	} );
	//#587:  the view is remembered by its number, not its place - tabs reorder and names repeat
	it( 'keeps an owner\'s tab index and view number apart, and none of either as no entry', ()=>{
		prefs.setTabIndex( 'users', 2 );
		prefs.setViewNumber( 'users', 3 );
		expect( localStorage.getItem('users/tabIndex') ).toBe( '2' );
		expect( localStorage.getItem('users/viewNumber') ).toBe( '3' );
		expect( [prefs.tabIndex('users'), prefs.viewNumber('users')] ).toEqual( [2, 3] );
		prefs.setTabIndex( 'users', 0 );
		prefs.setViewNumber( 'users', 0 );
		expect( localStorage.getItem('users/tabIndex') ).toBeNull();
		expect( localStorage.getItem('users/viewNumber') ).toBeNull();
		expect( [prefs.tabIndex('users'), prefs.viewNumber('users')] ).toEqual( [0, 0] );
	} );
} );

//reviews/angular-refactor.md A4:  ten pages read the index into a field, forwarded selectedIndexChange into it and wrote it back in
//ngOnDestroy - and node-detail wrote a key per node that nothing ever read.
describe( 'persistedTabIndex', ()=>{
	afterEach( ()=>localStorage.removeItem('somePage/tabIndex') );
	const create = ()=>TestBed.runInInjectionContext( ()=>persistedTabIndex('somePage') );

	it( 'restores the stored index', ()=>{
		localStorage.setItem( 'somePage/tabIndex', '2' );
		expect( create()() ).toBe( 2 );
	} );

	it( 'writes the index back as it changes, and forgets the first tab', ()=>{
		const index = create();
		index.set( 3 );
		TestBed.tick();
		expect( localStorage.getItem('somePage/tabIndex') ).toBe( '3' );
		index.set( 0 );
		TestBed.tick();
		expect( localStorage.getItem('somePage/tabIndex') ).toBeNull();
	} );
} );