import { RouteItem } from '../pages/component-sidenav/route-item';
import { RouteStore } from './route-store';

//angular-review3 L3: the cached children are the NAMES of the rows the last user browsed, and nothing dropped them at
//logout - the navbar search read them straight back out of localStorage for whoever logged in next.
describe( 'RouteStore.clear', ()=>{
	beforeEach( ()=>localStorage.clear() );

	const child = ( title:string )=>new RouteItem( {title, path:title.toLowerCase(), icon:''} );

	it( 'drops every cached url, the key list and the in-memory map', ()=>{
		const store = new RouteStore();
		store.setChildren( 'users', [child('Alice'), child('Bob')] );
		store.setChildren( 'gateways/gw1', [child('Line 1')] );
		expect( [...store.entries().keys()] ).toEqual( ['users', 'gateways/gw1'] );

		store.clear();

		expect( localStorage.getItem('users') ).toBeNull();
		expect( localStorage.getItem('gateways/gw1') ).toBeNull();
		expect( localStorage.getItem(RouteStore.keysKey) ).toBeNull();
		expect( store.entries().size ).toBe( 0 );
	} );

	it( 'leaves nothing for a freshly constructed store to hydrate', ()=>{
		const store = new RouteStore();
		store.setChildren( 'roles', [child('Admin')] );
		store.clear();
		expect( new RouteStore().getChildren('roles') ).toEqual( [] );//a new user's navbar must not see the last one's rows
	} );

	it( 'keeps going when one key cannot be removed', ()=>{
		const store = new RouteStore();
		store.setChildren( 'users', [child('Alice')] );
		store.setChildren( 'roles', [child('Admin')] );
		const removeItem = localStorage.removeItem.bind( localStorage );
		localStorage.removeItem = ( k:string )=>{ if( k=='users' ) throw new Error( "site data blocked" ); removeItem( k ); };
		try{
			expect( ()=>store.clear() ).not.toThrow();//one bad key must not strand the rest
			expect( localStorage.getItem('roles') ).toBeNull();
			expect( localStorage.getItem(RouteStore.keysKey) ).toBeNull();
		}
		finally{ localStorage.removeItem = removeItem; }
	} );
} );

//reviews/angular-refactor.md B12:  writers keyed by whatever they held - '/apps', joined UrlSegments, a bare 'users' - and the
//readers tried each spelling in turn.  One key now, the absolute url with no slash at either end.
describe( 'RouteStore.key', ()=>{
	beforeEach( ()=>localStorage.clear() );

	it( 'spells a url and its segments the same', ()=>{
		expect( RouteStore.key('/apps') ).toBe( 'apps' );
		expect( RouteStore.key('access/users/') ).toBe( 'access/users' );
		expect( RouteStore.key(['/access', 'users']) ).toBe( 'access/users' );
		expect( RouteStore.key([{path: 'apps'}, {path: 'gateways'}] as any) ).toBe( 'apps/gateways' );
		expect( RouteStore.key('') ).toBe( '' );
	} );

	it( 'keeps one entry, whichever spelling wrote and reads it', ()=>{
		const store = new RouteStore();
		store.setChildren( '/access/users', [new RouteItem( {title: 'Alice', path: 'alice'} )] );
		expect( store.getChildren(['access', 'users']).map( c=>c.title ) ).toEqual( ['Alice'] );
		expect( new RouteStore().getChildren('access/users/').map( c=>c.title ) ).toEqual( ['Alice'] );//from localStorage
		expect( [...store.entries().keys()] ).toEqual( ['access/users'] );
	} );
} );