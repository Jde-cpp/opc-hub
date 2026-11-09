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
});

//#587:  a list's views are a row each under `<list>/views`, with the index at the prefix itself.  One read answers them all and is
//one cache entry, which a save under the prefix keeps current;  and it rejects where load() answers a default, since a caller
//that took a failed read for no rows would write its starting set over the user's.
describe( 'ProfileStore.loadRows', ()=>{
	const create = ()=>{
		const service = new MockProfileService();
		service.rows.set( 'things/views', '[2,1]' );
		service.rows.set( 'things/views/1', '{"a":1}' );
		service.rows.set( 'things/views/2', '{"a":2}' );
		service.rows.set( 'things', '"not a view"' );
		return { service, store: createStore( service ) };
	};
	//the cache's other entries:  `count` keys read, each one entry
	const fill = async ( store:ProfileStore, count:number, from=0 )=>{
		for( let i=from; i<from+count; ++i )
			await store.load( `k${i}`, 'd' );
	};

	it( 'reads every row under the prefix in one fetch, and answers the next read from the cache with a copy', async ()=>{
		const { service, store } = create();
		const rows = await store.loadRows( 'things/views' );
		expect( [...rows] ).toEqual( [['things/views', '[2,1]'], ['things/views/1', '{"a":1}'], ['things/views/2', '{"a":2}']] );
		rows.delete( 'things/views/1' );//the caller's copy
		expect( (await store.loadRows( 'things/views' )).size ).toBe( 3 );
		expect( service.loadRows ).toHaveBeenCalledTimes( 1 );
		expect( service.loadRows ).toHaveBeenCalledWith( 'things/views' );
	} );

	it( "holds a list's rows as one of the cache's ten entries, not one a view", async ()=>{
		const { service, store } = create();
		await store.loadRows( 'things/views' );
		await fill( store, 9 );
		await store.loadRows( 'things/views' );//the tenth entry, and now the latest
		await fill( store, 1, 9 );//evicts k0
		await store.loadRows( 'things/views' );
		expect( service.loadRows ).toHaveBeenCalledTimes( 1 );
		await store.load( 'k0', 'd' );
		expect( service.load ).toHaveBeenCalledTimes( 11 );
	} );

	it( 'a save under the prefix changes the entry, and is skipped as a no-op against it', async ()=>{
		const { service, store } = create();
		await store.loadRows( 'things/views' );
		await store.save( 'things/views/1', {a: 1} );//as the server holds it
		await store.save( 'things/views/9', null );//no such row
		expect( service.save ).not.toHaveBeenCalled();
		await store.save( 'things/views/3', {a: 3} );
		await store.save( 'things/views', [2, 1, 3] );
		await store.save( 'things/views/2', null );
		expect( service.save.mock.calls ).toEqual( [['things/views/3', '{"a":3}'], ['things/views', '[2,1,3]'], ['things/views/2', null]] );
		expect( [...await store.loadRows( 'things/views' )] ).toEqual( [['things/views', '[2,1,3]'], ['things/views/1', '{"a":1}'], ['things/views/3', '{"a":3}']] );
		expect( service.loadRows ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'rejects a failed read, and caches nothing of it', async ()=>{
		const { service, store } = create();
		service.loadRows.mockRejectedValueOnce( new Error('down') );
		await expect( store.loadRows('things/views') ).rejects.toThrow( 'down' );
		expect( (await store.loadRows( 'things/views' )).size ).toBe( 3 );
		expect( service.loadRows ).toHaveBeenCalledTimes( 2 );
	} );

	it( 'drops the entry on a failed save under the prefix, and the next read replaces it whole', async ()=>{
		const { service, store } = create();
		await store.loadRows( 'things/views' );
		service.save.mockRejectedValueOnce( new Error('down') );
		await expect( store.save('things/views/1', {a: 9}) ).rejects.toThrow( 'down' );
		service.rows.delete( 'things/views/2' );//another tab's deletion
		expect( [...(await store.loadRows( 'things/views' )).keys()] ).toEqual( ['things/views', 'things/views/1'] );
		expect( service.loadRows ).toHaveBeenCalledTimes( 2 );
		await store.save( 'things/views/1', {a: 9} );//not deduped against the value that never landed
		expect( service.rows.get('things/views/1') ).toBe( '{"a":9}' );
	} );

	//the row's own entry, made while the rows were not cached, would outlive them and dedupe a later save against a stale value
	it( 'leaves a row no entry of its own beside the rows', async ()=>{
		const { service, store } = create();
		await store.save( 'things/views/1', 'A' );
		await store.loadRows( 'things/views' );
		await store.save( 'things/views/1', 'B' );
		await fill( store, 10 );//the rows are evicted
		await store.save( 'things/views/1', 'A' );
		expect( service.rows.get('things/views/1') ).toBe( 'A' );
	} );

	it( 'logged out, there are no rows and nothing is asked', async ()=>{
		const { service, store } = create();
		service.setUser( undefined );
		expect( (await store.loadRows( 'things/views' )).size ).toBe( 0 );
		expect( service.loadRows ).not.toHaveBeenCalled();
	} );

	//as load() (reviews/m3-closing.md #17):  the anonymous answer is no rows, which cached would be the user's own
	it( 'rejects where the user changed while it read, and caches nothing', async ()=>{
		const { service, store } = create();
		let answer!:( rows:Map<string,string> )=>void;
		service.loadRows.mockImplementationOnce( ()=>new Promise( resolve=>{ answer = resolve; } ) );
		const read = store.loadRows( 'things/views' );
		await flush();//past the writes it waits out
		service.setUser( undefined );
		answer( new Map() );
		await expect( read ).rejects.toThrow( 'The user changed' );
		service.setUser( 'u1' );
		expect( (await store.loadRows( 'things/views' )).size ).toBe( 3 );
	} );
} );

//reviews/m3-closing.md #17:  a returning user whose server session had lapsed was reset to anonymous while their profile loaded,
//A whole-value save of what one page read dropped what another page or tab wrote since (419-watch #9):  update changes the
//value the server holds, after the key's queued writes, and caches the result.
describe( 'ProfileStore.update', ()=>{
	it( 'changes the value the server holds, not the cached one, and caches the result', async ()=>{
		const service = new MockProfileService();
		service.rows.set( 'k', JSON.stringify( ['a'] ) );
		const store = createStore( service ), other = createStore( service );
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( ['a'] );
		await other.save( 'k', ['a', 'b'] );//another tab
		await store.update<string[]>( 'k', [], v=>[...v, 'c'] );
		expect( service.rows.get( 'k' ) ).toBe( JSON.stringify( ['a', 'b', 'c'] ) );
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( ['a', 'b', 'c'] );
		expect( service.load ).toHaveBeenCalledTimes( 2 );//the cache's first read, then update's:  the last load is the cache's
	} );

	it( 'applies changes made together one after another', async ()=>{
		const service = new MockProfileService();
		const store = createStore( service );
		await Promise.all( [store.update<string[]>( 'k', [], v=>[...v, 'a'] ), store.save( 'k', ['x'] ), store.update<string[]>( 'k', [], v=>[...v, 'b'] )] );
		expect( service.rows.get( 'k' ) ).toBe( JSON.stringify( ['x', 'b'] ) );
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( ['x', 'b'] );
	} );

	it( 'writes nothing for an unchanged value, and rejects a failed write without caching it', async ()=>{
		const service = new MockProfileService();
		service.rows.set( 'k', JSON.stringify( ['a'] ) );
		const store = createStore( service );
		await store.update<string[]>( 'k', [], v=>v );
		expect( service.save ).not.toHaveBeenCalled();
		service.save.mockRejectedValueOnce( new Error( 'down' ) );
		await expect( store.update<string[]>( 'k', [], v=>[...v, 'b'] ) ).rejects.toThrow( 'down' );
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( ['a'] );
	} );

	it( 'reads past the cache with `fresh`, once the key\'s queued writes are done', async ()=>{
		const service = new MockProfileService();
		const store = createStore( service ), other = createStore( service );
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( [] );
		await other.save( 'k', ['b'] );//another tab
		expect( await store.load<string[]>( 'k', [] ) ).toEqual( [] );//the cache's
		expect( await store.load<string[]>( 'k', [], true ) ).toEqual( ['b'] );
		let release!:()=>void;
		service.save.mockImplementationOnce( ( key:string, value:string|null )=>new Promise<void>( r=>{ release = ()=>{ service.rows.set( key, value! ); r(); }; } ) );
		const saving = store.save( 'k', ['c'] );
		const loading = store.load<string[]>( 'k', [], true );
		release();
		expect( await loading ).toEqual( ['c'] );
		await saving;
	} );
} );

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