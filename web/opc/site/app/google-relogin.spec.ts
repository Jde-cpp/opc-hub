//Silent Google re-login on session expiry (reviews/todo.md §7): GoogleAuthService owns the GIS prompt machinery and
//ProtoService.authGet renews a lapsed Google session in place, retrying the original request with the fresh session.
import { TestBed } from '@angular/core/testing';
import { HttpClient, HttpHeaders, HttpResponse } from '@angular/common/http';
import { of, throwError } from 'rxjs';

//the vitest environment provides window/document/btoa but no localStorage - back the bare-global references in
//AuthStore/GoogleAuthService with an in-memory one BEFORE importing them (AuthStore reads it at construction).
if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	const storage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>{ backing.clear(); },
	};
	(globalThis as any).localStorage = storage;
	(window as any).localStorage = storage;
}

import { AuthStore, ETransport, GoogleAuthService, ProtoService, googleClientIdKey } from 'jde-framework';
import { EProvider, User, UserJson } from 'jde-spa';

const b64url = ( o:object )=>btoa( JSON.stringify(o) ).replace( /\+/g, '-' ).replace( /\//g, '_' ).replace( /=+$/, '' );
const makeJwt = ( email:string )=>`${b64url({alg:'RS256'})}.${b64url({email, name:'Test User', picture:'p', exp:4102444800, iss:'https://accounts.google.com'})}.sig`;
const freshJwt = makeJwt( 'fresh@example.com' );

class TestService extends ProtoService<object,object>{
	constructor( http:HttpClient, authStore:AuthStore, googleAuth?:GoogleAuthService ){
		super( {create:()=>({}), encode:()=>({finish:()=>new Uint8Array()})}, http, ETransport.Unsecure, authStore, false, googleAuth );//ts-proto codec stub: the base takes a codec, not a constructor
		super.instances = [{host:'apphost', port:1} as any];
	}
	protected override processMessage():void{}
	protected override handleConnectionError():void{}
	authGetPublic<Y>( target:string, auth?:string ):Promise<Y>{ return (this as any).authGet( target, auth, ()=>{} ); }
	postPublic<Y>( target:string, body:any ):Promise<Y>{ return this.post<Y>( target, body ); }
	setSocket( id:number ){ this.setSocketId( id ); }
	sentAuthorizations:number[] = [];
	protected override async sendAuthorization( socketId:number ):Promise<void>{ this.sentAuthorizations.push( socketId ); }
}

const the401 = ()=>throwError( ()=>({status:401, error:'session expired', url:'http://apphost:1/x'}) );

describe( 'GoogleAuthService.renewCredential', ()=>{
	let gis:{ initialize:any, prompt:any, renderButton:any };
	let credentialCb:( (r:any)=>void )|undefined;
	let momentCb:( (m:any)=>void )|undefined;
	let service:GoogleAuthService;

	beforeEach( ()=>{
		localStorage.clear();
		localStorage.setItem( googleClientIdKey, 'client-id' );
		credentialCb = momentCb = undefined;
		gis = {
			initialize: vi.fn( (cfg:any)=>credentialCb=cfg.callback ),
			prompt: vi.fn( (cb?:any)=>momentCb=cb ),
			renderButton: vi.fn(),
		};
		(globalThis as any).google = { accounts:{ id:gis } };
		service = new GoogleAuthService();
	});
	afterEach( ()=>{ delete (globalThis as any).google; } );

	it( 'resolves the credential the prompt produces', async ()=>{
		const p = service.renewCredential();
		expect( gis.prompt ).toHaveBeenCalledTimes( 1 );
		credentialCb!( {credential:'tok'} );
		await expect( p ).resolves.toBe( 'tok' );
	});

	it( 'coalesces concurrent renewals into one prompt', async ()=>{
		const p1 = service.renewCredential();
		const p2 = service.renewCredential();
		expect( p2 ).toBe( p1 );
		expect( gis.prompt ).toHaveBeenCalledTimes( 1 );
		credentialCb!( {credential:'tok'} );
		await expect( p2 ).resolves.toBe( 'tok' );
		//settled - the next renewal starts a fresh prompt
		const p3 = service.renewCredential();
		expect( p3 ).not.toBe( p1 );
		expect( gis.prompt ).toHaveBeenCalledTimes( 2 );
		credentialCb!( {credential:'tok2'} );
		await expect( p3 ).resolves.toBe( 'tok2' );
	});

	it( 'a skipped moment is a normal null outcome, not an error', async ()=>{
		const p = service.renewCredential();
		momentCb!( {isSkippedMoment:()=>true} );
		await expect( p ).resolves.toBeNull();
	});

	it( 'a dismissal resolves null - except credential_returned, which precedes the credential callback', async ()=>{
		const p = service.renewCredential();
		momentCb!( {isSkippedMoment:()=>false, isDismissedMoment:()=>true, getDismissedReason:()=>'credential_returned'} );
		credentialCb!( {credential:'tok'} );//the dismissal must not have settled null first
		await expect( p ).resolves.toBe( 'tok' );

		const p2 = service.renewCredential();
		momentCb!( {isSkippedMoment:()=>false, isDismissedMoment:()=>true, getDismissedReason:()=>'cancel_called'} );
		await expect( p2 ).resolves.toBeNull();
	});

	it( 'resolves null without prompting when the login page has never cached a client id', async ()=>{
		localStorage.removeItem( googleClientIdKey );
		await expect( service.renewCredential() ).resolves.toBeNull();
		expect( gis.prompt ).not.toHaveBeenCalled();
	});

	it( 'the timeout backstop resolves null when no moment callback ever fires', async ()=>{
		vi.useFakeTimers();
		try{
			const p = service.renewCredential();
			vi.advanceTimersByTime( GoogleAuthService.renewalTimeoutMs );
			await expect( p ).resolves.toBeNull();
		}
		finally{ vi.useRealTimers(); }
	});

	it( 'a credential with no pending renewal dispatches to the interactive handler', ()=>{
		const handler = vi.fn();
		service.credentialHandler = handler;
		service.initialize( 'client-id' );
		credentialCb!( {credential:'tok'} );
		expect( handler ).toHaveBeenCalledWith( 'tok' );
	});
});

describe( 'ProtoService silent Google re-login on 401', ()=>{
	let authStore:AuthStore;
	let logout:any;
	let http:{ get:any, post:any };
	const loginResponse = ()=>of( new HttpResponse({body:'{}', headers:new HttpHeaders({Authorization:'fresh-session'})}) );

	const googleUser = ()=>{
		authStore.append( new User(makeJwt('stale@example.com')) );
		authStore.append( {sessionId:'stale-session'} );
	};

	beforeEach( ()=>{
		localStorage.clear();
		TestBed.resetTestingModule();
		authStore = TestBed.inject( AuthStore );//not `new AuthStore()`: it inject()s RouteStore to clear the browsed route names on logout
		logout = vi.spyOn( authStore, 'logout' );
		http = { get:vi.fn(), post:vi.fn() };
	});
	const makeService = ( renewed:string|null )=>{
		const googleAuth = { renewCredential: vi.fn().mockResolvedValue(renewed) };
		return { service: new TestService( http as unknown as HttpClient, authStore, googleAuth as unknown as GoogleAuthService ), googleAuth };
	};

	it( 'renews in place and retries with the fresh session', async ()=>{
		googleUser();
		const { service, googleAuth } = makeService( freshJwt );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of({ok:true}) );
		http.post.mockReturnValue( loginResponse() );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {ok:true} );
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( http.post ).toHaveBeenCalledTimes( 1 );
		expect( http.post.mock.calls[0][0] ).toContain( '/login' );
		expect( http.post.mock.calls[0][2].headers.Authorization ).toBe( `Bearer ${freshJwt}` );
		expect( http.get.mock.calls[1][1].headers.Authorization ).toBe( 'fresh-session' );//retried authenticated, not anonymously
		expect( logout ).not.toHaveBeenCalled();
		expect( authStore.user()?.authorization ).toBe( 'fresh-session' );
		expect( authStore.user()?.jwt ).toBe( freshJwt );//identity refreshed alongside the session
	});

	it( 'a prompt that produces no credential falls back to the anonymous retry', async ()=>{
		googleUser();
		const { service, googleAuth } = makeService( null );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{anon:true}})) );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {anon:true} );
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( http.post ).not.toHaveBeenCalled();
		expect( logout ).toHaveBeenCalled();
	});

	it( 'password/OpcServer users never reach the silent path', async ()=>{
		authStore.append( {id:'bob', provider:EProvider.OpcServer, sessionId:'stale-session'} as UserJson );
		const { service, googleAuth } = makeService( freshJwt );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{anon:true}})) );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {anon:true} );
		expect( googleAuth.renewCredential ).not.toHaveBeenCalled();
		expect( logout ).toHaveBeenCalled();
	});

	it( 'concurrent 401s coalesce into one renewal round-trip', async ()=>{
		googleUser();
		let resolveCredential!:( c:string|null )=>void;
		const googleAuth = { renewCredential: vi.fn( ()=>new Promise<string|null>(r=>resolveCredential=r) ) };
		const service = new TestService( http as unknown as HttpClient, authStore, googleAuth as unknown as GoogleAuthService );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( the401() ).mockReturnValue( of({ok:1}) );
		http.post.mockReturnValue( loginResponse() );

		const p1 = service.authGetPublic( 'a', 'stale-session' );
		const p2 = service.authGetPublic( 'b', 'stale-session' );
		await new Promise( r=>setTimeout(r) );//let both 401s land in renewGoogleSession before the credential resolves
		resolveCredential( freshJwt );
		await expect( p1 ).resolves.toEqual( {ok:1} );
		await expect( p2 ).resolves.toEqual( {ok:1} );
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( http.post ).toHaveBeenCalledTimes( 1 );//one /login for both requests
	});

	it( 'a second 401 against the fresh session falls through without a second prompt', async ()=>{
		googleUser();
		const { service, googleAuth } = makeService( freshJwt );
		http.get.mockReturnValue( the401() );//every request 401s, renewed session included
		http.post.mockReturnValue( loginResponse() );

		await expect( service.authGetPublic('data', 'stale-session') ).rejects.toMatchObject( {status:401} );//the final anonymous 401 must throw
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( logout ).toHaveBeenCalled();
	});

	it( 'a connected socket is re-authenticated with the fresh session', async ()=>{
		googleUser();
		const { service } = makeService( freshJwt );
		service.setSocket( 5 );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of({ok:true}) );
		http.post.mockReturnValue( loginResponse() );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {ok:true} );
		expect( service.sentAuthorizations ).toEqual( [5] );
	});
});

