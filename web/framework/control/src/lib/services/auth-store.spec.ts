import { TestBed } from '@angular/core/testing';
import { RouteItem, RouteStore } from 'jde-spa';
import { AuthStore } from './auth-store';

//angular-review3 L3: RouteStore caches the NAMES of the rows the last user browsed, and nothing dropped them at logout.
//AuthStore.logout is the funnel - the navbar button reaches it through AppService/GatewayService, and so do both 401
//handlers once silent renewal has given up.
describe( 'AuthStore.logout clears the browsed route names', ()=>{
	beforeEach( ()=>localStorage.clear() );

	it( 'leaves no cached children behind', ()=>{
		const routeStore = TestBed.inject( RouteStore );
		routeStore.setChildren( 'users', [new RouteItem({title:'Alice', path:'alice', icon:''})] );
		expect( localStorage.getItem('users') ).not.toBeNull();

		TestBed.inject( AuthStore ).logout();

		expect( localStorage.getItem('users') ).toBeNull();
		expect( routeStore.entries().size ).toBe( 0 );
	} );
} );