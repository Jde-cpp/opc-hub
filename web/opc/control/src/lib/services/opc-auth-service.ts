import { Injectable, computed, signal, inject } from '@angular/core';
import { AppService } from 'jde-framework';
import { GATEWAY_SERVICE, Gateway } from './gateway-service';
import { EProvider, IAuth, User } from 'jde-spa';

@Injectable()
export class OpcAuthService implements IAuth{
	#app = inject( AppService );
	#gatewayService = inject( GATEWAY_SERVICE );

	googleAuthClientId(): Promise<string> {
		return this.#app.googleAuthClientId();
	}

	async login( user:User ):Promise<void>{
		let promise = await this.#app.login( user );
		this.isOpc.set( false );
		return promise;
	}
	providers():Promise<EProvider[]>{ return this.#app.providers(); }
	validateSessionId():void{ this.#app.validateSessionId(); }

	async logout():Promise<void>{
		let promise;
		if( this.isOpc() )
			promise = await this.#signedInto?.logout();
		else
			promise = await this.#app.logout();
		this.isOpc.set( undefined );
		this.#signedInto = undefined;
		return promise;
	}
	async loginPassword( username:string, password:string, authenticator:string|undefined ):Promise<void>{
		// multiple gateways aren't implemented yet.  gateway/connection/user?
		let gateways = await this.#gatewayService.gateways();
		if( !gateways.length )
			throw new Error( "No gateways defined to authorize user." );
		let gateway = gateways[0];
		let promise = await gateway.login( authenticator ?? "", username, password );
		this.isOpc.set( true );
		this.#signedInto = gateway;
		return promise;
	} #signedInto:Gateway|undefined;
	user = computed( () => this.#app?.user() );
	isOpc = signal<boolean|undefined>( undefined );
}