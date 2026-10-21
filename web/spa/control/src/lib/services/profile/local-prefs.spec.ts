//reviews/angular-refactor.md B14:  ProfileStore also kept the browser-local half - a static reader and an instance writer for the
//navbar, and the tab and view indexes under two key schemes, a page's bare key and `<collection>/viewIndex`.  LocalPrefs is that
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

	it( 'keeps an owner\'s tab and view indexes apart, and the first of each as no entry', ()=>{
		prefs.setTabIndex( 'users', 2 );
		prefs.setViewIndex( 'users', 1 );
		expect( localStorage.getItem('users/tabIndex') ).toBe( '2' );
		expect( localStorage.getItem('users/viewIndex') ).toBe( '1' );
		expect( [prefs.tabIndex('users'), prefs.viewIndex('users')] ).toEqual( [2, 1] );
		prefs.setTabIndex( 'users', 0 );
		expect( localStorage.getItem('users/tabIndex') ).toBeNull();
		expect( prefs.tabIndex('users') ).toBe( 0 );
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