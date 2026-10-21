import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { AppService, AUTH_STORE, AuthStore, ETransport, GoogleAuthService, Instance, ProtoService } from 'jde-framework';

import * as FromClient from 'jde-proto/Opc.FromClient';
import * as FromServer from 'jde-proto/Opc.FromServer';

//An OpcServer registers with the application server like any other app, so the instance list (host/port/instanceName)
//comes from its /opcServers endpoint - but every query then goes straight at that instance's own web server:  `logs` is
//answered out of the process's own archive (App::LogQLAwait), so the AppServer cannot serve it on the server's behalf.
@Injectable( {providedIn: 'root'} )
export class OpcServerService{
	#authStore = inject( AUTH_STORE );

	//the promise is the cache, so concurrent callers share one round trip instead of racing to build duplicate services.
	servers():Promise<OpcServer[]>{
		return this.#servers ??= this.appService.opcServerInstances().then(
			instances=>instances.map( instance=>new OpcServer(instance, this.appService.transport, this.http, this.#authStore, this.googleAuth) ) )
			.catch( (e)=>{ this.#servers = undefined; throw e; } );
	} #servers:Promise<OpcServer[]>|undefined;

	async server( instanceName:string ):Promise<OpcServer>{
		const y = (await this.servers()).find( server=>server.name==instanceName );
		if( !y )//FindApplications only reports *connected* sessions, so a stopped server is a miss, not an empty result
			throw new Error( `No OpcServer instance named '${instanceName}' is connected to the application server.` );
		return y;
	}

	appService = inject( AppService );
	googleAuth = inject( GoogleAuthService );//OpcServer is built by hand below, so the silent-renewal service is threaded through rather than injected there
	http = inject( HttpClient );
}

//REST only:  Opc::Server::RequestHandler::WebsocketSession asserts, so neither socket hook below can fire - every
//IGraphQL entry point ProtoService offers reaches the server over http.
export class OpcServer extends ProtoService<FromClient.Transmission,FromServer.Message>{
	constructor( instance:Instance, transport:ETransport, http:HttpClient, authStore:AuthStore, googleAuth?:GoogleAuthService ){
		super( FromClient.Transmission, http, transport, authStore, googleAuth );
		super.instances = [instance];
	}
	protected override get product(){ return "OpcServer"; }

	protected override processMessage( bytearray:Uint8Array ):void{ console.error( `OpcServer '${this.name}' has no websocket transport; discarding ${bytearray.length} bytes.` ); }
	protected override handleConnectionError( err:unknown ):void{ console.error( `OpcServer '${this.name}' has no websocket transport.`, err ); }

	get name():string{ return this.instances[0].instanceName!; }
	get host():string{ return this.instances[0].host; }
}