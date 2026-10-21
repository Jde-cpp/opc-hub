//Silent Google re-login on session expiry (reviews/todo.md §7): GoogleAuthService owns the GIS prompt machinery and
//ProtoService.authGet renews a lapsed Google session in place, retrying the original request with the fresh session.
import { TestBed } from '@angular/core/testing';
import { HttpClient, HttpHeaders, HttpResponse } from '@angular/common/http';
import { of, throwError } from 'rxjs';
import { AuthStore } from './auth-store';
import { GoogleAuthService, googleClientIdKey } from './google-auth-service';
import { EProvider, User, UserJson } from 'jde-spa';
import { fakeJwt, flush } from 'jde-spa/testing';
import { TestProtoService } from 'jde-framework/testing';

const makeJwt = ( email:string )=>fakeJwt( {email, name:'Test User', picture:'p', exp:4102444800, iss:'https://accounts.google.com'} );
const freshJwt = makeJwt( 'fresh@example.com' );

class TestService extends TestProtoService{
	constructor( http:HttpClient, authStore:AuthStore, googleAuth?:GoogleAuthService ){
		super( authStore, {http, googleAuth} );
		super.instances = [{host:'apphost', port:1} as any];
	}
	authGetPublic<Y>( target:string, auth?:string ):Promise<Y>{ return (this as any).authGet( target, auth ); }
	postPublic<Y>( target:string, body:any ):Promise<Y>{ return this.postRaw<Y>( target, body ); }
	setSocket( id:number ){ this.setSocketId( id ); }
	sentAuthorization = vi.fn();
	protected override async sendAuthorization( socketId:number ):Promise<void>{ this.sentAuthorization( socketId ); }
}

const the401 = ()=>throwError( ()=>({status:401, error:'session expired', url:'http://apphost:1/x'}) );

describe( 'GoogleAuthService.renewCredential', ()=>{
	//credential and moment:  what GIS calls back with a credential, and with a prompt's outcome - the latest callbacks it was given
	const create = ()=>{
		const gis = { initialize: vi.fn(), prompt: vi.fn(), renderButton: vi.fn() };
		vi.stubGlobal( 'google', { accounts:{ id:gis } } );
		return {
			service: new GoogleAuthService(),
			gis,
			credential: ( r:any )=>gis.initialize.mock.lastCall![0].callback( r ),
			moment: ( m:any )=>gis.prompt.mock.lastCall![0]( m )
		};
	};
	beforeEach( ()=>{
		localStorage.clear();
		localStorage.setItem( googleClientIdKey, 'client-id' );
	});
	afterEach( ()=>vi.unstubAllGlobals() );

	it( 'resolves the credential the prompt produces', async ()=>{
		const { service, gis, credential } = create();
		const p = service.renewCredential();
		expect( gis.prompt ).toHaveBeenCalledTimes( 1 );
		credential( {credential:'tok'} );
		await expect( p ).resolves.toBe( 'tok' );
	});

	it( 'coalesces concurrent renewals into one prompt', async ()=>{
		const { service, gis, credential } = create();
		const p1 = service.renewCredential();
		const p2 = service.renewCredential();
		expect( p2 ).toBe( p1 );
		expect( gis.prompt ).toHaveBeenCalledTimes( 1 );
		credential( {credential:'tok'} );
		await expect( p2 ).resolves.toBe( 'tok' );
		//settled - the next renewal starts a fresh prompt
		const p3 = service.renewCredential();
		expect( p3 ).not.toBe( p1 );
		expect( gis.prompt ).toHaveBeenCalledTimes( 2 );
		credential( {credential:'tok2'} );
		await expect( p3 ).resolves.toBe( 'tok2' );
	});

	it( 'a skipped moment is a normal null outcome, not an error', async ()=>{
		const { service, moment } = create();
		const p = service.renewCredential();
		moment( {isSkippedMoment:()=>true} );
		await expect( p ).resolves.toBeNull();
	});

	it( 'a dismissal resolves null - except credential_returned, which precedes the credential callback', async ()=>{
		const { service, credential, moment } = create();
		const p = service.renewCredential();
		moment( {isSkippedMoment:()=>false, isDismissedMoment:()=>true, getDismissedReason:()=>'credential_returned'} );
		credential( {credential:'tok'} );//the dismissal must not have settled null first
		await expect( p ).resolves.toBe( 'tok' );

		const p2 = service.renewCredential();
		moment( {isSkippedMoment:()=>false, isDismissedMoment:()=>true, getDismissedReason:()=>'cancel_called'} );
		await expect( p2 ).resolves.toBeNull();
	});

	it( 'resolves null without prompting when the login page has never cached a client id', async ()=>{
		const { service, gis } = create();
		localStorage.removeItem( googleClientIdKey );
		await expect( service.renewCredential() ).resolves.toBeNull();
		expect( gis.prompt ).not.toHaveBeenCalled();
	});

	it( 'the timeout backstop resolves null when no moment callback ever fires', async ()=>{
		const { service } = create();
		vi.useFakeTimers();//the backstop is the service's own timer
		try{
			const p = service.renewCredential();
			vi.advanceTimersByTime( GoogleAuthService.renewalTimeoutMs );
			await expect( p ).resolves.toBeNull();
		}
		finally{ vi.useRealTimers(); }
	});

	it( 'a credential with no pending renewal dispatches to the interactive handler', ()=>{
		const { service, credential } = create();
		const handler = vi.fn();
		service.credentialHandler = handler;
		service.initialize( 'client-id' );
		credential( {credential:'tok'} );
		expect( handler ).toHaveBeenCalledWith( 'tok' );
	});
});

