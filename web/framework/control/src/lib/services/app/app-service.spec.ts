import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { AUTH_STORE } from '../auth-store';
import { IENVIRONMENT } from 'jde-spa';
import { ETransport } from '../proto-service';
import { AppService } from './app-service';
import { anonymousAuthStore } from 'jde-framework/testing';
import { silence } from 'jde-spa/testing';

const create = ( env:Record<string,any> = {} )=>{
	const values = { applicationServer: {host:'localhost', port:1967}, ...env };
	TestBed.configureTestingModule({ providers: [
		provideHttpClient(),
		{ provide: IENVIRONMENT, useValue: { get: ( key:string )=>(<any>values)[key] } },
		{ provide: AUTH_STORE, useValue: anonymousAuthStore() }
	]});
	return TestBed.runInInjectionContext( ()=>new AppService() );
};

//install-issues #2: the production environment leaves the host empty, and the service reaches the app server by the host the
//page was served from - so the installed site, served by IIS from any name, calls that name's 1967 rather than the browser's
//own localhost.  A loopback host resolves the same way (the development environment's); a registry name is kept.
describe( 'AppService host', ()=>{
	const pageHost = typeof location=="undefined" ? "localhost" : location.hostname;
	it( 'reaches the app server by the page host when the environment leaves it empty', ()=>{
		const service = create( {applicationServer:{host:"", port:1967}} );
		expect( service.instances[0].host ).toBe( pageHost );
		expect( service.instances[0].port ).toBe( 1967 );
	} );
	it( 'resolves the development environment\'s localhost the same way', ()=>{
		const service = create();
		expect( service.instances[0].host ).toBe( pageHost );
	} );
	it( 'keeps a host that names another machine', ()=>{
		const service = create( {applicationServer:{host:"hub", port:1967}} );
		expect( service.instances[0].host ).toBe( "hub" );
	} );
} );

//#484:  a null port is the page's own - a front answers for the hub there (config.json's applicationServer.port) - and the
//instances the hub names on a loopback host, its own process, are reached on it too.
describe( 'AppService port', ()=>{
	afterEach( ()=>vi.unstubAllGlobals() );
	const gateways = async ( port:number|null )=>{
		vi.stubGlobal( 'location', {protocol: 'https:', hostname: 'demo', host: 'demo', port: ''} );
		const service = create( {applicationServer:{host:"", port}} );
		(service as any).get = async ()=>({ servers: [{host: 'localhost', port: 1967}, {host: 'gateway2', port: 1968}] });
		return { service, gateways: await service.gatewayInstances(), opcServers: await service.opcServerInstances() };
	};
	it( 'takes the page\'s port for a null one, for the hub and each instance of its process', async ()=>{
		const { service, gateways: found, opcServers } = await gateways( null );
		expect( service.instances[0] ).toEqual( {host: 'demo', port: 443} );
		expect( found ).toEqual( [{host: 'demo', port: 443}, {host: 'gateway2', port: 1968}] );
		expect( opcServers ).toEqual( found );
	} );
	it( 'keeps a port the environment names, and the instances theirs', async ()=>{
		const { service, gateways: found } = await gateways( 1967 );
		expect( service.instances[0] ).toEqual( {host: 'demo', port: 1967} );
		expect( found ).toEqual( [{host: 'demo', port: 1967}, {host: 'gateway2', port: 1968}] );
	} );
} );

//review3 C12/L8: a logout whose round trip fails still has to drop the local session - the user was left logged in with
//no feedback and no way out, and GatewayService.logout already swallowed the same failure.
describe( 'AppService.logout', ()=>{
	it( 'clears the local session even when the server round trip rejects', async ()=>{
		const service = create();
		const logout = vi.fn();
		(service as any).authStore = { user: ()=>undefined, logout };
		(service as any).postRaw = ()=>Promise.reject( new Error('502') );
		const spy = silence( 'log' );
		await expect( service.logout() ).resolves.toBeUndefined();
		expect( logout ).toHaveBeenCalledTimes( 1 );
		expect( spy.mock.calls.some( ([m])=>String(m).includes('logout failed') && String(m).includes('502') ) ).toBe( true );
		spy.mockRestore();
	} );
} );

//The environment's `httpTransport` was Unsecure in both builds, so a page browsed as https://<hub>:1967 loaded and then
//called the hub over http and ws, which the browser blocks as mixed content.  The page's own protocol decides now.
describe( 'AppService transport', ()=>{
	afterEach( ()=>vi.unstubAllGlobals() );

	it( 'is plain for a page browsed over http', ()=>{
		const service = create();
		expect( service.transport ).toBe( ETransport.Unsecure );
		expect( service.urlWithTarget('graphql') ).toBe( 'http://localhost:1967/graphql' );
	} );

	it( 'is TLS for a page browsed over https, the REST calls and the socket alike', ()=>{
		vi.stubGlobal( 'location', {protocol: 'https:', hostname: 'localhost', host: 'localhost'} );
		const service = create();
		expect( service.transport ).toBe( ETransport.Secure );
		expect( service.urlWithTarget('graphql') ).toBe( 'https://localhost:1967/graphql' );
		expect( (service as any).socketUrl ).toBe( 'wss://localhost:1967' );
	} );
} );

//a hub registers under its own program name, so a gateway's page asks for both
describe( 'AppService.instancePKs', ()=>{
	it( 'answers each program from the one read', async ()=>{
		const service = create();
		const read = vi.fn( async ()=>[{instanceId: 3, instanceName: 'Hub', programName: 'Jde.OpcHub'}, {id: 4, instanceName: 'Other', programName: 'OpcGateway'}] );
		(service as any).queryArray = read;
		expect( await service.instancePKs( 'Hub', ['OpcGateway', 'OpcHub'] ) ).toEqual( [undefined, 3] );
		expect( read ).toHaveBeenCalledTimes( 1 );
		expect( await service.instancePK( 'Other', 'OpcGateway' ) ).toBe( 4 );
	} );
} );