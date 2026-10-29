import { TestBed } from '@angular/core/testing';
import { RouteItem, RouteStore, User } from 'jde-spa';
import { fakeJwt } from 'jde-spa/testing';
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

describe( 'AuthStore load', ()=>{
	beforeEach( ()=>localStorage.clear() );

	//a refresh signed a Google user in again with the stored jwt:  the constructor dropped the stored session.
	it( "keeps a Google user's session across a refresh", ()=>{
		const jwt = fakeJwt( {email:"a@b.c", name:"A B", exp:2000000000, iss:"https://accounts.google.com"} );
		const before = TestBed.inject( AuthStore );
		before.append( new User(jwt) );
		before.append( {sessionId:'92d9ab0b'} );

		const after = TestBed.runInInjectionContext( ()=>new AuthStore() );
		expect( after.user()?.sessionId ).toBe( '92d9ab0b' );
		expect( after.user()?.authorization ).toBe( '92d9ab0b' );
	} );
} );

//Each window read the stored user once, at load:  one renewed its session and the other kept the stale one.
describe( 'AuthStore across windows', ()=>{
	beforeEach( ()=>localStorage.clear() );
	const otherWindow = ( key:string|null, newValue:string|null )=>window.dispatchEvent( new StorageEvent('storage', {key, newValue}) );

	it( "takes another window's session", ()=>{
		const jwt = fakeJwt( {email:"a@b.c", name:"A B", exp:2000000000, iss:"https://accounts.google.com"} );
		const authStore = TestBed.inject( AuthStore );
		authStore.append( new User({jwt, sessionId:'a1b2c3d4'}) );
		otherWindow( 'user', JSON.stringify(new User({jwt, sessionId:'0badf00d'})) );
		expect( authStore.user()?.sessionId ).toBe( '0badf00d' );
		expect( authStore.user()?.email ).toBe( 'a@b.c' );
	} );

	it( "follows another window's logout", ()=>{
		const authStore = TestBed.inject( AuthStore );
		authStore.append( {sessionId:'a1b2c3d4'} );
		otherWindow( 'user', null );
		expect( authStore.user() ).toBeUndefined();
	} );

	it( "ignores another key", ()=>{
		const authStore = TestBed.inject( AuthStore );
		authStore.append( {sessionId:'a1b2c3d4'} );
		otherWindow( 'theme', 'dark' );
		expect( authStore.user()?.sessionId ).toBe( 'a1b2c3d4' );
	} );
} );