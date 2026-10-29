//ProfileStore is the server row through the 'IProfileService' token:  save() deduped against a 10-entry LRU of JSON strings,
//load() parsing per call so callers never share mutable objects.  Logged out, without a provider or on a failed load it answers
//the default - localStorage is LocalPrefs', and never read here.
import { Injector, runInInjectionContext, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { IPROFILE_SERVICE, IProfileService } from './profile-service';
import { ProfileStore } from './profile-store';
import { flush, MockProfileService, silence } from 'jde-spa/testing';

//ProfileStore reads its service through inject() (review3 C5/C13), so a bare `new` needs an injection context carrying the token.
const createStore = ( service:IProfileService|null=null )=>runInInjectionContext( Injector.create({providers: [{provide: IPROFILE_SERVICE, useValue: service}]}), ()=>new ProfileStore() );

describe( 'ProfileStore', ()=>{
	beforeEach( ()=>localStorage.clear() );

	it( 'logged out: load answers the default, never a local value under the key, and save is a no-op', async ()=>{
		const service = new MockProfileService();
		service.setUser( undefined );//logged out
		const store = createStore( service );
		localStorage.setItem( 'k', JSON.stringify({a:1}) );
		await expect( store.load('k', {a:0}) ).resolves.toEqual( {a:0} );
		await store.save( 'k', {a:2} );
		expect( localStorage.getItem('k') ).toBe( JSON.stringify({a:1}) );
		expect( service.load ).not.toHaveBeenCalled();
		expect( service.save ).not.toHaveBeenCalled();
	});

	it( 'no provider: load answers the default and save has nowhere to go', async ()=>{
		const store = createStore( null );
		await expect( store.load('k', 'd') ).resolves.toBe( 'd' );
		await store.save( 'k', 'v' );
		expect( localStorage.getItem('k') ).toBeNull();
	});

	it( 'logged in: one fetch per key, later loads served from the cache with a fresh parse each time', async ()=>{
		const service = new MockProfileService();
		service.rows.set( 'k', JSON.stringify({a:1}) );
		const store = createStore( service );
		const first = await store.load( 'k', {} );
		const second = await store.load( 'k', {} );
		expect( service.load ).toHaveBeenCalledTimes( 1 );
		expect( second ).toEqual( first );
		expect( second ).not.toBe( first );//callers must not share one mutable object
	});

	it( 'no server row: default returned and the absence is cached', async ()=>{
		const service = new MockProfileService();
		const store = createStore( service );
		await expect( store.load('k', 'd') ).resolves.toBe( 'd' );
		await expect( store.load('k', 'd2') ).resolves.toBe( 'd2' );
		expect( service.load ).toHaveBeenCalledTimes( 1 );//negative-cached
	});

	it( 'save skips the mutation when the value is unchanged, and never touches localStorage', async ()=>{
		const service = new MockProfileService();
		const store = createStore( service );
		await store.save( 'k', {a:1} );
		await store.save( 'k', {a:1} );//unchanged ngOnDestroy-style save
		expect( localStorage.getItem('k') ).toBeNull();
		expect( service.save ).toHaveBeenCalledTimes( 1 );
		await store.save( 'k', {a:2} );
		expect( service.save ).toHaveBeenCalledTimes( 2 );
	});

	it( 'load failure answers the default and is not cached', async ()=>{
		const service = new MockProfileService();
		service.load.mockRejectedValue( new Error('down') );
		const store = createStore( service );
		const warn = silence();
		try{
			await expect( store.load('k', 'd') ).resolves.toBe( 'd' );
			await store.load( 'k', 'd' );
			expect( service.load ).toHaveBeenCalledTimes( 2 );//failure retried, not cached
		}
		finally{ warn.mockRestore(); }
	});

	//angular-review3 #11: save() used to catch and console.warn, so it resolved normally and every caller's error handling
	//was dead code - the UI showed the view as saved and it was gone after a reload.
	it( 'save failure REJECTS, and is not cached: the next save retries instead of being deduped away', async ()=>{
		const service = new MockProfileService();
		service.save.mockRejectedValue( new Error('down') );
		const store = createStore( service );
		await expect( store.save('k', 'v') ).rejects.toThrow( 'down' );
		await expect( store.save('k', 'v') ).rejects.toThrow( 'down' );//same value, but the failed write left nothing cached to compare against
		expect( service.save ).toHaveBeenCalledTimes( 2 );
	});

	//two ticks in a row on a node's children were two writes in flight, and the server kept whichever landed last
	it( "a key's saves reach the server in the order made", async ()=>{
		const service = new MockProfileService();
		let release!:()=>void;
		service.save.mockImplementationOnce( async ( key:string, value:string|null )=>{ await new Promise<void>( r=>release=r ); service.rows.set( key, value! ); } );
		const store = createStore( service );
		const first = store.save( 'k', 'a' ), second = store.save( 'k', 'ab' );
		await flush();
		expect( service.save ).toHaveBeenCalledTimes( 1 );//the second waits on the first
		release();
		await Promise.all( [first, second] );
		expect( service.rows.get('k') ).toBe( 'ab' );
	});

	it( 'a failed save holds up nothing, and drops no later value from the cache', async ()=>{
		const service = new MockProfileService();
		service.save.mockRejectedValueOnce( new Error('down') );
		const store = createStore( service );
		const first = store.save( 'k', 'a' ), second = store.save( 'k', 'ab' );
		await expect( first ).rejects.toThrow( 'down' );
		await second;
		expect( service.rows.get('k') ).toBe( 'ab' );
		await store.save( 'k', 'ab' );
		expect( service.save ).toHaveBeenCalledTimes( 2 );//'ab' is still cached, so the repeat is deduped
	});

	it( 'a successful save resolves', async ()=>{
		const store = createStore( new MockProfileService( ) );
		await expect( store.save('k', 'v') ).resolves.toBeUndefined();
	});

	it( 'the 11th key evicts the least-recently-used entry', async ()=>{
		const service = new MockProfileService();
		const store = createStore( service );
		for( let i=0; i<11; ++i )
			await store.load( `k${i}`, 'd' );
		expect( service.load ).toHaveBeenCalledTimes( 11 );
		await store.load( 'k10', 'd' );//still cached
		expect( service.load ).toHaveBeenCalledTimes( 11 );
		await store.load( 'k0', 'd' );//evicted ⇒ refetched
		expect( service.load ).toHaveBeenCalledTimes( 12 );
	});

	it( 'cache entries are user-scoped: a different login cannot hit them', async ()=>{
		const service = new MockProfileService();
		service.rows.set( 'k', JSON.stringify('a-data') );
		const store = createStore( service );
		await expect( store.load('k', 'd') ).resolves.toBe( 'a-data' );
		service.setUser( 'u2' );
		service.rows.set( 'k', JSON.stringify('b-data') );
		await expect( store.load('k', 'd') ).resolves.toBe( 'b-data' );
		expect( service.load ).toHaveBeenCalledTimes( 2 );
	});

	it( 'loadArray revives each item', async ()=>{
		class Item{
			constructor( public json:{n:number}, public extra:string ){}
		}
		const service = new MockProfileService();
		service.rows.set( 'k', JSON.stringify([{n:1},{n:2}]) );
		const store = createStore( service );
		const items = await store.loadArray( 'k', json=>new Item(json, 'x') );
		expect( items ).toHaveLength( 2 );
		expect( items[0] ).toBeInstanceOf( Item );
		expect( items[1].json.n ).toBe( 2 );
		expect( items[0].extra ).toBe( 'x' );
	});
});

//reviews/m3-closing.md #17:  a returning user whose server session had lapsed was reset to anonymous while their profile loaded,
//the anonymous query answered "no row", and load() cached that as known-absent under the real user's key - so after they signed
//in again the next read hit the cached nothing, and the next star saved the defaults over their favorites.
describe( 'ProfileStore.load', ()=>{
	it( 'caches nothing for a user who changed while it loaded', async ()=>{
		const userKey = signal<string|undefined>( "alice" );
		let answer!:( json:string|null )=>void;
		const load = vi.fn( ()=>new Promise<string|null>( resolve=>{ answer = resolve; } ) );
		TestBed.configureTestingModule({ providers: [{ provide: IPROFILE_SERVICE, useValue: {userKey, load, save: vi.fn()} }] });
		const store = TestBed.inject( ProfileStore );
		const first = store.load<string[]>( "favorites", ["default"] );
		userKey.set( undefined );//the lapsed session:  loginWait resets the user under the await, and the query runs anonymously
		answer( null );
		expect( await first ).toEqual( ["default"] );
		userKey.set( "alice" );//signed in again
		const second = store.load<string[]>( "favorites", ["default"] );
		answer( JSON.stringify(["mine"]) );
		expect( await second ).toEqual( ["mine"] );
		expect( load ).toHaveBeenCalledTimes( 2 );
	} );
} );