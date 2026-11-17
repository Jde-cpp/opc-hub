//config.json (docs/demo/spec.md *The public origin*, #484):  a file a front serves beside index.html, layered over the build's
//environment before any service starts.  Absent, or a page answered in its place, the site is as the build made it - the hub on
//1967, the cards at home, the sign-in shown.  Present, the hub is called on the page's own port, as is every instance of its
//process, the empty path goes to `home`, `banner` heads every page, `login` false leaves no sign-in and a section among `closed`
//has no page, tile or search result.
import { CUSTOM_ELEMENTS_SCHEMA } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { Routes } from '@angular/router';
import { provideHttpClient } from '@angular/common/http';
import { AuthGuard } from 'jde-access';
import { AppService, AUTH_STORE, HomeRouteService } from 'jde-framework';
import { IENVIRONMENT, NavBar, RouteSearchProvider } from 'jde-spa';
import { App } from './app';
import { applyConfig, offered, routes } from './app.routes';
import { EnvironmentService } from './services/environment-service';
import { environment as built } from '../environments/environment';
import { blankRoutes, flush, silence } from 'jde-spa/testing';
import { anonymousAuthStore, openRoute } from 'jde-framework/testing';

const build = built.applicationServer;//the build's own:  1967, on the page's host
const floor = '/apps/gateways/hub/lines/plant/floor';
const demo = { applicationServer: {port: null}, home: floor, banner: 'Live demo: a plant of five emulated PLCs, read-only.', login: false, closed: ['access', 'apps'] };
const json = ( body:unknown )=>new Response( typeof body=='string' ? body : JSON.stringify( body ), {status: 200, headers: {'Content-Type': 'application/json'}} );
//an EnvironmentService that read `answer` as its config.json:  a Response, or the fetch's failure
const load = async ( answer:Response|Error )=>{
	const fetch = vi.fn( async ()=>{ if( answer instanceof Error ) throw answer; return answer; } );
	vi.stubGlobal( 'fetch', fetch );
	const info = silence( 'info' ), warn = silence();
	const environment = new EnvironmentService();
	await environment.load();
	return { environment, fetch, info, warn };
};
const absent = ()=>new Response( 'Not Found', {status: 404, headers: {'Content-Type': 'text/plain'}} );