//reviews/m3-closing.md #22:  a mutation went out with the stored session and a 401 back - the hub restarted, the tab idled past
///http/timeout, the laptop changed network - was only reported:  the stale session stayed, and every Save repeated the 401 until
//some unrelated read happened to renew it.  A POST now takes GET's policy:  renew a Google session and retry once;  an
//anonymous session retries anonymously;  a signed-in user who cannot be renewed is signed out and the mutation is refused,
//never re-run as anonymous.
describe( 'ProtoService POST on 401', ()=>{
	let authStore:AuthStore;
	let http:{ get:any, post:any };
	const loginResponse = ()=>of( new HttpResponse({body:'{}', headers:new HttpHeaders({Authorization:'fresh-session'})}) );
	beforeEach( ()=>{
		localStorage.clear();
		TestBed.resetTestingModule();
		authStore = TestBed.inject( AuthStore );
		http = { get:vi.fn(), post:vi.fn() };
	});
	const makeService = ( renewed:string|null )=>new TestService( http as unknown as HttpClient, authStore, {renewCredential: vi.fn().mockResolvedValue(renewed)} as unknown as GoogleAuthService );

	it( 'renews a Google session and retries the mutation once with it', async ()=>{
		authStore.append( new User(makeJwt('stale@example.com')) );
		authStore.append( {sessionId:'stale-session'} );
		http.post.mockReturnValueOnce( the401() ).mockReturnValueOnce( loginResponse() ).mockReturnValueOnce( of({saved:true}) );
		await expect( makeService(freshJwt).postPublic('graphql', {q:1}) ).resolves.toEqual( {saved:true} );
		expect( http.post ).toHaveBeenCalledTimes( 3 );
		expect( http.post.mock.calls[1][0] ).toContain( '/login' );
		expect( http.post.mock.calls[2][2].headers.Authorization ).toBe( 'fresh-session' );
	});

	it( 'retries an anonymous session anonymously', async ()=>{
		authStore.append( {sessionId:'stale-anonymous'} );
		http.post.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{saved:true}, headers:new HttpHeaders({Authorization:'new-anonymous'})})) );//the server mints a session for an anonymous request
		await expect( makeService(null).postPublic('graphql', {q:1}) ).resolves.toEqual( {saved:true} );
		expect( http.post.mock.calls[1][2].headers ).toBeUndefined();
		expect( authStore.user()?.authorization ).toBe( 'new-anonymous' );
	});

	it( 'signs out a user it cannot renew and refuses the mutation', async ()=>{
		authStore.append( {id:'bob', provider:EProvider.OpcServer, sessionId:'stale-session'} as UserJson );
		const logout = vi.spyOn( authStore, 'logout' );
		http.post.mockReturnValueOnce( the401() );
		await expect( makeService(null).postPublic('graphql', {q:1}) ).rejects.toMatchObject( {status: 401} );
		expect( logout ).toHaveBeenCalled();
		expect( http.post ).toHaveBeenCalledTimes( 1 );//never re-run as anonymous
	});
});
