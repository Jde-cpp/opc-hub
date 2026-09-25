//the vitest environment provides window/document but no localStorage - back the bare-global references with an
//in-memory one BEFORE importing jde-spa/jde-framework (see the site specs).
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
import { provideRouter, Router, Routes } from '@angular/router';
import { HttpErrorResponse, provideHttpClient } from '@angular/common/http';
import { AppService, AUTH_STORE, AuthStore, ETransport } from 'jde-framework';
import { GatewayService } from './gateway-service';
import { OPC_STORE } from './opc-store';

class Dummy{}
//the routes GatewayService has to read a gateway out of - app.routes.ts carries both forms, and 'apps/gateways/:instance'
//is the one that maps the 'IGraphQL' token to this service.
const routes:Routes = [
	{ path: '', component: Dummy },
	{ path: 'gateways/:gateway', component: Dummy },
	{ path: 'gateways/:gateway/:connection', component: Dummy, children: [ {path: '**', component: Dummy} ] },
	{ path: 'apps/gateways/:instance', component: Dummy, children: [ {path: '', component: Dummy}, {path: ':connection', component: Dummy} ] },
	{ path: 'access/users/:slug', component: Dummy }
];
const instances = [ {host:'localhost', port:1968, instanceName:'A'}, {host:'localhost', port:1969, instanceName:'B'} ];

describe('GatewayService.defaultGateway', () => {
	let service:GatewayService;
	let router:Router;
	beforeEach( async () => {
		TestBed.configureTestingModule({ providers: [
			provideRouter( routes ),
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: {user: ()=>undefined, logout: ()=>{}} as unknown as AuthStore },
			{ provide: OPC_STORE, useValue: {} },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, gatewayInstances: ()=>Promise.resolve(instances)} }
		]});
		router = TestBed.inject( Router );
		service = TestBed.inject( GatewayService );
		await service.gateways();//the instance lookup is a promise; nothing resolves before it lands
	});

	//review3 #4: this used to be cached off the ROOT ActivatedRoute's paramMap, which never carries a child ':gateway' -
	//so the first url-suffix guess stuck and every later mutation went to the wrong gateway.
	it('follows navigation on the /gateways/:gateway routes', async () => {
		await router.navigateByUrl( '/gateways/B' );
		expect( service.defaultGateway.slug ).toBe( 'B' );
		await router.navigateByUrl( '/gateways/A' );
		expect( service.defaultGateway.slug ).toBe( 'A' );
	});

	it('reads the gateway out of a node url, past the browse path', async () => {
		await router.navigateByUrl( '/gateways/B/local/2~DeviceSet/2~Machine' );
		expect( service.defaultGateway.slug ).toBe( 'B' );
	});

	it("follows the 'apps/gateways/:instance' routes, which are the ones bound to the 'IGraphQL' token", async () => {
		await router.navigateByUrl( '/apps/gateways/B' );
		expect( service.defaultGateway.slug ).toBe( 'B' );
		await router.navigateByUrl( '/apps/gateways/B/local' );
		expect( service.defaultGateway.slug ).toBe( 'B' );
	});

	it('falls back to the first gateway where the url names none', async () => {
		await router.navigateByUrl( '/gateways/B' );
		await router.navigateByUrl( '/access/users/Google-someone%40gmail.com' );
		expect( service.defaultGateway.slug ).toBe( 'A' );
	});

	it('falls back to the first gateway for an unknown slug rather than throwing', async () => {
		await router.navigateByUrl( '/gateways/nosuch' );
		expect( service.defaultGateway.slug ).toBe( 'A' );
	});
});

