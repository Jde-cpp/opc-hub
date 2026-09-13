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
import { provideHttpClient } from '@angular/common/http';
import { HttpClient } from '@angular/common/http';
import * as FromClient from 'jde-proto/App.FromClient';
import * as App from 'jde-proto/App';
import { AUTH_STORE, AuthStore } from '../auth-store';
import { IENVIRONMENT } from 'jde-spa';
import { ETransport, RequestId } from '../proto-service';
import { AppService } from './app-service';

//review3 L10: the send payloads are plain objects spread into a FromClient.Message, so a key the proto does not declare is
//silently dropped by encode() and a requestId-only message goes on the wire - the server has nothing to answer and the
//promise never settles.  `strings` was such a key; the field is `request_strings`.
class TestAppService extends AppService{
	sent:any[] = [];
	//capture what send() hands the wire.  Going through the real path would open a socket: with none connected sendWithId
	//backlogs and calls connect(), so the encode below is done here instead - it is the step the bug was lost in.
	protected override sendWithId( m:any, requestId:RequestId, _log:string ):void{ this.sent.push( {requestId, ...m} ); }
	wire( index=0 ):FromClient.Message{
		return FromClient.Message.decode( FromClient.Message.encode(FromClient.Message.fromPartial(this.sent[index])).finish() );
	}
}

const create = ( env:Record<string,any> = {} )=>{
	TestBed.resetTestingModule();
	const values = { applicationServer: {host:'localhost', port:1967}, ...env };
	TestBed.configureTestingModule({ providers: [
		provideHttpClient(),
		{ provide: IENVIRONMENT, useValue: { get: ( key:string )=>(<any>values)[key] } },
		{ provide: AUTH_STORE, useValue: { user: ()=>undefined, logout: ()=>{} } as unknown as AuthStore }
	]});
	return TestBed.runInInjectionContext( ()=>new TestAppService() );
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

describe( 'AppService socket message field names', ()=>{

	const md5 = new Uint8Array( 16 ).fill( 7 );

	it( 'puts the StringMD5s payload on the wire', ()=>{
		const service = create();
		service.requestStrings( App.StringMD5s.fromPartial({messages:[md5], files:[], functions:[], userPKs:[42]}) );
		const message = service.wire();
		expect( message.requestStrings?.messages ).toEqual( [md5] );
		expect( message.requestStrings?.userPKs ).toEqual( [42] );
	} );

	//review3 C11: `graphQl`/`graphQL` were not proto fields either, and `requestType:UnsubscribeLogs` is one the server answers
	//with "not implemented".  The three now travel as the proto's subscription/unsubscription/query.
	it( 'puts a log subscription on the wire as a Query', ()=>{
		const service = create();
		service.logs( 3, 2, new Date('2026-09-05T00:00:00Z'), 10 );
		const message = service.wire();
		expect( message.subscription?.text ).toContain( 'subscribe logs(applicationId:3' );
		expect( message.subscription?.returnRaw ).toBe( false );
		expect( message.requestType ).toBeUndefined();
	} );

	it( 'puts the unsubscription, carrying the subscription id, on the wire', ()=>{
		const service = create();
		service.logsUnsubscribe( 9 );
		const message = service.wire();
		expect( message.unsubscription?.requestIds ).toEqual( [9] );
		expect( message.requestType ).toBeUndefined();
	} );

	it( 'puts the log-level mutation on the wire as a Query', ()=>{
		const service = create();
		service.updateLogLevel( 5, 2, 3 );
		const message = service.wire();
		expect( message.query?.text ).toContain( 'LogApplicationInstances( id:5' );
	} );
} );

//review3 C12/L8: a logout whose round trip fails still has to drop the local session - the user was left logged in with
//no feedback and no way out, and GatewayService.logout already swallowed the same failure.
describe( 'AppService.logout', ()=>{
	it( 'clears the local session even when the server round trip rejects', async ()=>{
		const service = create();
		let cleared = 0;
		(service as any).authStore = { user: ()=>undefined, logout: ()=>{ ++cleared; } };
		(service as any).postRaw = ()=>Promise.reject( new Error('502') );
		const logged:string[] = [];
		await expect( service.logout( m=>logged.push(m) ) ).resolves.toBeUndefined();
		expect( cleared ).toBe( 1 );
		expect( logged.some( m=>m.includes('logout failed') && m.includes('502') ) ).toBe( true );
	} );
} );

//review3 L11: neither environment file carried an `httpTransport` key, so this was constructed with `undefined`.  Every
//test in ProtoService is `==Secure`/`==Hybrid`, which undefined fails, so it acted as Unsecure - but `==ETransport.Unsecure`
//was false too, that enumerator being 0.
describe( 'AppService transport', ()=>{
	it( 'is a real enumerator when the environment names none', ()=>{
		expect( create().transport ).toBe( ETransport.Unsecure );
	} );

	it( 'honours the environment key', ()=>{
		expect( create({httpTransport: ETransport.Secure}).transport ).toBe( ETransport.Secure );
	} );
} );
