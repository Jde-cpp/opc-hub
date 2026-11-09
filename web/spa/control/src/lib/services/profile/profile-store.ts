import { Injectable, inject } from '@angular/core';
import { IPROFILE_SERVICE } from './profile-service';

//The user's profile on the server, through the 'IProfileService' token - what follows them between browsers.  Logged out, or with
//no provider, there is none:  load() answers the default and save() does nothing.  Per-browser preferences are LocalPrefs'.
@Injectable( { providedIn: 'root' } )
export class ProfileStore{
	#profileService = inject( IPROFILE_SERVICE, {optional: true} );
	//`fresh`:  read past the cache, once the key's queued writes are done - for a value another tab may have written since this
	//one cached it
	async load<T>( key:string, defaultValue:T, fresh=false ): Promise<T>{
		const userKey = this.#profileService?.userKey();
		if( !this.#profileService || !userKey )
			return defaultValue;
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		if( fresh )
			await this.#writes.get( cacheKey )?.catch( ()=>{} );
		let json = fresh ? undefined : this.cacheGet( cacheKey );
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

	//Every row whose key starts with `prefix`, its raw JSON by key - a ViewSet's index and its views' rows in one read.  The
	//answer is one cache entry:  a save under the prefix changes it and a read replaces it whole, so a row another tab deleted is
	//not remembered.  THROWS on a failed read, where load() answers its default:  no rows and a failed read must not look alike
	//to a caller that then writes every row.  Logged out, or with no provider, there are none.
	async loadRows( prefix:string ):Promise<Map<string,string>>{
		const service = this.#profileService, userKey = service?.userKey();
		if( !service || !userKey )
			return new Map();
		const cacheKey = ProfileStore.rowsKey( userKey, prefix );
		let rows = this.cacheGet<Map<string,string>>( cacheKey );
		if( !rows ){
			const under = ProfileStore.cacheKey( userKey, prefix );
			await Promise.all( [...this.#writes].filter( ([key])=>key.startsWith(under) ).map( ([, write])=>write.catch( ()=>{} ) ) );//a write on its way is in the answer
			rows = await service.loadRows( prefix );
			if( service.userKey()!=userKey )//as load():  an anonymous answer has no rows, and cached it would be this user's
				throw new Error( "The user changed while the profile was read." );
			for( const key of [...this.cache.keys()].filter( key=>key.startsWith(under) ) )//a row has no entry of its own beside these:  save() would compare with it once they were evicted
				this.cache.delete( key );
			this.cacheSet( cacheKey, rows );
		}
		return new Map( rows );//a copy per call, as load() parses per call
	}

	//THROWS on a failed write.  Catching and warning here made every caller's error handling dead code:  the promise
	//resolved normally, so node-children's .catch and ql-list's try/catch never ran - the UI showed the view as saved,
	//it was gone after a reload, and a console.warn was the only evidence.
	async save<T>( key:string, value:T|string|null ):Promise<void>{
		const service = this.#profileService, userKey = service?.userKey();
		if( !service || !userKey )
			return;
		const json = ProfileStore.json( value );
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		if( this.#held(cacheKey, key)===json )//save-as-needed: skip no-op mutations (e.g. unchanged ngOnDestroy saves).
			return;
		this.#hold( cacheKey, key, json );
		//A key's writes go one at a time, in the order made:  two in flight landed in either order, and two ticks in a row on a
		//node's children left the server holding the first.
		const previous = this.#writes.get( cacheKey );
		const write = previous ? previous.catch( ()=>{} ).then( ()=>service.save(key, json) ) : service.save( key, json );
		this.#writes.set( cacheKey, write );
		try{
			await write;
		}
		catch( e ){
			if( this.#held(cacheKey, key)===json )//not a later save's value
				this.cache.delete( this.#rowsOver(cacheKey) ?? cacheKey );//the row is not what we just cached:  drop it - with the rows it is among, which no longer say what the server holds - so the next save retries instead of being deduped away.
			throw e;
		}
		finally{
			if( this.#writes.get(cacheKey)===write )
				this.#writes.delete( cacheKey );
		}
	}
	//A change made to the value the server holds now, and written back:  for a key more than one page or tab writes, where saving
	//what one page read drops what another wrote since - a watch list a node page appends to (419-watch #9).  Queued behind the
	//key's writes and read past the cache;  another browser's write between the read and the write still loses.  Logged out, or
	//signed in as someone else by the time the read returns, it writes nothing.  THROWS on a failed read or write.
	async update<T>( key:string, defaultValue:T, change:( current:T )=>T ):Promise<void>{
		const service = this.#profileService, userKey = service?.userKey();
		if( !service || !userKey )
			return;
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		const previous = this.#writes.get( cacheKey );
		const write:Promise<void> = (previous ? previous.catch( ()=>{} ) : Promise.resolve()).then( async ()=>{
			const held = await service.load( key );
			if( service.userKey()!=userKey )
				return;
			const json = ProfileStore.json( change( held==null ? defaultValue : JSON.parse( held ) as T ) );
			if( json!==held )
				await service.save( key, json );
			if( this.#writes.get( cacheKey )===write )//a later save has cached its own value already
				this.cacheSet( cacheKey, json );
		} );
		this.#writes.set( cacheKey, write );
		try{
			await write;
		}
		catch( e ){
			if( this.#writes.get( cacheKey )===write )
				this.cache.delete( cacheKey );
			throw e;
		}
		finally{
			if( this.#writes.get( cacheKey )===write )
				this.#writes.delete( cacheKey );
		}
	}
	private static json<T>( value:T|string|null ):string|null{//empty string folds to null - both mean "no row" to the server.
		const json = value!=null && typeof value!='string' ? JSON.stringify( value ) : value as string|null;
		return json || null;
	}
	private static cacheKey( userKey:string, key:string ):string{
		return `${userKey}\u0000${key}`;//user-scoped so re-login as another user can't hit stale entries.
	}
	private static rowsKey( userKey:string, prefix:string ):string{
		return `${ProfileStore.cacheKey( userKey, prefix )}\u0000`;//apart from the entry of the key that is the prefix itself
	}
	//the entry of the rows a key is among (loadRows), if they are cached
	#rowsOver( cacheKey:string ):string|undefined{
		for( const [entry, value] of this.cache ){
			if( value instanceof Map && cacheKey.startsWith(entry.slice(0, -1)) )
				return entry;
		}
		return undefined;
	}
	//what the cache holds for a key:  its row among the rows it is under, else its own entry
	#held( cacheKey:string, key:string ):string|null|undefined{
		const rows = this.#rowsOver( cacheKey );
		return rows ? this.cacheGet<Map<string,string>>( rows )!.get( key ) ?? null : this.cacheGet( cacheKey );//the rows are every row under the prefix, so one they lack is known-absent
	}
	#hold( cacheKey:string, key:string, json:string|null ):void{
		const rows = this.#rowsOver( cacheKey );
		if( !rows )
			this.cacheSet( cacheKey, json );
		else if( json==null )
			this.cacheGet<Map<string,string>>( rows )!.delete( key );
		else
			this.cacheGet<Map<string,string>>( rows )!.set( key, json );
	}
	private cacheGet<T extends string|null|Map<string,string> = string|null>( key:string ):T|undefined{//undefined ⇒ miss, null ⇒ known-absent on the server.
		if( !this.cache.has(key) )
			return undefined;
		const value = this.cache.get( key )!;
		this.cache.delete( key );//re-insert: Map insertion order doubles as recency.
		this.cache.set( key, value );
		return value as T;
	}
	private cacheSet( key:string, value:string|null|Map<string,string> ):void{
		this.cache.delete( key );
		this.cache.set( key, value );
		while( this.cache.size>ProfileStore.cacheLimit )
			this.cache.delete( this.cache.keys().next().value! );
	}
	private cache = new Map<string,string|null|Map<string,string>>();//a key's JSON, or the rows under a prefix
	#writes = new Map<string,Promise<void>>();//per cache key, the last write queued
	private static readonly cacheLimit = 10;//last N profiles kept in memory so page visits don't refetch - a list's views are one of them, not one each.
}