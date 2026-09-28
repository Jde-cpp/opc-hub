//the vitest environment provides window/document but no localStorage - back the bare-global references with an
//in-memory one BEFORE importing jde-spa (see google-relogin.spec.ts).
if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { NavigationStart, provideRouter, Route, Router } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { filter } from 'rxjs';
import { vi } from 'vitest';
import { AccessService, AuthGuard } from 'jde-access';
import { HomeRouteService, SnackbarService } from 'jde-framework';
import { isNotFound, matchConfig, ProfileStore, RecentVisits, RouteStore } from 'jde-spa';
import { routes } from './app.routes';

//reviews/m3-closing.md #38, against the site's own route table:  #32's DetailResolver cell built a table of its own, so it
//could not see that resources has no ':slug' route and that '/access/resources/$new' matched nothing at all.  Every page is
//swapped for an empty one - what is under test is where a url lands and what its resolver does - bar the not-found page.
@Component( {template: ''} ) class Dummy{}
const stub = ( r:Route ):Route=>isNotFound( r ) ? r : { ...r, loadComponent: undefined, component: r.loadComponent || r.component ? Dummy : undefined, children: r.children?.map( stub ) };

describe( 'the site routes for a url that names no page', ()=>{
	const setup = async ()=>{
		const error = vi.fn();
		const schemaWithEnums = vi.fn( async ()=>{ throw new Error( 'no list in this test' ); } );
		TestBed.configureTestingModule( {providers: [
			provideRouter( routes.map(stub) ),
			{ provide: AuthGuard, useValue: {canActivate: ()=>true, canActivateChild: ()=>true} },
			{ provide: AccessService, useValue: {toCollectionName: ( s:string )=>s, schemaWithEnums} },
			{ provide: SnackbarService, useValue: {error, exception: vi.fn()} },
			{ provide: RecentVisits, useValue: {forget: vi.fn()} },
			{ provide: RouteStore, useValue: {getChildren: ()=>[], setChildren: vi.fn()} },
			{ provide: ProfileStore, useValue: {} }
		]} );
		const harness = await RouterTestingHarness.create();
		const router = TestBed.inject( Router );
		const starts:string[] = [];
		router.events.pipe( filter((e):e is NavigationStart=>e instanceof NavigationStart) ).subscribe( e=>starts.push(e.url) );
		const settle = ()=>new Promise( r=>setTimeout(r, 20) );
		return { harness, router, error, schemaWithEnums, starts, settle };
	};
	const landedOn = ( router:Router )=>{
		let route = router.routerState.snapshot.root;
		while( route.firstChild )
			route = route.firstChild;
		return route.routeConfig;
	};

	it( "shows the not-found page for resources' '$new', which has no detail route", async ()=>{
		const { harness, router } = await setup();
		await harness.navigateByUrl( '/access/resources/$new' );
		expect( isNotFound(landedOn(router)!) ).toBe( true );
		expect( harness.routeNativeElement?.textContent ).toContain( 'Page not found' );
		expect( harness.routeNativeElement?.textContent ).toContain( '/access/resources/$new' );
	} );

	it( 'shows the not-found page for a url outside every section', async ()=>{
		const { harness, router } = await setup();
		await harness.navigateByUrl( '/no/such/page' );
		expect( isNotFound(landedOn(router)!) ).toBe( true );
		expect( router.url ).toBe( '/no/such/page' );
	} );

	it( 'refuses a list name /access does not declare and goes back to /access', async ()=>{
		const { harness, router, error, schemaWithEnums, settle } = await setup();
		await harness.navigateByUrl( '/access/nonsense' );
		await settle();
		expect( error ).toHaveBeenCalledWith( 'There is no nonsense list.' );
		expect( schemaWithEnums ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/access' );
	} );

	it( "still refuses users' '$new' and goes back to the users list (#32)", async ()=>{
		const { harness, error, starts, settle } = await setup();
		await harness.navigateByUrl( '/access/users/$new' );
		await settle();
		expect( error ).toHaveBeenCalledWith( 'There is no Add for users.' );
		expect( starts ).toContain( '/access/users' );
	} );

	it( 'is no page to the readers of the route table', async ()=>{
		await setup();
		const tiles = await TestBed.inject( HomeRouteService ).children();
		expect( tiles.map(t=>t.title) ).not.toContain( 'Page not found' );
		expect( matchConfig(routes, ['apps', 'gateways']) ).toBeUndefined();//the crumb between Applications and an instance, and AppResolver's link test
		expect( matchConfig(routes, ['no', 'such']) ).toBeUndefined();
	} );
} );
