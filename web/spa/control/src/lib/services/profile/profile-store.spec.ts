if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { vi } from 'vitest';
import { IPROFILE_SERVICE } from './profile-service';
import { ProfileStore } from './profile-store';

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
