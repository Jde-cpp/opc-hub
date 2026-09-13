import { webSocket, WebSocketSubject } from 'rxjs/webSocket';
import { firstValueFrom } from 'rxjs';
import { HttpClient, HttpErrorResponse, HttpEvent, HttpResponse, HttpSentEvent } from '@angular/common/http';
import { FieldKind } from '../model/ql/schema/field';
import { fromIsoDuration, verify } from '../utils/utils';
import { TableSchema } from '../model/ql/schema/table-schema';
import { EnumValue, Log, IQueryResult, Query } from './graphql';
import { MutationSchema } from '../model/ql/schema/mutation-schema';
import { Instance, resolveInstance } from './app/app-service-types';
import { ELogLevel } from 'jde-proto/Log';
import { Exception as IException } from 'jde-proto/Common';
import { AuthStore } from './auth-store';
import { errorText, httpStatus } from '../utils/errors';
import { GoogleAuthService } from './google-auth-service';
import { Mutation } from '../model/ql/mutation';
import { computed, Signal } from '@angular/core';
import { EProvider, User } from 'jde-spa';
import { StringUtils } from '../utils/string-utils';

export interface IError{ requestId?:number; message: string; sc?:number; httpStatus?:number; }//sc is the proto `code`; httpStatus avoids the opc StatusCode collision.

type TransformInput = (x:any)=>any;
type Resolve = (x:any)=>void;
type Reject = ( e:{error:IError} )=>void;
export type RequestId = number;
export enum ETransport{ Unsecure, Secure, Hybrid };

//status-0/"Failed to fetch" errors are deliberately opaque - the browser hides whether CORS, DNS, or the connection failed.  A no-cors probe reaches the server regardless of its CORS headers, so reachable-but-blocked (a policy problem) can be told apart from unreachable.
export async function describeFetchError( url:string, e:unknown ):Promise<string>{
	const isNetworkError = e instanceof TypeError || (e instanceof HttpErrorResponse && e.status==0);
	if( !isNetworkError || typeof location=="undefined" )
		return e instanceof Error || e instanceof HttpErrorResponse ? e.message : `${e}`;
	const target = new URL( url, location.href );
	try{
		await fetch( target, {mode:"no-cors", cache:"no-store"} );
	}
	catch{
		return `'${target.host}' is unreachable - server down, wrong port, or the hostname doesn't resolve.`;
	}
	return target.hostname==location.hostname
		? `'${target.host}' is reachable but the response was blocked - likely a CORS policy problem on the server.`
		: `'${target.host}' answered but withheld CORS approval for origin '${location.origin}' - with allowOrigin 'sameHost' the page's host must match the server's; pin http/accessControl/allowOrigin to '${location.origin}' or browse the app via '${target.hostname}'.`;
}

class RequestPromise<ResultMessage>{
	constructor( public result:undefined|((arg:ResultMessage)=>any), public resolve:Resolve, public reject:Reject, public transformInput:TransformInput|null=null )
	{}
}

//ts-proto emits a per-message interface plus a codec object, not a class, so the transport is handed the codec rather
//than a constructor - `new Transmission()` no longer exists.  The codec also encodes, which retires the abstract
//encode() hook each subclass used to implement identically.
export interface MessageCodec<T>{
	create( base?:any ):T;
	encode( message:T ):{ finish():Uint8Array };
}

//`session_id` IS the credential the socket authenticates with, and `jwt` is the Google id token - neither belongs in a
//console anyone can screenshot or paste into an issue.  JSON.stringify walks the whole transmission, so the replacer has
//to catch them wherever in the message tree they sit.
const redactCredentials = ( key:string, value:any ):any=>key=="sessionId" || key=="jwt" ? "<redacted>" : value;

export abstract class ProtoService<Transmission,ResultMessage>{
	constructor( private TCreator: MessageCodec<Transmission>, protected http: HttpClient, public readonly transport:ETransport, protected authStore:AuthStore, private isAppServer:boolean=false, protected googleAuth?:GoogleAuthService )
	{}

