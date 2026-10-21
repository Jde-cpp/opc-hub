import { Injectable, InjectionToken, inject } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import {Instance, resolveInstance} from './app-service-types'

import { pageTransport, ProtoService } from '../proto-service';
import * as FromServer from 'jde-proto/App.FromServer';
import * as FromClient from 'jde-proto/App.FromClient';
import { IAuth, IENVIRONMENT, IEnvironment, User } from 'jde-spa';
import { IGraphQL } from '../graphql';
import { AUTH_STORE } from '../auth-store';
import { GoogleAuthService } from '../google-auth-service';
import { StringUtils } from '../../utils/string-utils';
import { errorMessage } from '../../utils/errors';

@Injectable( {providedIn: 'root'} )
export class AppService extends ProtoService<FromClient.Transmission,FromServer.Message> implements IGraphQL, IAuth{
	constructor(){
		const environment:IEnvironment = inject( IENVIRONMENT );
		super( FromClient.Transmission, inject(HttpClient), pageTransport(), inject(AUTH_STORE), inject(GoogleAuthService) );
		let appServer = environment.get<Instance>( 'applicationServer' );
		if( !appServer ){
			console.log( "No Application Server set in environment" );
			appServer = { port:1967, host:"" };
		}
		super.instances = [appServer];//the setter resolves an empty or loopback host to the page's
	}
	protected override get product(){ return "The application server"; }
	// ping():Promise<string>{
	// 	return this.sendSingularRequest( FromClient.ERequest.Ping );
	// }

	//App.FromClient's session_id is a `uint32` - a session PK the AppServer turns straight back into the session key with
	//{:x} - while the browser holds it as the hex string the Authorization response header carried.  So it has to go back as
	//a NUMBER;  the raw string threw @bufbuild's assertUInt32 and left the socket unauthenticated (see the base method).
	//A user holding only a jwt has no session to adopt yet:  Message.jwt is a DELEGATED validation (ServerSocketSession::Login
	//echoes the answer and installs nothing on the socket), not a handshake, so there is nothing to send.
	protected override authorizationMessage( authorization:string ):any|undefined{
		const sessionId = /^[0-9a-fA-F]+$/.test( authorization ) ? parseInt( authorization, 16 ) : NaN;//parseInt alone stops at the first bad digit, so "Bearer ey..." would have become NaN but "1a2bZZ" a plausible-looking 6699
		if( !Number.isSafeInteger(sessionId) || sessionId<=0 || sessionId>0xFFFFFFFF ){
			console.warn( `AppService.authorizationMessage: '${authorization}' is not a uint32 hex session id.` );
			return undefined;
		}
		return { sessionId };
	}

	//The registry's host is the instance's configured one - `localhost` on an install and under ng serve - which reaches it
	//only from its own machine; resolved to the page's host here, so a caller building a url or showing the row sees the name
	//that works from where the page runs (resolveInstanceHost, install-issues #2).
	async gatewayInstances():Promise<Instance[]>{
		const y = await this.get( "opcGateways" ) as any;
		return (y["servers"] as Instance[]).map( instance=>resolveInstance(instance) );
	}

	async opcServerInstances():Promise<Instance[]>{
		const y = await this.get( "opcServers" ) as any;
		return (y["servers"] as Instance[]).map( instance=>resolveInstance(instance) );
	}

	async instancePK( instanceName:string, programName?:string ):Promise<number|undefined>{
		const connections = await this.queryArray<{id?:number, instanceId?:number, instanceName:string, programName:string}>( "connections(deleted:null){instanceId instanceName programName}" );
		const strip = ( p:string )=> p?.startsWith("Jde.") ? p.substring(4) : p;
		const match = connections.find( c=>c.instanceName==instanceName && (!programName || strip(c.programName)==strip(programName)) );//instance names are only unique per program — "Debug" exists for both the AppServer and the gateway
		return match?.instanceId ?? match?.id;//servers whose connections view predates the instanceId column emit the instance pk under 'id'
	}

	async login( user:User ):Promise<void>{
		console.assert( !user.sessionId );
		await super.loginJwt( user.authorization! );//the sessionId arrives in the response Authorization header — postRaw already appended it to authStore; the /login body is metadata ({expiration}), NOT the sessionId
		this.authStore.append( user );//persist the jwt/identity fields; user.sessionId stays undefined so append() keeps the header-derived sessionId instead of clobbering it with the body
	}
	loginPassword( username:string, password:string, authenticator:string ):Promise<void>{
		throw new Error( "noImpl" );
	}
	async logout():Promise<void>{
		if( this.log.restRequests )	console.log( `logout()` );
		const options = { observe: "response", headers:{"Authorization":this.user()?.authorization} };
		try{
			const result = await this.postRaw<string>( 'logout', {}, options );
			if( this.log.restResults ) console.log( `logout=>${result}` );
		}
		catch( e:unknown ){
			console.log( `logout failed:  ${errorMessage(e)}` );//angular-review3 C12/L8: the local session goes regardless, as GatewayService.logout already does - a failed round trip must not leave the user logged in with no way out.
		}
		this.authStore.logout();
	}

	async googleAuthClientId():Promise<string>{
		return await super.querySetting( "googleAuthClientId" );
	}

	public async validateSessionId():Promise<User | null>{
		let user = this.user();
		if( !user )
			return Promise.resolve( null );
		const y = await this.query<{session:{domain:string,loginName:string}}>( `session( id:${StringUtils.qlString(user.sessionId!)} ){ domain loginName }` );
		return new User( { domain:y.session.domain, id:y.session.loginName, sessionId:user.sessionId } );
	}

	protected processMessage( bytearray:Uint8Array ){
		try{
			let t:FromServer.Transmission;
			try{
				t = FromServer.Transmission.decode( bytearray );
			}
			catch( e ){
				throw new Error( `error decode ${bytearray.length} bytes, error: ${JSON.stringify(e)}` );
			}
			for( const message of t.messages ){
				const requestId = message.requestId as number;
				if( super.processCommonMessage(message, requestId) )
					continue;
				if( message.ack ){//first message after handshake
					console.log( `[App.${requestId}]Connected to '${super.socketUrl}', socketId: ${message.ack}` );
					let socketId = message.ack;
					if( this.user()?.authorization )//not `this.user()`: logout() leaves a truthy serverInstances-only User whose authorization is null
						super.sendAuthorization( socketId );
					else{
						console.warn( `no authorization` );
						this.setSocketId( socketId );
					}
				}
				else if( message.exception ){
					if( !requestId || !super.processError(message.exception, requestId) )
						throw new Error( `[App.${requestId}]Error:  (${message.exception.statusCode})(${message.exception.code!.toString(16)})${message.exception.what}` );
				}
				else
					throw new Error( `unknown message type ${Object.keys(message)}` );
			}
		}
		catch( e ){
			console.error( e );
		}
		return bytearray;
	}
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const APP_SERVICE = new InjectionToken<AppService>( 'AppService' );