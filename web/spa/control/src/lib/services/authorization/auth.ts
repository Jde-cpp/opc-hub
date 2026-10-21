import { Signal, InjectionToken } from '@angular/core';

export enum EProvider{
	None = 0,
	Google=1,
	Facebook=2,
	Amazon=3,
	Microsoft=4,
	VK=5,
	Key = 6,
	OpcServer = 7
};
export class UserJson{
	constructor(){
		this.serverInstances = [];
	}
  id?: string; //loginName
	sessionId?: string;
	jwt?: string;
	domain?: string;
	exp?: Date;	//expiration
  email?: string;
	name?: string;
  picture?: string;
	provider?: EProvider;
	serverInstances?:{url:string,instance:number}[];
}
export class User extends UserJson {
	constructor( arg?:UserJson|string ){
		super();
		this.jwt = typeof arg === 'string' ? <string>arg : arg ? (<UserJson>arg).jwt : undefined;
		if( this.jwt ){
			const jwt = User.decodeJwt( this.jwt );
			this.email = jwt.email;
			this.name = jwt.name;
			this.picture = jwt.picture;
			this.exp = new Date( jwt.exp * 1000 );
			if( jwt.iss=="https://accounts.google.com" )
				this.provider = EProvider.Google;
		}
		if( arg && !this.jwt )
			this.append( arg as UserJson );//faithfully preserve id/domain/provider/sessionId/serverInstances for password/OpcServer logins — dropping id broke isLoggedIn across reload & append-clone
	}

	append( updated:UserJson ):void{
		for( let key in updated ){
			const k = key as keyof UserJson;
			if( updated[k] !== undefined )
				(this as Record<string,unknown>)[k] = updated[k];
		}
	}

	static decodeJwt(idToken: string):any{
		const base64Url = idToken.split( "." )[1];
		if( !base64Url )//a legacy/truncated stored value used to surface as `.replace of undefined` from inside the AuthStore constructor - see its comment
			throw new Error( `'${idToken}' is not a jwt:  no '.'-separated payload.` );
		const base64 = base64Url.replace( /-/g, "+" ).replace( /_/g, "/" );
		const jsonPayload = decodeURIComponent(
			window.atob( base64 ).split("")
				.map( (c)=>"%" + ("00" + c.charCodeAt(0).toString(16)).slice(-2) )
				.join("")
		);
		return JSON.parse( jsonPayload );
	}
	override toString():string{
		if( this.jwt )
			return JSON.stringify( {sessionId:this.sessionId, jwt:this.jwt, serverInstances: this.serverInstances} );
		else
			return JSON.stringify( this );
	}
	get authorization(){ return this.sessionId ?? (this.jwt ? `Bearer ${this.jwt}` : null); }
}
export interface IAuth{
	login( user:User ):Promise<void>;
	loginPassword( username:string, password:string, authenticator:string|undefined ):Promise<void>;
	logout():Promise<void>;
	providers():Promise<EProvider[]>;
	validateSessionId():void;
	googleAuthClientId():Promise<string>;
	user:Signal<User | undefined>;
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const IAUTH = new InjectionToken<IAuth>( 'IAuth' );