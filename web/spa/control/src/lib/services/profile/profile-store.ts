import { Injectable, inject } from '@angular/core';
import { IPROFILE_SERVICE, IProfileService } from './profile-service';

type Constructor<T = object> = new (...args: any[]) => T;

function factory<T>(ctor: Constructor<T>, ...args: any[]): T {
  return new ctor(...args);
}

@Injectable( { providedIn: 'root' } )
export class ProfileStore{
	//optional so tests/apps without the provider stay on localStorage.
	private profileService:IProfileService|null = inject( IPROFILE_SERVICE, {optional: true} );
	static showDeleted( collectionName:string ):boolean{
		const item = localStorage.getItem( `${collectionName}.showDeleted` );
		let value = item ? item=="Y" : false;
		return value;
	}
	static setShowDeleted( collectionName:string, showDeleted:boolean ):void{
		if( showDeleted )
			localStorage.setItem( `${collectionName}.showDeleted`, "Y" );
		else
			localStorage.removeItem( `${collectionName}.showDeleted` );
	}
	static local<T>( key:string, defaultValue:T ):T{
		let index = key.indexOf( '/' );
		let defaultKey = index==-1 ? key : key.substring( 0, index );
		let savedDefault = ProfileStore.localDefaults.get( defaultKey );
		if( savedDefault==undefined )
			ProfileStore.localDefaults.set( defaultKey, JSON.stringify( defaultValue ) );

		const item = localStorage.getItem( key );
		if( item )
			ProfileStore.localOriginalValues.set( key, item );
		return item ? JSON.parse( item ) as T : defaultValue;
	}
	//browser-local only:  per-browser preferences (navbar showBreadcrumbs) and the copy the static local() readers and the
	//logged-out/load-failure fallbacks below read back.  Never reaches the server - that is save()'s job.
	set<T>( key:string, value:T|string|null ):void{
		const json = ProfileStore.json( value );
		if( json )
			localStorage.setItem( key, json );
		else
			localStorage.removeItem( key );
	}
	async load<T>( key:string, defaultValue:T ): Promise<T>{
		const userKey = this.profileService?.userKey();
		if( !this.profileService || !userKey )
			return ProfileStore.local<T>( key, defaultValue );//logged out: exactly the pre-server behavior.
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		let json = this.cacheGet( cacheKey );
		if( json===undefined ){
			try{
				json = await this.profileService.load( key );
			}
			catch( e ){
				console.warn( `ProfileStore.load('${key}') failed - using localStorage.`, e );
				return ProfileStore.local<T>( key, defaultValue );//failure not cached ⇒ retried next visit.
			}
			if( this.profileService.userKey()!=userKey )//the user changed under the await - a lapsed session is reset to anonymous there, and the anonymous answer (no row) was cached as known-absent under this user's key, so after they signed in again the next star saved the defaults over their row (reviews/m3-closing.md #17)
				return defaultValue;
			if( json==null ){//no server row: migrate any pre-existing local value up.
				const local = localStorage.getItem( key );
				if( local!=null ){
					json = local;
					//fire & forget; also primes the cache.  Nothing the user did is behind this one, so a warning is the whole
					//story - but it still needs a handler now that save() reports a failure instead of eating it.
					this.save( key, local ).catch( e=>console.warn(`ProfileStore: could not migrate the local '${key}' to the server.`, e) );
				}
			}
			this.cacheSet( cacheKey, json );
		}
		return json==null ? defaultValue : JSON.parse( json ) as T;//parse per call - callers never share mutable objects.
	}

	async loadClassArray<T>( key:string, ctor: Constructor<T>, ...args: any[] ): Promise<T[]>{
		let json = await this.load<any[]>( key, [] );
		return json.map( item => factory(ctor, ...[item, ...args]) );
	}

	static pageSize( key:string ):number{
		return +(localStorage.getItem( key+".pageSize" ) ?? 24);
	}
	static setPageSize( key:string, value:number ):void{
		if( value!=24 )
			localStorage.setItem( key+".pageSize", value.toString() );
		else
			localStorage.removeItem( key+".pageSize" );
	}

	static tabIndex( key:string ):number{
		const item = localStorage.getItem( key );
		let value = item ? +item : 0;
		return value;
	}
	static setTabIndex( key:string, value:number|undefined ):void{
		if( value )
			localStorage.setItem( key, value.toString() );
		else
			localStorage.removeItem( key );
	}
	static viewIndex( collectionName:string ):number{
		const item = localStorage.getItem( `${collectionName}/viewIndex` );
		return item ? +item : 0;
	}
	static setViewIndex( collectionName:string, value:number ):void{
		if( value )
			localStorage.setItem( `${collectionName}/viewIndex`, value.toString() );
		else
			localStorage.removeItem( `${collectionName}/viewIndex` );
	}

	//server only:  the user's profile row, through the 'IProfileService' token.  Logged out, or with no provider, there is
	//nowhere to write and this is a no-op - a browser-local copy is set()'s job.
	//THROWS on a failed write.  Catching and warning here made every caller's error handling dead code:  the promise
	//resolved normally, so node-children's .catch and ql-list's try/catch never ran - the UI showed the view as saved,
	//it was gone after a reload, and a console.warn was the only evidence.
	async save<T>( key:string, value:T|string|null ):Promise<void>{
		const userKey = this.profileService?.userKey();
		if( !this.profileService || !userKey )
			return;
		const json = ProfileStore.json( value );
		const cacheKey = ProfileStore.cacheKey( userKey, key );
		if( this.cacheGet(cacheKey)===json )//save-as-needed: skip no-op mutations (e.g. unchanged ngOnDestroy saves).
			return;
		this.cacheSet( cacheKey, json );
		try{
			await this.profileService.save( key, json );
		}
		catch( e ){
			this.cache.delete( cacheKey );//the row is not what we just cached:  drop it so the next save retries instead of being deduped away.
			throw e;
		}
	}
	private static json<T>( value:T|string|null ):string|null{//empty string folds to null - both mean "no row" to the server and "remove" to localStorage.
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
	private static localDefaults:Map<string, string> = new Map<string, string>();
	//#defaults:Map<string, string> = new Map<string, string>();
	private static localOriginalValues:Map<string, string> = new Map<string, string>();
}