describe( 'config.json', ()=>{
	afterEach( ()=>{
		vi.unstubAllGlobals();
		vi.restoreAllMocks();
	});

	it( 'is asked for beside the page', async ()=>{
		const { fetch } = await load( absent() );
		expect( String((fetch.mock.calls[0] as unknown[])[0]) ).toBe( new URL('config.json', document.baseURI).href );
	} );

	it( 'absent, leaves the build\'s environment, with one console line', async ()=>{
		const { environment, info, warn } = await load( absent() );
		expect( environment.get('applicationServer') ).toEqual( build );
		expect( environment.get('home') ).toBeUndefined();
		expect( environment.get('banner') ).toBeUndefined();
		expect( environment.get('login') ).toBeUndefined();
		expect( environment.get('closed') ).toBeUndefined();
		expect( info ).toHaveBeenCalledTimes( 1 );
		expect( warn ).not.toHaveBeenCalled();
	} );

	it( 'is absent when the page is answered in its place, the fetch fails or the body is no JSON object', async ()=>{
		const page = new Response( JSON.stringify(demo), {status: 200, headers: {'Content-Type': 'text/html'}} );//a front's fallback, or ng serve's:  never read, whatever it holds
		for( const answer of [page, new Error('Failed to fetch'), json('<!doctype html>'), json([demo]), json('null'), new Response(JSON.stringify(demo), {status: 203, headers: {'Content-Type': 'application/json'}})] ){
			const { environment, info } = await load( answer );
			expect( environment.get('applicationServer') ).toEqual( build );
			expect( environment.get('home') ).toBeUndefined();
			expect( info ).toHaveBeenCalledTimes( 1 );
			vi.restoreAllMocks();
		}
	} );

	it( 'present, sets the port, the home, the banner, the login and the closed sections over the build\'s', async ()=>{
		const { environment, info, warn } = await load( new Response(JSON.stringify(demo), {status: 200, headers: {'Content-Type': 'application/json; charset=utf-8'}}) );
		expect( environment.get('applicationServer') ).toEqual( {...build, port: null, listenerPort: build.port} );//the build's port the hub's listener, the front's own
		expect( environment.get('home') ).toBe( floor );
		expect( environment.get('banner') ).toBe( demo.banner );
		expect( environment.get('login') ).toBe( false );
		expect( environment.get('closed') ).toEqual( ['access', 'apps'] );
		expect( environment.get('version') ).toBeTypeOf( 'string' );//the build's own keys stay
		expect( info ).not.toHaveBeenCalled();
		expect( warn ).not.toHaveBeenCalled();
	} );

	it( 'takes a key of its own type alone, and no key of the build\'s', async ()=>{
		const { environment, warn } = await load( json({applicationServer: {port: '443'}, home: 'https://elsewhere.example/', banner: 7, login: 'no', closed: 'access', version: 'forged', production: !built.production}) );
		expect( environment.get('applicationServer') ).toEqual( build );
		expect( environment.get('home') ).toBeUndefined();
		expect( environment.get('banner') ).toBeUndefined();
		expect( environment.get('login') ).toBeUndefined();
		expect( environment.get('closed') ).toBeUndefined();
		expect( environment.get('version') ).not.toBe( 'forged' );
		expect( environment.get('production') ).toBe( built.production );
		expect( warn ).toHaveBeenCalledTimes( 5 );
		for( const closed of [['access', 7], ['access/users'], ['']] ){//a list of paths, each a section's own
			vi.restoreAllMocks();
			expect( (await load( json({closed}) )).environment.get('closed') ).toBeUndefined();
		}

		vi.restoreAllMocks();
		const home = await load( json({home: '/', applicationServer: {port: 8443}}) );//the empty path is no home for itself;  a port of the front's own is one
		expect( home.environment.get('home') ).toBeUndefined();
		expect( home.environment.get('applicationServer') ).toEqual( {...build, port: 8443} );
	} );

	it( 'takes no home that its login false closes', async ()=>{
		for( const login of ['/login', '/login?next=/apps', '/login/'] ){
			const { environment, warn } = await load( json({home: login, login: false}) );
			expect( environment.get('home') ).toBeUndefined();
			expect( environment.get('login') ).toBe( false );
			expect( warn ).toHaveBeenCalledTimes( 1 );
			vi.restoreAllMocks();
		}
		expect( (await load( json({home: '/login', login: true}) )).environment.get('home') ).toBe( '/login' );//a site that opens on its sign-in
		expect( (await load( json({home: '/logins', login: false}) )).environment.get('home') ).toBe( '/logins' );
	} );

	it( 'is absent when it is not answered in time, the site waiting on it to start', async ()=>{
		const timeout = EnvironmentService.timeoutMs;
		EnvironmentService.timeoutMs = 10;
		try{
			const fetch = vi.fn( ( _:URL, init:RequestInit )=>new Promise<Response>( ( _resolve, reject )=>init.signal!.addEventListener( 'abort', ()=>reject(init.signal!.reason) ) ) );
			vi.stubGlobal( 'fetch', fetch );
			const info = silence( 'info' );
			const environment = new EnvironmentService();
			await environment.load();
			expect( environment.get('applicationServer') ).toEqual( build );
			expect( info ).toHaveBeenCalledTimes( 1 );
		}
		finally{
			EnvironmentService.timeoutMs = timeout;
		}
	} );
} );

