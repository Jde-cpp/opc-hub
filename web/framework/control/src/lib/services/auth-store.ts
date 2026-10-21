import { Injectable, Signal, inject, signal, InjectionToken } from '@angular/core';
import { RouteStore, User, UserJson } from 'jde-spa';

const userStorageKey = 'user';

@Injectable({ providedIn: 'root' })
export class AuthStore{
	//A bad stored user must never take the app down.  This is providedIn:'root' at the head of the
	//NavBar -> Authorization -> OpcAuthService -> AppService injection chain, so anything thrown here fails bootstrap, and
	//main.ts only console.errors it:  the user got a blank page on EVERY load, with nothing clearing the offending key.
	//Both halves threw - JSON.parse on a truncated/legacy value, and User.decodeJwt's split('.')[1] on a jwt with no
	//separators.  Treat an unreadable value as "logged out" and drop it, so the next login writes a good one.
	constructor(){
		let userString:string|null = null;
		try{
			userString = localStorage.getItem( userStorageKey );//localStorage access itself throws where site data is blocked
			if( userString )
				this.#userSignal.set( new User(JSON.parse(userString)) );
		}
		catch( e ){
			console.error( `Discarding unreadable localStorage['${userStorageKey}'] (${userString}):`, e );
			try{ localStorage.removeItem( userStorageKey ); }
			catch( e2 ){ console.error( `Could not clear localStorage['${userStorageKey}']:`, e2 ); }
			this.#userSignal.set( undefined );
		}
	}

	append( user:UserJson ):void{
		let current = this.user() ? new User(this.user() as UserJson) : new User();
		current.append( user );
		let stringify = JSON.stringify( current );
		localStorage.setItem( userStorageKey, stringify );
		this.#userSignal.set( current );
	}

	//Drop the session, keeping only the jwt identity (undefined for password/OpcServer logins, which yields an empty
	//User exactly as before).  The old signature took a {url,instance} whose only source was loginWait's read of a
	//serverSettings key the server does not send; with that gone nothing produces an instance, so the parameter and the
	//upsert it drove went with it.
	reset( jwt?:string ):void{
		const user = new User( jwt );
		localStorage.setItem( userStorageKey, JSON.stringify(user) );
		this.#userSignal.set( user );
	}

	//Every logout path lands here - the navbar button through AppService/GatewayService, and both 401 handlers once silent
	//renewal has given up - so this is where the browsed route names get dropped.  They are the previous user's rows.
	logout(){
		this.#routeStore.clear();
		let newAuth = new User( {serverInstances:this.user()?.serverInstances} );
		if( newAuth.serverInstances ){
			localStorage.setItem( userStorageKey, JSON.stringify(newAuth) );
		}
		else
			localStorage.removeItem( userStorageKey );
		this.#userSignal.set( newAuth );
	}

	#routeStore = inject( RouteStore );
	#userSignal = signal<User | undefined>( undefined );
	get user():Signal<User | undefined>{ return this.#userSignal.asReadonly(); }
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const AUTH_STORE = new InjectionToken<AuthStore>( 'AuthStore' );