import { TestBed } from '@angular/core/testing';
import { NavigationStart } from '@angular/router';
import { filter } from 'rxjs';
import { AccessService, AuthGuard } from 'jde-access';
import { HomeRouteService } from 'jde-framework';
import { isNotFound, matchConfig, ProfileStore, RouteStore } from 'jde-spa';
import { routes } from './app.routes';
import { blankRoutes, flush, profileStoreStub } from 'jde-spa/testing';
import { openRoute } from 'jde-framework/testing';

//reviews/m3-closing.md #38, against the site's own route table:  #32's DetailResolver cell built a table of its own, so it
//could not see that resources has no ':slug' route and that '/access/resources/$new' matched nothing at all.  Every page is
//swapped for an empty one - what is under test is where a url lands and what its resolver does - bar the not-found page.
describe( 'the site routes for a url that names no page', ()=>{
	const setup = async ()=>{
		const schemaWithEnums = vi.fn( async ()=>{ throw new Error( 'no list in this test' ); } );
		const { harness, router, error, leaf } = await openRoute( {routes: blankRoutes( routes ), providers: [
			{ provide: AuthGuard, useValue: {canActivate: ()=>true, canActivateChild: ()=>true} },
			{ provide: AccessService, useValue: {toCollectionName: ( s:string )=>s, schemaWithEnums} },
			{ provide: RouteStore, useValue: {getChildren: ()=>[], setChildren: vi.fn()} },
			{ provide: ProfileStore, useValue: profileStoreStub() }
		]} );
		const starts:string[] = [];
		router.events.pipe( filter((e):e is NavigationStart=>e instanceof NavigationStart) ).subscribe( e=>starts.push(e.url) );
		const settle = ()=>flush();//the redirect is a second navigation
		return { harness, router, error, schemaWithEnums, starts, settle, landedOn: ()=>leaf().routeConfig };
	};

	it( "shows the not-found page for resources' '$new', which has no detail route", async ()=>{
		const { harness, landedOn } = await setup();
		await harness.navigateByUrl( '/access/resources/$new' );
		expect( isNotFound(landedOn()!) ).toBe( true );
		expect( harness.routeNativeElement?.textContent ).toContain( 'Page not found' );
		expect( harness.routeNativeElement?.textContent ).toContain( '/access/resources/$new' );
	} );

	it( 'shows the not-found page for a url outside every section', async ()=>{
		const { harness, router, landedOn } = await setup();
		await harness.navigateByUrl( '/no/such/page' );
		expect( isNotFound(landedOn()!) ).toBe( true );
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