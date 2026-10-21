import { TestBed } from '@angular/core/testing';
import { HttpErrorResponse, provideHttpClient } from '@angular/common/http';
import { AppService, AUTH_STORE, ETransport } from 'jde-framework';
import { GatewayService } from './gateway-service';
import { OPC_STORE } from './opc-store';
import { anonymousAuthStore } from 'jde-framework/testing';

const instances = [ {host:'localhost', port:1968, instanceName:'A'}, {host:'localhost', port:1969, instanceName:'B'} ];

//review3 L4: `gateway()` ended in `find(...)!`, so a url segment naming a gateway that is not registered - a stale
//bookmark, a renamed instance - resolved with `undefined`.  The miss only surfaced as "cannot read properties of
//undefined" inside the resolver's first query, with nothing naming the gateway that was asked for.
describe( 'GatewayService.gateway', ()=>{
	const configure = ( gatewayInstances:()=>Promise<any[]> )=>{
		TestBed.configureTestingModule({ providers: [
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: anonymousAuthStore() },
			{ provide: OPC_STORE, useValue: {} },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, gatewayInstances} }
		]});
		return TestBed.inject( GatewayService );
	};

	it( 'resolves a registered gateway', async ()=>{
		const service = configure( ()=>Promise.resolve(instances) );
		await service.gateways();
		expect( (await service.gateway('B')).slug ).toBe( 'B' );
	});

	it( 'rejects for an unregistered gateway, naming the ones that are', async ()=>{
		const service = configure( ()=>Promise.resolve(instances) );
		await service.gateways();
		await expect( service.gateway('nosuch') ).rejects.toThrow( /No gateway 'nosuch' is registered.*'A', 'B'/ );
	});

	//the queued path: the same miss, taken before gatewayInstances() has landed, used to resolve its awaiter with undefined.
	it( 'rejects a request queued before the instance lookup landed', async ()=>{
		let land:( instances:any[] )=>void;
		const service = configure( ()=>new Promise<any[]>( resolve=>{ land = resolve; } ) );
		const queued = service.gateway( 'nosuch' );
		land!( instances );
		await expect( queued ).rejects.toThrow( /No gateway 'nosuch' is registered/ );
	});

	//reviews/m3-closing.md #7:  after a failed instance lookup nothing ever settled a later gateway()/gateways() - /gateways
	//awaited forever, and a Retry could not help.  A later call looks the instances up again.
	it( 'looks the instances up again once a lookup has failed', async ()=>{
		const lookup = vi.fn().mockRejectedValueOnce( new HttpErrorResponse({status: 0}) ).mockResolvedValue( instances );
		const service = configure( lookup );
		await expect( service.gateways() ).rejects.toBeTruthy();
		const later = service.gateway( 'B' );
		const gaveUp = new Promise( resolve=>setTimeout(()=>resolve("still waiting"), 500) );//a deadline, not a wait:  the bug left `later` pending for good
		expect( await Promise.race([later.then(g=>g.slug), gaveUp]) ).toBe( 'B' );
		expect( lookup ).toHaveBeenCalledTimes( 2 );
	});

	it( 'still resolves a queued request that does name a registered gateway', async ()=>{
		let land:( instances:any[] )=>void;
		const service = configure( ()=>new Promise<any[]>( resolve=>{ land = resolve; } ) );
		const queued = service.gateway( 'B' );
		land!( instances );
		expect( (await queued).slug ).toBe( 'B' );
	});

	it( 'shares one lookup between the callers waiting on it', async ()=>{
		let land:( instances:any[] )=>void;
		const lookup = vi.fn( ()=>new Promise<any[]>( resolve=>{ land = resolve; } ) );
		const service = configure( lookup );
		const waiting = Promise.all( [service.gateways(), service.gateway('B'), service.gateway('A')] );
		land!( instances );
		const [gateways, b, a] = await waiting;
		expect( lookup ).toHaveBeenCalledTimes( 1 );
		expect( [gateways.length, b.slug, a.slug] ).toEqual( [2, 'B', 'A'] );
	});
});
//reviews/install-issues.md #54:  the OPC sign-out passed options of its own, and postRaw attaches the stored session only when
//a caller passes none - so /logout went without an Authorization header, the hub ended a session made for that request, and
//the signed-in one lived on.  It now names the session, as AppService.logout does, and still drops the local one.
describe( 'Gateway.logout', ()=>{
	it( 'names the signed-in session and clears the local one', async ()=>{
		TestBed.configureTestingModule({ providers: [
			provideHttpClient(),
			{ provide: AUTH_STORE, useValue: anonymousAuthStore() },
			{ provide: OPC_STORE, useValue: {} },
			{ provide: AppService, useValue: {transport: ETransport.Unsecure, gatewayInstances: ()=>Promise.resolve(instances)} }
		]});
		const gateway = await TestBed.inject( GatewayService ).gateway( 'B' );
		const logout = vi.fn();
		(gateway as any).authStore = { user: ()=>({authorization: "d10c90aa"}), logout };
		const postRaw = vi.fn( async ( _target:string, _body:any, _options:any )=>{} );
		(gateway as any).postRaw = postRaw;
		await gateway.logout();
		expect( postRaw ).toHaveBeenCalledTimes( 1 );
		expect( postRaw.mock.calls[0][0] ).toBe( 'logout' );
		expect( postRaw.mock.calls[0][2]?.headers?.Authorization ).toBe( "d10c90aa" );
		expect( logout ).toHaveBeenCalledTimes( 1 );
	} );
} );