//review3 L4: `gateway()` ended in `find(...)!`, so a url segment naming a gateway that is not registered - a stale
//bookmark, a renamed instance - resolved with `undefined`.  The miss only surfaced as "cannot read properties of
//undefined" inside the resolver's first query, with nothing naming the gateway that was asked for.
describe('GatewayService.gateway', () => {
	const configure = ( gatewayInstances:()=>Promise<any[]> )=>{
		TestBed.configureTestingModule({ providers: [
			provideRouter( routes ),
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: {user: ()=>undefined, logout: ()=>{}} as unknown as AuthStore },
			{ provide: OPC_STORE, useValue: {} },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, gatewayInstances} }
		]});
		return TestBed.inject( GatewayService );
	};

	it('resolves a registered gateway', async () => {
		const service = configure( ()=>Promise.resolve(instances) );
		await service.gateways();
		expect( (await service.gateway('B')).slug ).toBe( 'B' );
	});

	it('rejects for an unregistered gateway, naming the ones that are', async () => {
		const service = configure( ()=>Promise.resolve(instances) );
		await service.gateways();
		await expect( service.gateway('nosuch') ).rejects.toThrow( /No gateway 'nosuch' is registered.*'A', 'B'/ );
	});

	//the queued path: the same miss, taken before gatewayInstances() has landed, used to resolve its awaiter with undefined.
	it('rejects a request queued before the instance lookup landed', async () => {
		let land:( instances:any[] )=>void;
		const service = configure( ()=>new Promise<any[]>( resolve=>{ land = resolve; } ) );
		const queued = service.gateway( 'nosuch' );
		land!( instances );
		await expect( queued ).rejects.toThrow( /No gateway 'nosuch' is registered/ );
	});

	//reviews/m3-closing.md #7:  after a failed instance lookup nothing ever settled a later gateway()/gateways() - /gateways
	//awaited forever, and a Retry could not help.  A later call looks the instances up again.
	it('looks the instances up again once a lookup has failed', async () => {
		let calls = 0;
		const service = configure( ()=>++calls==1 ? Promise.reject(new HttpErrorResponse({status: 0})) : Promise.resolve(instances) );
		await expect( service.gateways() ).rejects.toBeTruthy();
		const later = service.gateway( 'B' );
		const gaveUp = new Promise( resolve=>setTimeout(()=>resolve("still waiting"), 500) );
		expect( await Promise.race([later.then(g=>g.slug), gaveUp]) ).toBe( 'B' );
		expect( calls ).toBe( 2 );
	});

	it('still resolves a queued request that does name a registered gateway', async () => {
		let land:( instances:any[] )=>void;
		const service = configure( ()=>new Promise<any[]>( resolve=>{ land = resolve; } ) );
		const queued = service.gateway( 'B' );
		land!( instances );
		expect( (await queued).slug ).toBe( 'B' );
	});
});
//reviews/install-issues.md #54:  the OPC sign-out passed options of its own, and postRaw attaches the stored session only when
//a caller passes none - so /logout went without an Authorization header, the hub ended a session made for that request, and
//the signed-in one lived on.  It now names the session, as AppService.logout does, and still drops the local one.
describe( 'Gateway.logout', ()=>{
	it( 'names the signed-in session and clears the local one', async ()=>{
		TestBed.configureTestingModule({ providers: [
			provideRouter( routes ),
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: {user: ()=>undefined, logout: ()=>{}} as unknown as AuthStore },
			{ provide: OPC_STORE, useValue: {} },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, gatewayInstances: ()=>Promise.resolve(instances)} }
		]});
		const gateway = await TestBed.inject( GatewayService ).gateway( 'B' );
		let cleared = 0;
		(gateway as any).authStore = { user: ()=>({authorization: "d10c90aa"}), logout: ()=>{ ++cleared; } };
		const sent:{target:string, options:any}[] = [];
		(gateway as any).postRaw = async ( target:string, _body:any, _secure:boolean, options:any )=>{ sent.push( {target, options} ); };
		await gateway.logout( ()=>{} );
		expect( sent.length ).toBe( 1 );
		expect( sent[0].target ).toBe( 'logout' );
		expect( sent[0].options?.headers?.Authorization ).toBe( "d10c90aa" );
		expect( cleared ).toBe( 1 );
	} );
} );
