//Spec helpers, imported as `jde-spa/testing` (a tsconfig path create-workspace.sh writes).  Never exported from public-api and
//excluded from the library build:  this folder is for the specs alone, here and in every library above jde-spa.
import { Component, Provider, signal } from '@angular/core';
import { ActivatedRoute, Route, Routes } from '@angular/router';
import { BehaviorSubject, isObservable, NEVER, Observable } from 'rxjs';
import { ComponentPageTitle } from '../lib/pages/component-page-title/component-page-title';
import { IProfileService } from '../lib/services/profile/profile-service';
import { isNotFound } from '../lib/services/route-utils';
import { ScreenSize } from '../lib/services/screen-size';

//ActivatedRoute.  `data` and `params` are streams:  a value is a BehaviorSubject's, an Observable is used as it is - pass a Subject
//to move the route on from the test - and an absent one never emits.
export type RouteShape = { data?:object|Observable<object>; params?:object|Observable<object>; snapshot?:object; parent?:object; routeConfig?:object };
export function routeStub( {data, params, snapshot={}, parent, routeConfig={}}:RouteShape={} ):ActivatedRoute{
	const stream = ( x:object|Observable<object>|undefined )=>x===undefined ? NEVER : isObservable( x ) ? x : new BehaviorSubject( x );
	return <ActivatedRoute><unknown>{ data: stream( data ), params: stream( params ), snapshot, parent, routeConfig };
}
export const provideRoute = ( shape?:RouteShape ):Provider=>({ provide: ActivatedRoute, useValue: routeStub( shape ) });

//Router, for a page that navigates:  `url` is what it reads, and navigate/navigateByUrl record their calls and succeed.
export const routerStub = ( url='/' )=>({ url, navigate: vi.fn().mockResolvedValue( true ), navigateByUrl: vi.fn().mockResolvedValue( true ) });

export const pageTitleStub:Provider = { provide: ComponentPageTitle, useValue: {} };

//ScreenSize at a width:  a spec's document has no _general.scss, so the real service would warn in every test that renders a
//navbar, a sidenav or a list toolbar.  A phone is small too.
export const screenSize = ( width:'wide'|'small'|'extra-small'='wide' ):Provider=>
	({ provide: ScreenSize, useValue: {isSmall: signal( width!='wide' ), isExtraSmall: signal( width=='extra-small' )} });

//A route's page, for a router test about where a url lands rather than what the page draws.
@Component( {template: ''} ) export class Blank{}
//`routes` with every page swapped for Blank - bar the not-found page, which says where a url landed.
export function blankRoutes( routes:Routes ):Routes{
	const blank = ( r:Route ):Route=>isNotFound( r ) ? r : { ...r, loadComponent: undefined, component: r.loadComponent || r.component ? Blank : undefined, children: r.children?.map( blank ) };
	return routes.map( blank );
}

//Waits, for code that awaits on its own.  flush:  `turns` macrotasks, past promise chains and zero timers.  stable:  a fixture
//rendered `passes` times, waiting each time, for a page that renders between awaits.  A microtask (`await Promise.resolve()`)
//never suffices once a timer or a redirect is in play - the router's second navigation is one.
export async function flush( turns=1 ):Promise<void>{
	for( let i=0; i<turns; ++i )
		await new Promise( r=>setTimeout(r) );
}
export async function stable( fixture:{ detectChanges():void, whenStable():Promise<unknown> }, passes=3 ):Promise<void>{
	for( let i=0; i<passes; ++i ){
		fixture.detectChanges();
		await fixture.whenStable();
		await flush();
	}
}

//IProfileService over `rows`, signed in as `user` until setUser says otherwise.
export class MockProfileService implements IProfileService{
	constructor( user:string|undefined='u1' ){ this.#user.set( user ); }
	#user = signal<string|undefined>( undefined );
	userKey = this.#user.asReadonly();
	setUser( u:string|undefined ){ this.#user.set( u ); }
	rows = new Map<string,string>();
	load = vi.fn( async ( key:string )=>this.rows.has(key) ? this.rows.get(key)! : null );
	save = vi.fn( async ( key:string, value:string|null )=>{ value==null ? this.rows.delete(key) : this.rows.set(key, value); } );
}
//ProfileStore, for a page that loads its settings:  load answers `rows[key]` or the default, a class array is empty, and the
//writes are recorded.
export const profileStoreStub = ( rows:Record<string,unknown>={} )=>({
	load: vi.fn( async <T>( key:string, dflt:T )=>(key in rows ? rows[key] : dflt) as T ),
	loadArray: vi.fn( async ()=>[] ),
	save: vi.fn().mockResolvedValue( undefined )
});

//sessionStorage over the Map returned, until vi.unstubAllGlobals().
export function fakeSessionStorage():Map<string,string>{
	const stored = new Map<string,string>();
	vi.stubGlobal( 'sessionStorage', { getItem: ( k:string )=>stored.get(k) ?? null, setItem: ( k:string, v:string )=>{ stored.set(k, v); }, removeItem: ( k:string )=>{ stored.delete(k); } } );
	return stored;
}

//A console level kept quiet for a test that expects it to speak;  the spy says what it was told.  vi.restoreAllMocks() undoes it.
export const silence = ( level:'log'|'info'|'warn'|'error'='warn' )=>vi.spyOn( console, level ).mockImplementation( ()=>{} );

//An unsigned JWT carrying `claims` - what decodeJwt reads;  nothing here verifies the signature.
export function fakeJwt( claims:object ):string{
	const b64url = ( o:object )=>btoa( JSON.stringify(o) ).replace( /\+/g, '-' ).replace( /\//g, '_' ).replace( /=+$/, '' );
	return `${b64url( {alg: 'RS256'} )}.${b64url( claims )}.signature`;
}