describe( 'ProtoService silent Google re-login on 401', ()=>{
	const loginResponse = ()=>of( new HttpResponse({body:'{}', headers:new HttpHeaders({Authorization:'fresh-session'})}) );
	const googleUser = ( authStore:AuthStore )=>{
		authStore.append( new User(makeJwt('stale@example.com')) );
		authStore.append( {sessionId:'stale-session'} );
	};
	const create = ( renewed:string|null )=>{
		const authStore = TestBed.inject( AuthStore );//not `new AuthStore()`: it inject()s RouteStore to clear the browsed route names on logout
		const http = { get:vi.fn(), post:vi.fn() };
		const googleAuth = { renewCredential: vi.fn().mockResolvedValue(renewed) };
		const service = new TestService( http as unknown as HttpClient, authStore, googleAuth as unknown as GoogleAuthService );
		return { authStore, logout: vi.spyOn( authStore, 'logout' ), http, googleAuth, service };
	};
	beforeEach( ()=>localStorage.clear() );

	it( 'renews in place and retries with the fresh session', async ()=>{
		const { authStore, logout, http, googleAuth, service } = create( freshJwt );
		googleUser( authStore );
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
		const { authStore, logout, http, googleAuth, service } = create( null );
		googleUser( authStore );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{anon:true}})) );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {anon:true} );
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( http.post ).not.toHaveBeenCalled();
		expect( logout ).toHaveBeenCalled();
	});

	it( 'password/OpcServer users never reach the silent path', async ()=>{
		const { authStore, logout, http, googleAuth, service } = create( freshJwt );
		authStore.append( {id:'bob', provider:EProvider.OpcServer, sessionId:'stale-session'} as UserJson );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{anon:true}})) );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {anon:true} );
		expect( googleAuth.renewCredential ).not.toHaveBeenCalled();
		expect( logout ).toHaveBeenCalled();
	});

	it( 'concurrent 401s coalesce into one renewal round-trip', async ()=>{
		const { authStore, http, googleAuth, service } = create( null );
		googleUser( authStore );
		let resolveCredential!:( c:string|null )=>void;
		googleAuth.renewCredential.mockImplementation( ()=>new Promise<string|null>(r=>resolveCredential=r) );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( the401() ).mockReturnValue( of({ok:1}) );
		http.post.mockReturnValue( loginResponse() );

		const p1 = service.authGetPublic( 'a', 'stale-session' );
		const p2 = service.authGetPublic( 'b', 'stale-session' );
		await flush();//let both 401s land in renewGoogleSession before the credential resolves
		resolveCredential( freshJwt );
		await expect( p1 ).resolves.toEqual( {ok:1} );
		await expect( p2 ).resolves.toEqual( {ok:1} );
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( http.post ).toHaveBeenCalledTimes( 1 );//one /login for both requests
	});

	it( 'a second 401 against the fresh session falls through without a second prompt', async ()=>{
		const { authStore, logout, http, googleAuth, service } = create( freshJwt );
		googleUser( authStore );
		http.get.mockReturnValue( the401() );//every request 401s, renewed session included
		http.post.mockReturnValue( loginResponse() );

		await expect( service.authGetPublic('data', 'stale-session') ).rejects.toMatchObject( {status:401} );//the final anonymous 401 must throw
		expect( googleAuth.renewCredential ).toHaveBeenCalledTimes( 1 );
		expect( logout ).toHaveBeenCalled();
	});

	it( 'a connected socket is re-authenticated with the fresh session', async ()=>{
		const { authStore, http, service } = create( freshJwt );
		googleUser( authStore );
		service.setSocket( 5 );
		http.get.mockReturnValueOnce( the401() ).mockReturnValueOnce( of({ok:true}) );
		http.post.mockReturnValue( loginResponse() );

		await expect( service.authGetPublic('data', 'stale-session') ).resolves.toEqual( {ok:true} );
		expect( service.sentAuthorization.mock.calls ).toEqual( [[5]] );
	});
});

