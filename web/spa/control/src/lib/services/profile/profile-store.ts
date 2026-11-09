import { Injectable, inject } from '@angular/core';
import { IPROFILE_SERVICE } from './profile-service';

//The user's profile on the server, through the 'IProfileService' token - what follows them between browsers.  Logged out, or with
//no provider, there is none:  load() answers the default and save() does nothing.  Per-browser preferences are LocalPrefs'.
@Injectable( { providedIn: 'root' } )
export class ProfileStore{
	#profileService = inject( IPROFILE_SERVICE, {optional: true} );
	//whose profile is read and written:  signed out there is nobody's, and neither happens
	get userKey():string|undefined{ return this.#profileService?.userKey(); }
	//`fresh`:  read past the cache, once the key's queued writes are done - for a value another tab may have written since this
	//one cached it
	async load<T>( key:string, defaultValue:T, fresh=false ): Promise<T>{
		const userKey = this.#profileService?.userKey();
		if( !this.#profileService || !userKey )
			return defaultValue;
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		if( fresh )
			await this.#writes.get( cacheKey )?.catch( ()=>{} );
		let json = fresh ? undefined : this.#held( cacheKey, key );
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
			this.#hold( cacheKey, key, json );
		}
		return json==null ? defaultValue : JSON.parse( json ) as T;//parse per call - callers never share mutable objects.
	}

	//Every row whose key starts with `prefix`, its raw JSON by key - a ViewSet's index and its views' rows in one read.  The
	//answer is one entry of #rows:  a save under the prefix changes it and a read replaces it whole, so a row another tab deleted is
	//not remembered.  THROWS on a failed read, where load() answers its default:  no rows and a failed read must not look alike
	//to a caller that then writes every row.  Logged out, or with no provider, there are none.
	async loadRows( prefix:string ):Promise<Map<string,string>>{
		const service = this.#profileService, userKey = service?.userKey();
		if( !service || !userKey )
			return new Map();
		const under = ProfileStore.cacheKey( userKey, prefix );
		let rows = ProfileStore.cacheGet( this.#rows, under );
		if( !rows ){
			await Promise.all( [...this.#writes].filter( ([key])=>key.startsWith(under) ).map( ([, write])=>write.catch( ()=>{} ) ) );//a write on its way is in the answer
			rows = await service.loadRows( prefix );
			if( service.userKey()!=userKey )//as load():  an anonymous answer has no rows, and cached it would be this user's
				throw new Error( "The user changed while the profile was read." );
			for( const key of [...this.cache.keys()].filter( key=>key.startsWith(under) ) )//a row has no entry of its own beside these:  save() would compare with it once they were evicted
				this.cache.delete( key );
			ProfileStore.cacheSet( this.#rows, under, rows );
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
				this.#forget( cacheKey );//the row is not what we just cached:  drop it - with the rows it is among, which no longer say what the server holds - so the next save retries instead of being deduped away.
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
				this.#hold( cacheKey, key, json );
		} );
		this.#writes.set( cacheKey, write );
		try{
			await write;
		}
		catch( e ){
			if( this.#writes.get( cacheKey )===write )
				this.#forget( cacheKey );
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
	//the #rows entry a key is among, if there is one
	#rowsOver( cacheKey:string ):string|undefined{
		return [...this.#rows.keys()].find( under=>cacheKey.startsWith(under) );
	}
	//what is known of a key:  its row among the rows it is under, else its own entry.  undefined ⇒ nothing, null ⇒ known-absent.
	#held( cacheKey:string, key:string ):string|null|undefined{
		const rows = this.#rows.get( this.#rowsOver(cacheKey) ?? "" );
		return rows ? rows.get( key ) ?? null : ProfileStore.cacheGet( this.cache, cacheKey );//the rows are every row under the prefix, so one they lack is known-absent
	}
	#hold( cacheKey:string, key:string, json:string|null ):void{
		const rows = this.#rows.get( this.#rowsOver(cacheKey) ?? "" );
		if( !rows )
			ProfileStore.cacheSet( this.cache, cacheKey, json );
		else if( json==null )
			rows.delete( key );
		else
			rows.set( key, json );
	}
	#forget( cacheKey:string ):void{
		const under = this.#rowsOver( cacheKey );
		if( under )
			this.#rows.delete( under );
		else
			this.cache.delete( cacheKey );
	}
	private static cacheGet<V>( cache:Map<string,V>, key:string ):V|undefined{
		if( !cache.has(key) )
			return undefined;
		const value = cache.get( key )!;
		cache.delete( key );//re-insert: Map insertion order doubles as recency.
		cache.set( key, value );
		return value;
	}
	private static cacheSet<V>( cache:Map<string,V>, key:string, value:V ):void{
		cache.delete( key );
		cache.set( key, value );
		while( cache.size>ProfileStore.cacheLimit )
			cache.delete( cache.keys().next().value! );
	}
	private cache = new Map<string,string|null>();//a key's JSON
	#rows = new Map<string,Map<string,string>>();//the rows read under a prefix (loadRows), by the cache key of the prefix
	#writes = new Map<string,Promise<void>>();//per cache key, the last write queued
	private static readonly cacheLimit = 10;//last N of each kept in memory so page visits don't refetch - a list's views are one of the rows, not one each.
}