	connect():void{
		this.#socket = webSocket<Uint8Array>( {url: this.socketUrl, deserializer: msg => this.onMessage(msg), serializer: msg=>msg, binaryType:"arraybuffer"} );
		this.#socket.subscribe(
			( msg ) => this.addMessage( msg ),
			( err ) => this.error( err ),
			() => this.socketComplete()
		);
	}
	//should deserialize put into a constant variable or process in deserialization?
	addMessage( msg:any )
	{}

	toCollectionName( collectionDisplay:string ):string{ return collectionDisplay; }
	subQueries( typeName: string, id: number ):string[]{ return []; }
	slugQuery( schema: TableSchema, slug: string, showDeleted:boolean, excludedColumns:string[] ):string{
		let fields = this.fieldColumns( schema, showDeleted, excludedColumns );
		return `${schema.singular}( slug:${StringUtils.qlString(slug)} ){ ${fields.join(" ")} }`;
	}
	protected fieldColumns( schema: TableSchema, showDeleted:boolean, excludedColumns:string[] ):string[]{
		let columns = [];
		let filtered = schema.fields.filter(
			(x)=>!excludedColumns.includes(x.name) && (x.name!="deleted" || showDeleted) );
		for( const field of filtered ){
			if( field.type.underlyingKind==FieldKind.UNION )
				columns.push( `${field.name}{id}` );
			else if( field.type.underlyingKind==FieldKind.OBJECT )
				columns.push( `${field.name}{id name}` );
			else
				columns.push( field.name );
		}
		return columns;
	}

	//
	error( err:unknown ){
		console.log( "No longer connected to Server.", err );
		this.socketDown( err );
	}
	//a socket that is gone can never answer what was already sent or queued, so both teardown paths must settle everything: an unsettled promise is an await that never returns.
	private socketDown( err:unknown ):void{
		this.#socket = undefined;//drop the dead socket so the next send reconnects
		this.#socketId = 0;//NOT setSocketId(0): that flushes the backlog, and with the socket already dropped every queued transmission would go to `#socket?.next` and vanish
		this.rejectPending();
		this.handleConnectionError( err );
	}
	protected rejectPending():void{
		const message = `Connection to '${this.socketUrl}' was lost.`;
		this.backlog.length = 0;//sendPromise registers its callback whether or not the message actually went out, so the backlog's promises are in _callbacks and reject below - resending them after a reconnect would arrive with no callback left to take the answer
		const pending = [...this._callbacks];
		this._callbacks.clear();//clear before rejecting: a reject handler may send again and register new callbacks
		for( const [requestId, c] of pending )
			c.reject( {error:{requestId, message}} );
	}
	//Was an unconditional, unredacted, untruncated dump of every outgoing transmission - so the handshake printed the
	//session id on every socket open, past the log.sockRequests gate the rest of this class honours.
	sendTransmission( t:Transmission ){
		if( this.log.sockRequests ) console.log( JSON.stringify(t, redactCredentials).substring(0, this.log.maxLength) );
		var toSend = this.TCreator.encode(t).finish();
		this.#socket?.next( toSend );
	}
	send( m:any, log:string ):RequestId{
		const requestId = this.getRequestId();
		this.sendWithId( m, requestId, log );
		return requestId;
	}
	protected sendWithId( m:any, requestId:RequestId, log:string ):void{
		let t = this.TCreator.create() as any;
		if( this.log.subRequest ) console.log( `[${requestId}]${log.substring(0, this.log.maxLength)}` );
		t["messages"].push( {requestId:requestId,...m} );
		const isAuthorization = Object.hasOwn( m, 'sessionId' );//the handshake message that releases the backlog; must go out before socketId is set
		if( this.#socket && (this.socketId || isAuthorization) )
			this.sendTransmission( t );
		else{
			this.backlog.push( t );
			if( !this.#socket )//open exactly one socket; sends before the ack queue rather than each opening a new connection
				this.connect();
		}
	}

	//The handshake message for THIS proto, or undefined when the credential in hand cannot make one.  Opc.FromClient declares
	//`string session_id`, so the base sends the authorization as it stands;  App.FromClient declares `uint32 session_id` and
	//AppService overrides.  Sending the string into the uint32 field threw @bufbuild's assertUInt32 out of sendPromise, and
	//the catch below released the backlog anyway - the socket then ran unauthenticated for the rest of its life.
	protected authorizationMessage( authorization:string ):any|undefined{ return {sessionId: authorization}; }

	//the socketId MUST end up set on every path: setSocketId is what flushes the backlog, so an unsent/failed handshake would otherwise strand every queued send forever.
	protected async sendAuthorization( socketId:number ):Promise<void>{
		const authorization = this.user()?.authorization;
		//logout() leaves a truthy serverInstances-only User whose authorization is null; protobufjs omits the null field, so this used to put an *empty* handshake on the wire that the server never acks
		const message = authorization ? this.authorizationMessage( authorization ) : undefined;
		if( !message )
			console.warn( `sendAuthorization: ${authorization ? "the credential cannot authenticate this socket" : "no authorization"} - releasing the backlog unauthenticated.` );
		else{
			try{
				await this.sendPromise( message, `sendAuthorization: ${authorization}` );
			}
			catch( e ){//callers never await this (it is fired from the ack handler), so swallowing here is what keeps the rejection from floating
				console.error( "sendAuthorization failed - releasing the backlog anyway so queued requests fail against the server instead of hanging.", e );
			}
		}
		this.setSocketId( socketId );//release buffer.
	}

	sendPromise<TResult>( m:any, log:string ):Promise<TResult>{
		const requestId = this.send( m, log );
		return new Promise<TResult>( ( resolve, reject )=>{
			this._callbacks.set( requestId, new RequestPromise(undefined, resolve, reject, null) );
		});
	}

	async initWait():Promise<void>{
		let p = new Promise<void>( (resolve,reject)=>this.#initCallbacks.push({resolve:resolve,reject:reject}) );
		await p;
	}

	async loginWait<Y>( target:string, log:Log=console.log ):Promise<Y>{
		let p = new Promise<Y>( (resolve,reject)=>{
			this.#loginCallbacks.push( {target: target, resolve:resolve, reject:reject, log:log} );
		});
		if( this.#loginCallbacks.length==1 ){
			let url = this.urlWithTarget( "serverSettings", true );
			if( this.log.restRequests ) log( `get: ${url}` );
			try{
				let args = this.user()?.authorization ? {headers:{"Authorization":this.user()!.authorization}} : {} as any;
				const settings:any = await firstValueFrom( this.http.get<any>(url, args) );//`any`, deliberately: HttpClient picks its return type from the option LITERAL, and `args` above is already widened, so a typed body here resolves to HttpEvent<T>
				if( this.log.restResults ) log( JSON.stringify(settings) );
				this.timeoutSeconds = fromIsoDuration( settings["restSessionTimeout"] );
				let active = settings["active"];
				let timedout = this.lastRestCall && ( this.lastRestCall.getTime() < Date.now() - this.timeoutSeconds*1000 );
				//The per-server "instance" comparison that used to gate this is gone: it read settings["serverInstance"],
				//a key Server::SendServerSettings never emits (it sends restSessionTimeout, connectionId and active), so
				//parseInt gave NaN, `NaN != previousInstance` was always true, and on the AppServer path reset() fired on
				//EVERY load - wiping the stored user down to the jwt each time.  Instance tracking lives in AuthStore now.
				if( !active || timedout )
					this.authStore.reset( this.user()?.jwt );
				for( let callback of this.#loginCallbacks ){
					let y = await this.authGet<any>(
						callback.target,
						this.user()!.authorization!,
						callback.log
					);
					callback.resolve( y );
				}
			}
			catch( e ){
				for( let callback of this.#loginCallbacks )
					callback.reject( e );
			}
			this.#loginCallbacks.length=0;
		}
		return p;
	}

	urlWithTarget( suffix:string, preferSecure:boolean=false ):string{
		return preferSecure && this.transport==ETransport.Hybrid
			? `${this.secureRestUrl}/${suffix}`
			: `${this.restUrl}/${suffix}`;
	}

	private async authGet<Y>( target:string, authorization?:string, log:Log=console.log, renew:boolean=true ):Promise<Y>{
		if( target.indexOf("undefined")>=0 )
			console.error( `authGet - target contains 'undefined': ${target}` );
		if( this.log.restRequests )	log( `get: ${decodeURIComponent(target).substring(0,this.log.maxLength)}` );
		let url = this.urlWithTarget(target);
		let y:Y;
		let options:any = {};//`any`, deliberately: HttpClient's overloads key off the object LITERAL's `observe`, which a bag built up field by field cannot express
		if( authorization )
			options["headers"] = { "Authorization": authorization };
		if( !authorization || authorization.startsWith("Bearer ") ){
			options["observe"] = "response";
			options["transferCache"] = { includeHeaders: ["Authorization"] };
			try{
				let response = <HttpResponse<Y>>await firstValueFrom( this.http.get<Y>(url, options) );
				let newAuth = response.headers.get( "Authorization" );
				if( newAuth )
					this.authStore.append( {sessionId:newAuth} );
				y = response.body as Y;
			}
			catch( e:unknown ){
				y = await this.handle401<Y>( e, target, authorization, url, log, renew );
			}
		}
		else{
			try{
				y = await firstValueFrom( this.http.get<Y>(url, options) ) as Y;
			}
			catch( e:unknown ){
				y = await this.handle401<Y>( e, target, authorization, url, log, renew );//authorization is always set here (sessionId branch), so a 401 always retries
			}
		}
		if( this.log.restResults ) log( JSON.stringify(y).substring(0,this.log.maxLength) );
		this.lastRestCall = new Date();
		return y;
	}

	//A 401 with a credential means the session/jwt went stale. A Google user can mint a fresh credential without leaving
	//the page, so try that first and retry authenticated; otherwise retry anonymously — an anonymous 401 must throw, or
	//this recurses forever.
	private async handle401<Y>( e:unknown, target:string, authorization:string|undefined, url:string, log:Log, renew:boolean ):Promise<Y>{
		const status = httpStatus( e );//`e["status"]` off an `any` compiled whatever it was spelled; this narrows once and names it
		if( status!=401 || !authorization ){
			if( status==0 )
				describeFetchError( url, e ).then( m=>log(m) );//async probe; the rethrow keeps the original error for callers
			throw e;
		}
		log( `(${status})${errorText(e)}` );
		if( renew ){//false on the once-renewed retry: a second 401 against the fresh session must fall through to the anonymous retry, not loop into another prompt
			const renewed = await this.renewGoogleSession( log );
			if( renewed )
				return await this.authGet<Y>( target, renewed, log, false );
		}
		this.authStore.logout();
		return await this.authGet<Y>( target, undefined, log );
	}

	//Silent Google re-login (reviews/todo.md §7): renew the lapsed session in place instead of degrading to anonymous.
	//Resolves the fresh authorization, or null when the silent path has nothing to offer — password/OpcServer users have
	//no silent-renewal primitive, and a prompt that produced no credential is a normal outcome (FedCM cooldown, multiple
	//Google sessions, a dismissal), not an error. Concurrent 401s coalesce into one renewal round-trip.
	protected async renewGoogleSession( log:Log ):Promise<string|null>{
		if( this.user()?.provider!=EProvider.Google || !this.googleAuth )
			return null;
		return this.#renewal ??= this.renewGoogleSessionOnce( log ).finally( ()=>this.#renewal=null );
	}
	private async renewGoogleSessionOnce( log:Log ):Promise<string|null>{
		try{
			const credential = await this.googleAuth!.renewCredential();
			if( !credential )
				return null;
			const user = new User( credential );
			this.authStore.append( user );//identity/jwt FIRST: appending a jwt-carrying user rebuilds via the User ctor, whose jwt branch drops an existing sessionId (the auth.store setServerInstance caveat) — so the sessionId must land last
			await this.loginJwt( user.authorization! );//"Bearer <jwt>", the same shape AppService.login sends; the fresh sessionId arrives in the response Authorization header and postRaw appends it
			if( this.socketId )
				this.sendAuthorization( this.socketId );//the socket authenticated with the stale session; move it to the fresh one (not awaited — it settles its handshake internally)
			log( "silent Google re-login renewed the session." );
			return this.user()?.authorization ?? null;
		}
		catch( e ){
			console.warn( "silent Google re-login failed - falling back to the anonymous retry.", e );
			return null;
		}
	}
	#renewal:Promise<string|null>|null = null;

	async get<Y>( target:string, log?:Log ):Promise<Y>{
		if( !this.#instances )
			await this.initWait();
		let isActive = this.lastRestCall && (this.lastRestCall.getTime() > Date.now() - this.timeoutSeconds*1000);
		let y = !this.user()?.authorization || !isActive
			? await this.loginWait<Y>( target, log )
			: await this.authGet<Y>( target, this.user()!.authorization!, log );
		return y;
	}

	async loginJwt( credential:string ):Promise<string>{
		let options:any = {};//as authGet - the HttpClient overload needs the literal
		options["headers"] = { "Authorization": `${credential}` };
		options["observe"] = "response";
		options["transferCache"] = { includeHeaders: ["Authorization"] };
		return await this.postRaw<string>( 'login', null, true, options );
	}

	async post<Y>( target:string, body:any, preferSecure:boolean=false ):Promise<Y>{
		return await this.postRaw<Y>( target, body, preferSecure );
	}

	async postRaw<Y>( target:string, body:any, preferSecure:boolean=false, options?:any ):Promise<Y>{
		if( !this.#instances )
			await this.initWait();
		const url = this.urlWithTarget( target, preferSecure );
		if( !options ){
			if( !this.user()?.authorization )
				options = {observe: "response", transferCache:{includeHeaders:["Authorization"]}};
			else{
				let authorization = this.user()?.authorization;
				if( authorization ){
					options = { headers:{"Authorization":authorization} };
					this.lastRestCall = new Date();
				}
			}
		}

		let event:HttpEvent<Y>|any = await firstValueFrom( this.http.post<Y>(url, body, options) );
		let y:Y;
		if( options.observe=="response" ){
			let response:HttpResponse<Y> = <HttpResponse<Y>>( event instanceof HttpResponse ? event : null );
			verify( response!=null, "response==null" );
			if( options?.transferCache?.includeHeaders.includes("Authorization") ){
				let authorization = response.headers.get( "Authorization" );
				verify( authorization!=null, "no authorization" );
				if( authorization )
					this.authStore.append( {sessionId:authorization} );
			}
			y = <Y>response?.body;
		}
		else
			y = <Y>event;
		return y;
	}

	async postQL<Y>( q:string, vars?:any, log:Log=console.log ):Promise<Y>{
		let args:any = {query: q};
		if( vars )
			args["variables"] = vars;
		if( this.log.restRequests ) log( `post: graphql/${JSON.stringify(args).substring(0,this.log.maxLength)}` );
		const y = await this.post<any>( `graphql`, args, false );
		if( this.log.restResults ) log( JSON.stringify(y).substring(0,this.log.maxLength) );
		return y ? y["data"] : null as unknown as Y;
	}
	async ql<Y>( q:Query, log:Log ):Promise<Y>{
		var target = `graphql?query={${q.text}}`;
		if( q.vars )
			target += `&variables=${encodeURIComponent( JSON.stringify(q.vars))}`;
		const y:any = await this.get( target, log );
		return y ? y["data"] as Y : null as unknown as Y;
	}

	async providers( log:Log ):Promise<EProvider[]>{
		const ql = `__type(name: "Provider") { enumValues { id name } }`;
		const data:any = await this.query( ql, null, log );
		//ql() hands back null when the request fails, and GraphQL introspection answers `__type:null` for a type the schema
		//does not have - both used to TypeError here rather than say what went wrong.
		const enumValues:EnumValue[]|undefined = data?.["__type"]?.["enumValues"];
		if( !enumValues )
			console.warn( `providers: no Provider enum in the schema (${JSON.stringify(data)}) - offering no login providers.` );
		return enumValues?.map( (x:EnumValue)=>x.id ) ?? [];
	}
	async query<Y>( ql:string, vars?:any, log?:Log ):Promise<Y>{
		return await this.ql( {text: ql, vars:vars}, log ?? console.log );
	}
	async queryCount( ql:string, vars?:any, log?:Log ):Promise<number>{
		const y = await this.queryArray<{count:number}>( ql, vars, log ?? console.log );
		return y[0]["count"];
	}
	async querySingle<Y>( ql:string, vars?:any, log?:Log ):Promise<Y>{
		const y = await this.query<any>( ql, vars, log );
		return y[Object.keys(y)[0]];
	}
	async queryObject<Y>( ql:string, cnstrctr: new(...args:any[]) => Y, vars?:any, log?:Log ):Promise<Y>{
		const result = await this.query<any>( ql, vars, log );
		return new cnstrctr( result[Object.keys(result)[0]] );
	}
	async queryArray<Y>( ql:string, vars?:any, log?:Log ):Promise<Y[]>{
		const inputIndex = ql.indexOf('(');
		const fieldIndex = ql.indexOf('{');
		const index = inputIndex<0 ? fieldIndex : fieldIndex<0 ? inputIndex : Math.min( inputIndex, fieldIndex );
		const member = ql.substring( 0, index ).trim();
		const result:any = await this.ql( {text: ql, vars:vars}, log ?? console.log );
		if( !result.hasOwnProperty(member) )
			throw `'${member}' not found in ${JSON.stringify(result)}.`;
		const y = result[member];
		if( !Array.isArray(y) )
			throw `'${member}' is not an array in ${JSON.stringify(y)}.`;
		return y;
	}

	async querySetting( target:string, log:Log ):Promise<string>{
		const queryResult = await this.querySingle<{value:string}>( `setting(target:$target){value}`, {target: target}, log );
		return queryResult.value;
	}
	async querySettings(target:string[], log:Log):Promise<{[key:string]:string}>{
		const queryResult = await this.query<{settings:{target:string, value:string}[]}>( `settings(target:${JSON.stringify(target)}){target value}`, undefined, log );
		let y:{[key:string]:string} = {};
		for( const setting of queryResult.settings )
			y[setting.target] = setting.value;
		return y;
	}

	async mutate<Y>( ql: string|Mutation|Mutation[], log?:Log ):Promise<Y>{
		if( Array.isArray(ql) ){
			let y = [];
			for( let m of <Mutation[]>ql )
				y.push( await this.mutate(m, log) );
			return <Y>y;
		}
		let query = ql instanceof Mutation ? ql.toString() : ql;
		let vars = ql instanceof Mutation ? ql.variables : undefined;
		verify( query );
		return await this.postQL<Y>( `mutation ${query}`, vars, log );
	}

	async schemaWithEnums( type:string, log:Log ):Promise<TableSchema>{
		verify( type[0]==type[0].toUpperCase() );
		let schema = ( await this.schema([type], log) )[0];
		if( !schema.enums ){
			schema.enums = new Map<string, EnumValue[]>();
			for( const field of schema.fields.filter((x)=>x.type.underlyingKind==FieldKind.ENUM) ){
				if( schema.enums.has(field.type.name) )
					continue;//dedup by enum-type name (was `!enums.has(x.name)` — the field name — checked before the loop populated the map, so same-type fields each re-fetched)
				let enumResult = await this.query<{__type: {enumValues: Array<EnumValue>;}}>(
					`__type(name: $type) { enumValues { id name } }`,
					{ type: field.type.name }
				);
				let values:Array<EnumValue>  = enumResult.__type["enumValues"];
				schema.enums.set( field.type.name, values );
			}
		}
		return schema;
	}
	async schema( types:string[], log?:Log ):Promise<TableSchema[]>{
		let results = new Array<TableSchema>();
		let queries =  new Array<string>();
		for( let type of types ){
			if( this.#tables.has(type) )
				results.push(this.#tables.get(type)!);
			else
				queries.push(type);
		};
		for( let type of queries ){
			const ql = `__type(name: $type) { fields { name type { name kind ofType{name kind} } } }`;
			const data:any = await this.query( ql, {type:type}, log );
			if( !data?.["__type"] )//`__type` is an object for a valid type, null for a missing one — `.length` threw on null (and was always undefined!=0 for the object)
				throw `no such type: '${type}'`;
			const schema = new TableSchema( { ...data["__type"], name: type } );//keep the requested canonical type — the server may alias internal names (e.g. User → "UsersQl"), which broke collectionName/singular conventions downstream
			this.#tables.set( type, schema );
			results.push( schema );
		}
		return results;
	}

	async mutations():Promise<MutationSchema[]>{
		//NOTE: needs server-side `__schema{mutationType}` introspection, which the current backend rejects ("Query failed.") — callers must handle rejection.
		if( !this.#mutations ){
			const ql = `__schema{ mutationType{ name fields{ name args{ name defaultValue type{ name } } } } }`;//was missing a closing brace
			const data:any = await this.query( ql );
			this.#mutations = data.__schema.mutationType.fields;//was data.__schema.fields (always undefined)
		}
		return this.#mutations;
	}

	socketComplete(){ console.log( 'complete' ); this.socketDown( "the server closed the connection" ); }//a clean close strands pending requests exactly like an error does
	//get nextRequestId():RequestId{ return this.#requestId+1; }  why?
	getRequestId():RequestId{ return ++this.#requestId;} #requestId:RequestId=0;

	protected setSocketId( id:number ){
		this.#socketId = id;
		if( !this.#socket )
			return;//sendTransmission is `#socket?.next(...)`, so flushing without a socket would silently destroy the backlog rather than leave it for the next connect
		for( var m of this.backlog )
			this.sendTransmission( m );
		this.backlog.length=0;
	}
	private onMessage( event:MessageEvent ):Uint8Array{
		const m = new Uint8Array( event.data );
		this.processMessage( m );
		return m;
	}

	processCommonMessage( m:any, requestId:RequestId ):boolean{
		let handled = true;
		let c = this._callbacks.get( requestId );
		if( c ){
			//`m.Value` was protobufjs's virtual oneof discriminator - a getter naming the set field.  ts-proto emits the oneof
			//members as plain optionals with no such getter, so `!m.Value` was true for EVERY message and every pending
			//request resolved with null (subscribe's ack among them, which then died on `y.length`).  requestId is the only
			//non-oneof field, so "carries a payload" is "some other key is set" - and it must test `!==undefined`, because
			//ts-proto's decode seeds every optional member as an explicit undefined key.
			const payload = Object.keys(m).find( k=>k!="requestId" && m[k]!==undefined );
			if( !payload ){//bare response (no oneof payload), e.g. the authorization ack
				this._callbacks.delete( requestId );//settled requests must be removed or the map grows for the socket's lifetime
				c.resolve( null );
			}
			else if( m["graphQl"] ){
				this._callbacks.delete( requestId );
				const json = m["graphQl"]["json"];//payload keyed by its field — Object.entries(m)[0][1] assumed decode order
				c.resolve( c.transformInput ? c.transformInput(json) : json );
			}
			else
				handled = false;//payload is for a subclass handler (subscriptionAck etc.) — leave the callback for it
		}
		else
			handled = false;
		return handled;
	}
/*	processCallback( id:number, resolution:any, log:string ){
		if( !this._callbacks.has(id) )
			throw `no callback for:  '${id}'`;
		if( this.log.restResults ) console.log( `(${id})${log}` );
		let p:RequestPromise<ResultMessage> = this._callbacks.get( id );
		p.resolve( resolution );
		this._callbacks.delete( id );
	}
*/
	processError( e:IException, requestId:RequestId ):boolean{
		const handled = this._callbacks.has( requestId );
		if( e.statusCode==401 )
			this.authStore.logout();//same policy as authGet's http 401: the credential is stale - a retry with it can only repeat the 401.
		if( handled ){
			let p:RequestPromise<ResultMessage> = this._callbacks.get( requestId )!;
			p.reject( {error: {requestId:requestId, message:e.what as string, sc:e.code as number, httpStatus:e.statusCode as number}} );
			this._callbacks.delete( requestId );
		}
		return handled;
	}
	protected abstract processMessage( bytearray:Uint8Array ):void;

	protected abstract handleConnectionError( err:unknown ):void;

	protected backlog:Transmission[] = [];
	protected log = { sockRequests:true, sockResults:true, restRequests:true, restResults:false, subRequest:true, subResults:true, maxLength:255 };
	//Informational purposes only to match with server logs.
	protected get socketId():number{ return this.#socketId; } #socketId!:number;
	get instances(){return this.#instances;} set instances(x){
		this.#instances = x.map( instance=>resolveInstance(instance) );//an empty or loopback host is the page's - resolveInstanceHost (install-issues #2)
		for( let callback of this.#initCallbacks ){
			if( x.length )
				callback.resolve();
			else
				callback.reject( {error:{sc:0,message:"no server instances found."}} );
		}
	} #instances!:Instance[];
	#initCallbacks:{resolve:()=>void, reject:Reject}[]=[];
	#loginCallbacks:{target:string, resolve:(result:any)=>void, reject:( e:unknown )=>void, log:Log}[]=[];

	//abstract get queryId():number;
	#socket:WebSocketSubject<Uint8Array>|undefined;
	protected _callbacks = new Map<number, RequestPromise<ResultMessage>>();
	#tables = new Map<string,TableSchema>();
	#mutations!:Array<MutationSchema>;//per-instance — a static cache shared one schema across AppService/AccessService/Gateway (different endpoints)
	private get url(){
		if( !this.instances?.length ) throw "no instances";
		return `${this.instances[0].host}:${this.instances[0].port}`;
	}
	//the websocket's request path: "" for the app server (its socket is the root), a service whose protocol a host routes by path overrides it (Gateway: "/opc" on an OpcHub; the standalone gateway ignores it).
	protected get socketPath():string{ return ""; }
	protected get socketUrl(){ return `${this.transport==ETransport.Secure ? "wss" : "ws"}://${this.url}${this.socketPath}`; }
	private get restUrl(){return this.transport==ETransport.Secure ? this.secureRestUrl : `http://${this.url}`;}
	private get secureRestUrl(){return `https://${this.url}`;}

	isLoggedIn = computed( () => { const u = this.user(); return !!(u && (u.jwt || u.id)); } );//not `!=null`: logout() keeps a serverInstances-only User, and anonymous REST gets a sessionId — only jwt (Google) or id (password/OpcServer) means logged in
	get user():Signal<User|undefined>{return this.authStore.user; }
	lastRestCall!:Date;
	timeoutSeconds!:number;
}