describe( 'the site under a config.json', ()=>{
	afterEach( ()=>{
		vi.unstubAllGlobals();
		vi.restoreAllMocks();
	});
	//where a url lands, so every page is an empty one and no resolver reads its rows
	const unresolved = ( rs:Routes ):Routes=>rs.map( r=>({ ...r, resolve: undefined, children: r.children && unresolved( r.children ) }) );
	const open = async ( answer:Response, url:string )=>{
		const { environment } = await load( answer );
		const { router } = await openRoute( {routes: unresolved( blankRoutes(offered( routes, environment.get('closed') )) ), url, providers: [
			{ provide: IENVIRONMENT, useValue: environment },
			{ provide: AuthGuard, useValue: {canActivate: ()=>true, canActivateChild: ()=>true} }
		]} );
		await flush();//the redirect is a second navigation
		return router.url;
	};

	it( 'keeps the cards at the empty path and the login page with none', async ()=>{
		expect( await open( absent(), '/' ) ).toBe( '/' );
		TestBed.resetTestingModule();//a second build in the one test
		expect( await open( absent(), '/login' ) ).toBe( '/login' );
	} );

	it( 'redirects the empty path to home', async ()=>{
		expect( await open( json(demo), '/' ) ).toBe( floor );
	} );

	it( 'has no login page where login is false', async ()=>{
		expect( await open( json(demo), '/login' ) ).toBe( floor );
		TestBed.resetTestingModule();//a second build in the one test
		expect( await open( json({login: false}), '/login' ) ).toBe( '/' );
		TestBed.resetTestingModule();//and a third
		expect( await open( json({login: true, home: floor}), '/login' ) ).toBe( '/login' );
	} );

	//the site under that config.json, opened at url:  whether a url is Page not found, the home's tiles and what the search lists
	const site = async ( answer:Response, url:string )=>{
		const { environment, warn } = await load( answer );
		const open = offered( routes, environment.get('closed') );
		const { harness, leaf } = await openRoute( {routes: unresolved( blankRoutes(open) ), url, providers: [
			{ provide: IENVIRONMENT, useValue: environment },
			{ provide: AuthGuard, useValue: {canActivate: ()=>true, canActivateChild: ()=>true} }
		]} );
		const notFound = async ( to?:string )=>{
			if( to ){
				await harness.navigateByUrl( to );
				await flush();
			}
			return leaf().data['notFound'] as boolean|undefined;
		};
		const tiles = ( await TestBed.inject( HomeRouteService ).children() ).map( tile=>tile.path );
		return { open, warn, notFound, tiles, found: TestBed.inject( RouteSearchProvider ).items().map( item=>String(item.route) ) };
	};

	it( 'has no page, tile or search result for a closed section, and keeps the pages under it at a path of their own', async ()=>{
		const { open, warn, notFound, tiles, found } = await site( json(demo), '/access/users' );
		expect( open.map( r=>r.path ) ).toEqual( routes.map( r=>r.path ).filter( path=>path!='access' && path!='apps' ) );
		expect( warn ).not.toHaveBeenCalled();
		expect( await notFound() ).toBe( true );
		for( const [url, missing] of [['/access', true], ['/apps', true], ['/apps/gateways/hub', undefined], [floor, undefined], ['/gateways', undefined]] as const )
			expect( await notFound(url), url ).toBe( missing );
		expect( tiles ).toEqual( ['gateways', 'help'] );
		expect( found.filter( route=>route=='/apps' || route.startsWith('/access') ) ).toEqual( [] );
		expect( found ).toContain( '/gateways' );
	} );

	it( 'offers every section with none closed, and none that a name which is no section would close', async ()=>{
		const { open, warn, notFound, tiles } = await site( json({closed: ['login', 'nowhere', '**']}), '/access/users' );//the sign-in is `login`'s to close
		expect( open ).toBe( routes );
		expect( warn ).toHaveBeenCalledTimes( 3 );
		expect( await notFound() ).toBeUndefined();
		expect( await notFound('/apps') ).toBeUndefined();
		expect( tiles ).toEqual( ['gateways', 'access', 'apps', 'help'] );
		expect( offered(routes, undefined) ).toBe( routes );
		expect( offered(routes, []) ).toBe( routes );
	} );

	//app.config.ts's initializer, on a router built from every route as provideRouter builds it
	const configured = async ( answer:Response )=>{
		vi.stubGlobal( 'fetch', vi.fn( async ()=>answer ) );
		silence( 'info' );
		const environment = new EnvironmentService();
		const { router, harness, leaf } = await openRoute( {routes: unresolved( blankRoutes(routes) ), url: '/gateways', providers: [
			{ provide: IENVIRONMENT, useValue: environment },
			{ provide: AuthGuard, useValue: {canActivate: ()=>true, canActivateChild: ()=>true} }
		]} );
		const resetConfig = vi.spyOn( router, 'resetConfig' );
		await applyConfig( environment, router );
		const notFound = async ( to:string )=>{
			await harness.navigateByUrl( to );
			await flush();
			return leaf().data['notFound'] as boolean|undefined;
		};
		return { environment, resetConfig, notFound, tiles: async ()=>( await TestBed.inject( HomeRouteService ).children() ).map( tile=>tile.path ) };
	};

	it( 'closes the sections once config.json has loaded', async ()=>{
		const { environment, resetConfig, notFound, tiles } = await configured( json(demo) );
		expect( environment.get('closed') ).toEqual( demo.closed );
		expect( resetConfig ).toHaveBeenCalledTimes( 1 );
		expect( await tiles() ).toEqual( ['gateways', 'help'] );
		expect( await notFound('/apps') ).toBe( true );
		expect( await notFound('/access') ).toBe( true );
	} );

	it( 'leaves the router as built with no config.json', async ()=>{
		const { resetConfig, notFound } = await configured( absent() );
		expect( resetConfig ).not.toHaveBeenCalled();
		expect( await notFound('/apps') ).toBeUndefined();
	} );

	const banner = async ( answer:Response )=>{
		const { environment } = await load( answer );
		TestBed.configureTestingModule( {providers: [{ provide: IENVIRONMENT, useValue: environment }]} );
		TestBed.overrideComponent( App, {remove: {imports: [NavBar]}, add: {schemas: [CUSTOM_ELEMENTS_SCHEMA]}} );//the banner's own row, not the navbar under it
		const fixture = TestBed.createComponent( App );
		fixture.detectChanges();
		const element = fixture.nativeElement as HTMLElement;
		return { banner: element.querySelector( '.site-banner' ), first: element.firstElementChild };
	};

	it( 'heads every page with the banner, as text', async ()=>{
		const text = '<b>Live</b> demo';
		const { banner: line, first } = await banner( json({banner: text}) );
		expect( line?.textContent ).toBe( text );
		expect( line?.querySelector('b') ).toBeNull();
		expect( first ).toBe( line );
	} );

	it( 'has no banner with none', async ()=>{
		const { banner: line, first } = await banner( absent() );
		expect( line ).toBeNull();
		expect( first?.tagName.toLowerCase() ).toBe( 'app-navbar' );
	} );

	//the hub answers /opcGateways with its own advertisement, localhost on the listener's port, whatever front the page came through
	const gateways = async ( answer:Response, page:object )=>{
		const { environment } = await load( answer );
		vi.stubGlobal( 'location', page );
		TestBed.configureTestingModule( {providers: [
			provideHttpClient(),
			{ provide: IENVIRONMENT, useValue: environment },
			{ provide: AUTH_STORE, useValue: anonymousAuthStore() }
		]} );
		const service = TestBed.inject( AppService );
		(service as any).get = async ()=>({ servers: [{host: 'localhost', port: 1967, instanceName: 'hub'}, {host: 'gateway2', port: 1968, instanceName: 'split'}] });
		return { service, instances: await service.gatewayInstances() };
	};
	const page = { protocol: 'https:', hostname: 'demo', host: 'demo', port: '' };

	it( 'calls the hub and its gateway on the page\'s own port:  localhost:1967 under https://demo is demo:443', async ()=>{
		const { service, instances } = await gateways( json(demo), page );
		expect( service.urlWithTarget('graphql') ).toBe( 'https://demo:443/graphql' );
		expect( (service as any).socketUrl ).toBe( 'wss://demo:443' );
		expect( instances[0] ).toEqual( {host: 'demo', port: 443, instanceName: 'hub'} );
		expect( instances[1] ).toEqual( {host: 'gateway2', port: 1968, instanceName: 'split'} );//another host's instance keeps its own
	} );

	it( 'takes a port the page names', async ()=>{
		const { service, instances } = await gateways( json(demo), {protocol: 'http:', hostname: 'hub', host: 'hub:8071', port: '8071'} );
		expect( service.urlWithTarget('graphql') ).toBe( 'http://hub:8071/graphql' );
		expect( instances[0].port ).toBe( 8071 );
	} );

	it( 'calls 1967 with no config.json', async ()=>{
		const { service, instances } = await gateways( absent(), page );
		expect( service.urlWithTarget('graphql') ).toBe( 'https://demo:1967/graphql' );
		expect( instances[0] ).toEqual( {host: 'demo', port: 1967, instanceName: 'hub'} );
	} );
} );