//reviews/m3-closing.md #22:  a mutation went out with the stored session and a 401 back - the hub restarted, the tab idled past
///http/timeout, the laptop changed network - was only reported:  the stale session stayed, and every Save repeated the 401 until
//some unrelated read happened to renew it.  A POST now takes GET's policy:  renew a Google session and retry once;  an
//anonymous session retries anonymously;  a signed-in user who cannot be renewed is signed out and the mutation is refused,
//never re-run as anonymous.
//A23:  authGet sends one request either way;  only a GET with no session yet - anonymous, or a Bearer jwt - observes the
//response, for the session its Authorization header opens.
describe( 'ProtoService GET', ()=>{
	const create = ()=>{
		const authStore = TestBed.inject( AuthStore );
		const http = { get:vi.fn(), post:vi.fn() };
		return { authStore, http, service: new TestService( http as unknown as HttpClient, authStore ) };
	};
	beforeEach( ()=>localStorage.clear() );

	it( 'stores the session an anonymous GET opens', async ()=>{
		const { authStore, http, service } = create();
		http.get.mockReturnValueOnce( of(new HttpResponse({body:{ok:1}, headers:new HttpHeaders({Authorization:'new-anonymous'})})) );
		await expect( service.authGetPublic('data') ).resolves.toEqual( {ok:1} );
		expect( http.get.mock.calls[0][1].observe ).toBe( 'response' );
		expect( authStore.user()?.authorization ).toBe( 'new-anonymous' );
	});

	it( 'takes a session GET\'s body as it comes', async ()=>{
		const { http, service } = create();
		http.get.mockReturnValueOnce( of({ok:2}) );
		await expect( service.authGetPublic('data', 'a-session') ).resolves.toEqual( {ok:2} );
		expect( http.get.mock.calls[0][1] ).toEqual( {headers: {Authorization: 'a-session'}} );
	});
});

describe( 'ProtoService POST on 401', ()=>{
	const loginResponse = ()=>of( new HttpResponse({body:'{}', headers:new HttpHeaders({Authorization:'fresh-session'})}) );
	const create = ( renewed:string|null )=>{
		const authStore = TestBed.inject( AuthStore );
		const http = { get:vi.fn(), post:vi.fn() };
		return { authStore, http, service: new TestService( http as unknown as HttpClient, authStore, {renewCredential: vi.fn().mockResolvedValue(renewed)} as unknown as GoogleAuthService ) };
	};
	beforeEach( ()=>localStorage.clear() );

	it( 'renews a Google session and retries the mutation once with it', async ()=>{
		const { authStore, http, service } = create( freshJwt );
		authStore.append( new User(makeJwt('stale@example.com')) );
		authStore.append( {sessionId:'stale-session'} );
		http.post.mockReturnValueOnce( the401() ).mockReturnValueOnce( loginResponse() ).mockReturnValueOnce( of({saved:true}) );
		await expect( service.postPublic('graphql', {q:1}) ).resolves.toEqual( {saved:true} );
		expect( http.post ).toHaveBeenCalledTimes( 3 );
		expect( http.post.mock.calls[1][0] ).toContain( '/login' );
		expect( http.post.mock.calls[2][2].headers.Authorization ).toBe( 'fresh-session' );
	});

	it( 'retries an anonymous session anonymously', async ()=>{
		const { authStore, http, service } = create( null );
		authStore.append( {sessionId:'stale-anonymous'} );
		http.post.mockReturnValueOnce( the401() ).mockReturnValueOnce( of(new HttpResponse({body:{saved:true}, headers:new HttpHeaders({Authorization:'new-anonymous'})})) );//the server mints a session for an anonymous request
		await expect( service.postPublic('graphql', {q:1}) ).resolves.toEqual( {saved:true} );
		expect( http.post.mock.calls[1][2].headers ).toBeUndefined();
		expect( authStore.user()?.authorization ).toBe( 'new-anonymous' );
	});

	it( 'signs out a user it cannot renew and refuses the mutation', async ()=>{
		const { authStore, http, service } = create( null );
		authStore.append( {id:'bob', provider:EProvider.OpcServer, sessionId:'stale-session'} as UserJson );
		const logout = vi.spyOn( authStore, 'logout' );
		http.post.mockReturnValueOnce( the401() );
		await expect( service.postPublic('graphql', {q:1}) ).rejects.toMatchObject( {status: 401} );
		expect( logout ).toHaveBeenCalled();
		expect( http.post ).toHaveBeenCalledTimes( 1 );//never re-run as anonymous
	});
});