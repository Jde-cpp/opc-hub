import { Injectable, inject } from '@angular/core';
import { IPROFILE_SERVICE } from './profile-service';

//The user's profile on the server, through the 'IProfileService' token - what follows them between browsers.  Logged out, or with
//no provider, there is none:  load() answers the default and save() does nothing.  Per-browser preferences are LocalPrefs'.
@Injectable( { providedIn: 'root' } )
export class ProfileStore{
	#profileService = inject( IPROFILE_SERVICE, {optional: true} );
	async load<T>( key:string, defaultValue:T ): Promise<T>{
		const userKey = this.#profileService?.userKey();
		if( !this.#profileService || !userKey )
			return defaultValue;
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		let json = this.cacheGet( cacheKey );
		if( json===undefined ){
			try{
				json = await this.#profileService.load( key );
			}
			catch( e ){
				console.warn( `ProfileStore.load('${key}') failed - using the default.`, e );
				return defaultValue;//failure not cached ⇒ retried next visit.
			}
			if( this.#profileService.userKey()!=userKey )//the user changed under the await - a lapsed session is reset to anonymous there, and the anonymous answer (no row) was cached as known-absent under this user's key, so after they signed in again the next star saved the defaults over their row (reviews/m3-closing.md #17)
				return defaultValue;
			this.cacheSet( cacheKey, json );
		}
		return json==null ? defaultValue : JSON.parse( json ) as T;//parse per call - callers never share mutable objects.
	}

	//an array saved as JSON, each item revived - a ViewSet's views through View.fromJson
	async loadArray<T>( key:string, revive:( json:any )=>T ):Promise<T[]>{
		const json = await this.load<unknown[]>( key, [] );
		return json.map( revive );
	}

	//THROWS on a failed write.  Catching and warning here made every caller's error handling dead code:  the promise
	//resolved normally, so node-children's .catch and ql-list's try/catch never ran - the UI showed the view as saved,
	//it was gone after a reload, and a console.warn was the only evidence.
	async save<T>( key:string, value:T|string|null ):Promise<void>{
		const userKey = this.#profileService?.userKey();
		if( !this.#profileService || !userKey )
			return;
		const json = ProfileStore.json( value );
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		if( this.cacheGet(cacheKey)===json )//save-as-needed: skip no-op mutations (e.g. unchanged ngOnDestroy saves).
			return;
		this.cacheSet( cacheKey, json );
		try{
			await this.#profileService.save( key, json );
		}
		catch( e ){
			this.cache.delete( cacheKey );//the row is not what we just cached:  drop it so the next save retries instead of being deduped away.
			throw e;
		}
	}
	private static json<T>( value:T|string|null ):string|null{//empty string folds to null - both mean "no row" to the server.
		const json = value!=null && typeof value!='string' ? JSON.stringify( value ) : value as string|null;
		return json || null;
	}
	private static cacheKey( userKey:string, key:string ):string{
		return `${userKey}\u0000${key}`;//user-scoped so re-login as another user can't hit stale entries.
	}
	private cacheGet( key:string ):string|null|undefined{//undefined ⇒ miss, null ⇒ known-absent on the server.
		if( !this.cache.has(key) )
			return undefined;
		const value = this.cache.get( key )!;
		this.cache.delete( key );//re-insert: Map insertion order doubles as recency.
		this.cache.set( key, value );
		return value;
	}
	private cacheSet( key:string, value:string|null ):void{
		this.cache.delete( key );
		this.cache.set( key, value );
		while( this.cache.size>ProfileStore.cacheLimit )
			this.cache.delete( this.cache.keys().next().value! );
	}
	private cache = new Map<string,string|null>();
	private static readonly cacheLimit = 10;//last N profiles kept in memory so page visits don't refetch.
}