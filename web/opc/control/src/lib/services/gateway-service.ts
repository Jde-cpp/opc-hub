import { Injectable, InjectionToken, inject } from '@angular/core';
import { Router } from '@angular/router';
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Subject,Observable, finalize } from 'rxjs';
import { AppService, AUTH_STORE, AuthStore, Duration, ETransport, GoogleAuthService, Guid, IGraphQL, Instance, Log, Mutation, MutationSchema, ProtoService, ProtoUtils, Query, StringUtils, TableSchema, Timestamp, Type } from 'jde-framework';
import { EProvider, User } from 'jde-spa';
import { errorText } from 'jde-framework';


import { OpcError } from '../model/opc-error';

import * as Common from 'jde-proto/Opc.Common';
import * as FromClient from 'jde-proto/Opc.FromClient';
import * as FromServer from 'jde-proto/Opc.FromServer';
import { OPC_STORE, OpcStore } from './opc-store';
import { CnnctnSlug } from "../model/server-cnnctn";
import { NodeKey, NodeId, NodeIdentifier } from '../model/node-id';
import { ENodeClass, ObjectType, OpcObject, UaNode, Variable } from '../model/node';
import { OpcId, scBadUnexpectedError, StatusCode } from '../model/types';
import { ExNodeId } from '../model/ex-node-id';
import { Reading, toReading, Value, valueJson } from '../model/value';
import { Enum } from '../model/enum';

interface IError{ requestId:number; message: string; }
type Owner = any;

export type GatewaySlug = string;
//app.routes.ts: 'gateways/:gateway[/:connection/**]' and 'apps/gateways/:instance[/:connection]'.  Both segments hold a
//Gateway.slug (== instances[0].instanceName), so one pattern covers the lot.  Module scope, not a static #field:  a
//decorated class cannot carry a static private identifier (TS18036).
const gatewayUrl = /^\/(?:apps\/)?gateways\/([^/?#]+)/;
@Injectable( {providedIn: 'root'} )
export class GatewayService implements IGraphQL{
	private router = inject( Router );
	constructor(){
		this.#lookup();
	}
	//After a failed lookup nothing settled a later gateway()/gateways() - /gateways awaited forever and a Retry could not help
	//(reviews/m3-closing.md #7) - so a call made once one has failed looks the instances up again.
	#lookup(){
		this.#lookingUp = true;
		this.appService.gatewayInstances().then(
			(instances)=>{ this.#lookingUp = false; this.onGatewaySuccess( instances, this.appService.transport, this.http, this.#authStore, this.#opcStore ); },
			(e)=>{ this.#lookingUp = false; this.onInstancesError( e ); }//wrap so `this` is bound (bare method reference would run with this===undefined)
		);
	}
	#authStore:AuthStore = inject( AUTH_STORE );
	#opcStore:OpcStore = inject( OPC_STORE );
	#lookingUp = false;
	private onGatewaySuccess(gateways:Instance[], transport:ETransport, http: HttpClient, authStore:AuthStore, opcStore:OpcStore){
		if( gateways.length==0 )
			console.error("No IotServies running");
		this.#gateways = gateways.map( instance=>new Gateway(instance, transport, http, authStore, opcStore, this.googleAuth) );
		this.#gatewaysCallbacks.forEach( cb=>cb.resolve(this.#gateways) );
		this.#gatewaysCallbacks = [];
		this.#gatewayCallbacks.forEach( cb=>{
			const gateway = this.#gateways.find( gateway=>gateway.instances[0].instanceName==cb.instanceName );
			if( gateway )
				cb.resolve( gateway );
			else
				cb.reject( this.#unknownGateway(cb.instanceName) );
		});
		this.#gatewayCallbacks = [];
	}
	onInstancesError(e:HttpErrorResponse){
		console.error( `Could not get IotServices.  (${e.status})${e.message}` );
		this.#gatewaysCallbacks.forEach( x=>x.reject(e) );//reject BOTH lists so gateways() awaiters don't hang, not just gateway()
		this.#gatewaysCallbacks = [];
		this.#gatewayCallbacks.forEach( x=>x.reject(e) );
		this.#gatewayCallbacks = [];
	}
	//A url segment naming a gateway that is not registered - a stale bookmark, a renamed instance - used to come back as
	//`undefined` behind a `!`, so the miss only surfaced as "cannot read properties of undefined" inside the resolver's
	//first query, with nothing naming the gateway.  Reject, naming it:  a resolver route's caller says so (GatewayResolver's
	//snackbar), and so does a Cards page (its could-not-load state) - a bare rejection reached neither (reviews/m3-closing.md #7).
	async gateway( instanceName:string ):Promise<Gateway>{
		if( !this.#gateways ){
			const queued = new Promise<Gateway>( (resolve,reject)=>this.#gatewayCallbacks.push({instanceName, resolve, reject}) );
			if( !this.#lookingUp )
				this.#lookup();
			return queued;
		}
		const gateway = this.#gateways.find( gateway=>gateway.instances[0].instanceName==instanceName );
		if( !gateway )
			throw this.#unknownGateway( instanceName );
		return gateway;
	}
	#unknownGateway( instanceName:string ):Error{
		const known = this.#gateways?.map( gateway=>gateway.instances[0].instanceName ) ?? [];
		return new Error( `No gateway '${instanceName}' is registered.  ${known.length ? `Registered: '${known.join("', '")}'.` : "None are registered."}` );
	}
	async gateways():Promise<Gateway[]>{
		if( !this.#gateways ){
			const queued = new Promise<Gateway[]>( (resolve,reject)=>this.#gatewaysCallbacks.push({resolve:resolve,reject:reject}) );
			if( !this.#lookingUp )
				this.#lookup();
			return queued;
		}
		return Promise.resolve( this.#gateways );
	}
	//Every IGraphQL entry point goes through defaultGatewayAsync, not the synchronous getter: until gatewayInstances()
	//resolves there is no Gateway to hand back, and the getter used to return undefined so the caller died on
	//"cannot read properties of undefined" - even though gateway()/gateways() already queue for exactly this window.
	//These are all async, so waiting instead of throwing costs the caller nothing.
	async ql<Y>( q:Query, log:Log ):Promise<Y>{ return (await this.defaultGatewayAsync()).ql( q, log ); }
	async query<T>( ql: string, args?:any, log?:Log ):Promise<T>{ return (await this.defaultGatewayAsync()).query<T>(ql, args, log); }
	async querySingle<T>( ql: string ):Promise<T>{ return (await this.defaultGatewayAsync()).querySingle<T>( ql ); }
	async schema( names:string[] ):Promise<TableSchema[]>{ return (await this.defaultGatewayAsync()).schema( names ); }
	async schemaWithEnums( type:string, log:Log ):Promise<TableSchema>{ return (await this.defaultGatewayAsync()).schemaWithEnums( type, log ); }
	async mutate<T>( ql: string|Mutation|Mutation[], log?:Log ):Promise<T>{
		return (await this.defaultGatewayAsync()).mutate<T>( ql, log );
	}
	async mutations():Promise<MutationSchema[]>{ return (await this.defaultGatewayAsync()).mutations(); }

	slugQuery( schema:TableSchema, slug: string, showDeleted:boolean ):string{ return null as any; }
	subQueries( typeName: string, id: number ):string[]{ return []; }
	excludedColumns( tableName:string ):string[]{ return []; }
	toCollectionName( collectionDisplay:string ):string{ return collectionDisplay; }

	//Same resolution as the getter, but waits for the instance lookup instead of failing during it.  Prefer this everywhere
	//an await is already in hand; the getter stays for the synchronous callers.
	async defaultGatewayAsync():Promise<Gateway>{
		if( !this.#gateways )
			await this.gateways();//queues on #gatewaysCallbacks, and rejects if gatewayInstances() failed
		return this.defaultGateway;
	}
	//Resolved from the CURRENT url on every read, never cached.  This service is providedIn:'root', so its own ActivatedRoute
	//is the ROOT route and its paramMap never carries the child ':gateway' - the old subscription therefore fired with nothing,
	//#defaultGateway kept whatever the first url-suffix guess produced, and every IGraphQL call made on gateway B (reload,
	//delete<Type>(id), restore, OpcAuthService.logout) went to gateway A against B's row ids.  Masked only by single-gateway
	//deployments, where the find-miss and the [0] fallback happen to name the same instance.
	#urlGateway():Gateway|undefined{
		const navigation = this.router.getCurrentNavigation();//guards/resolvers run BEFORE activation, so routerState still holds the route being left
		const url = navigation ? this.router.serializeUrl( navigation.finalUrl ?? navigation.extractedUrl ) : this.router.url;
		const slug = gatewayUrl.exec( url )?.[1];
		return slug ? this.#gateways?.find( gateway=>gateway.slug==decodeURIComponent(slug) ) : undefined;
	}
	get defaultGateway():Gateway{
		//?? gateways[0]: 'default' means "when nothing names one" - loginPassword already treats gateways[0] as the one to use.
		const gateway = this.#urlGateway() ?? this.#gateways?.[0];
		if( !gateway )//was `undefined!`, so callers died on "cannot read properties of undefined" far from the cause
			throw new Error( "No gateway is available: the gatewayInstances() lookup has not resolved yet, or returned none.  Await defaultGatewayAsync() instead." );
		return gateway;
	}
	#gateways!:Gateway[];

	#gatewaysCallbacks:{resolve: (value:Gateway[])=>void, reject:(e?:unknown)=>void}[]= [];
	#gatewayCallbacks:{ instanceName:string, resolve: (value:Gateway)=>void, reject:(e?:unknown)=>void}[]= [];
	appService = inject(AppService);
	googleAuth = inject(GoogleAuthService);//Gateway is built by hand below, so the silent-renewal service is threaded through rather than injected there
	http = inject(HttpClient);
}


export class Gateway extends ProtoService<FromClient.Transmission,FromServer.Message>{
	//an OpcHub serves the app-server and gateway protocols from one port and tells them apart by the upgrade path; a standalone gateway ignores it.
	protected override get socketPath(){ return "/opc"; }
	constructor( gateway:Instance, transport:ETransport, http: HttpClient, authStore:AuthStore, private store:OpcStore, googleAuth?:GoogleAuthService ){
		super( FromClient.Transmission, http, transport, authStore, false, googleAuth );
		super.instances = [gateway];
		if( typeof location!="undefined" && gateway.host!=location.hostname )//a loopback advertisement was already rewritten to the page's host (resolveInstance); what remains is a split deployment's own name, and a page served from another host fails the server's allowOrigin 'sameHost' check
			console.warn( `Gateway '${gateway.instanceName}' is registered at host '${gateway.host}' but the app is served from '${location.hostname}' - requests will be CORS-blocked unless http/accessControl/allowOrigin is pinned or the app is browsed via '${gateway.host}'.` );
		//No connection warm-up here.  The navbar's search box injects SEARCH_PROVIDERS, which instantiates NodeSearchProvider,
		//which injects GATEWAY_SERVICE - so a Gateway is built on EVERY page, and a `serverConnections{…}` in this constructor
		//queried the gateway's whole connection list at bootstrap on /access/resources, /help, everywhere.  The list it cached
		//had exactly one reader, a setRoute() nobody called.  OpcStore.getConnection is the real cache: one connection, on
		//demand, memoized per gateway - which is what NodeResolver already uses.
	}
	async login( domain:string, username:string, password:string, log:Log ):Promise<void>{
		let self = this;
		if( this.log.restRequests )	console.log( `Login( opc='${domain}', username='${username}' )` );
		try{
			await this.logout( log );
			console.assert( !this.user()?.authorization );
			await this.postRaw<any>( 'login', {opc:domain, user:username, password:password}, true, null );
			if( this.log.restResults ) console.log( `authorization: '${this.user()?.authorization}'` );
			let user = new User();
			user.id = domain ? `${domain}\\${username}` : username;
			user.domain = domain;
			user.name = username;
			user.provider = EProvider.OpcServer;
			this.authStore.append( user );
		}
		catch( e ){
			throw e;
		}
	}

	async logout( log:Log ):Promise<void>{
		let self = this;
		if( this.log.restRequests )	console.log( `logout()` );
		try{
			//AppService.logout's header:  options of the caller's own attach no session, and a logout without one ended a session
			//made for the request while the signed-in one lived on (reviews/install-issues.md #54)
			await this.postRaw<string>( 'logout', {}, false, {observe: "response", headers:{"Authorization":this.user()?.authorization}} );
			if( this.log.restResults ) console.log( `logout` );
		}
		catch( e:unknown ){
			log( `logout failed:  ${errorText(e) ?? "Unknown error"}` );
		}
		this.authStore.logout();
	}

	//the socket carried every subscription, so the server has already forgotten them: drop the local bookkeeping directly rather than via clearOwner, whose unsubscribe sends would re-open the socket just to cancel subscriptions that no longer exist.
	protected handleConnectionError(){
		const e = { message: "Connection to the gateway was lost." };
		const subjects = [...this.#ownerSubscriptions.values()];
		this.#ownerSubscriptions.clear();//clear before error() so a later addToSubscription starts a fresh Subject, same as the _subscribe failure path
		this.#subscriptions.clear();
		this.#nodes.clear();
		subjects.forEach( s=>s.error(e) );
	};
	protected processMessage( buffer:Uint8Array ){
		try{
			const transmission = FromServer.Transmission.decode( buffer );
			for( const message of <FromServer.Message[]>transmission.messages ){
				let requestId = message.requestId;
				if( super.processCommonMessage(message, requestId) )
					continue;
				if( message.ack ){
					console.log( `[App.${requestId}]Connected to '${super.socketUrl}', socketId: ${message.ack}` );
					let socketId = message.ack;
					if( this.user()?.authorization )
						super.sendAuthorization( socketId );
					else{
						console.warn( `no authorization` );
						this.setSocketId( socketId );
					}
				}
				else if( message.nodeValues )
					this.nodeValues( message.nodeValues );
				else if( message.subscriptionAck )
					this.subscriptionAck( requestId, message.subscriptionAck );
				else if( message.unsubscribeAck )
					this.onUnsubscriptionResult( requestId, message.unsubscribeAck );
				else if( message.exception ){
					const e = message.exception;
					if( !this.processError( e, requestId ) )
						throw e;
				}
				else
					throw `unknown message:  ${JSON.stringify( message )}`;
			}
		}
		catch( e ){
			if( typeof e=="string" )
				console.error( e );
			else
				console.error( e );
		}
	}
	//protobufjs exposed a virtual `Identifier` getter naming the set oneof field; ts-proto emits the fields as plain
	//optionals, so test for undefined rather than truthiness - id 0 and "" are legitimate values, not absence.
	private static toIdentifier( proto:Common.NodeId ):NodeIdentifier|undefined{
		return proto.numeric!=undefined ? proto.numeric
			: proto.string!=undefined ? proto.string
			: proto.byteString!=undefined ? proto.byteString
			: proto.guid!=undefined ? Gateway.toGuid( proto.guid ) : undefined;
	}
	//The id goes in WITH the namespace.  Both of these built the node from its namespace alone and assigned the id after, and
	//a NodeId with no identifier is what the constructor reports - so every subscription push (one a second, per node) logged
	//"NodeId - unrecognized json" to the console for a node that was fine.  A proto with no identifier still says so.
	private static toNode( proto:Common.NodeId ):NodeId{
		return new NodeId( {ns: proto.namespaceIndex, id: Gateway.toIdentifier(proto)} );
	}
	private static toExpanded( proto:Common.ExpandedNodeId ):ExNodeId{
		return new ExNodeId( {ns: proto.node!.namespaceIndex, id: Gateway.toIdentifier(proto.node!), nsu: proto.namespaceUri!, serverIndex: proto.serverIndex!} );
	}

	private static toProto( nodes:NodeId[] ):Common.NodeId[]{
		let protoNodes = [];
		for( const node of nodes ){
			//Subscribe/Unsubscribe.nodes are plain Proto.NodeId — the previous ExpandedNodeId{node:…} wrapper encoded as an EMPTY node (fields read off the wrapper), so every subscribe failed with BadNodeIdUnknown
			let proto = Common.NodeId.create();//ts-proto codec object, not a constructor
			proto.namespaceIndex = node.ns;
			if( typeof node.id === "number" )
				proto.numeric = node.id;
			else if( typeof node.id === "string" )
				proto.string = node.id;
			else if( node.id instanceof Guid )
				proto.guid = node.id.value;
			else if( node.id instanceof Uint8Array )
				proto.byteString = node.id;
			protoNodes.push( proto );
		}
		return protoNodes;
	}

	//Fetches the names of the codes seen so far.  Public:  a status that arrives on the socket has no REST call behind it to
	//bring its name, so the table asks.  One request at a time - a fault holds its code on every push, and each of those
	//would otherwise re-request a name that is already on its way.  Never rejects:  most callers don't await it, and a name
	//is a nicety - the screen falls back to the severity and the code.
	updateErrorCodes():Promise<void>{
		return this.#errorCodes ??= this.#fetchErrorCodes().finally( ()=>this.#errorCodes = undefined );
	}
	#errorCodes:Promise<void>|undefined;
	async #fetchErrorCodes():Promise<void>{
		const asked = new Set<StatusCode>();//a code the server has no row for stays pending, and must not be asked for in a loop
		for( let scs = OpcError.emptyMessages(); scs.length; scs = OpcError.emptyMessages().filter(sc=>!asked.has(sc)) ){//round again for a code that turned up while the request was out - its caller was handed this same promise
			scs.forEach( sc=>asked.add(sc) );
			try{
				const json = await this.get( `ErrorCodes?scs=${scs.join(',')}` ) as any;//this., not super.:  the same method, and one a spec can stand in for
				OpcError.setMessages( json["errorCodes"] );
			}
			catch( e ){
				return console.warn( `Could not fetch the status code names - ${errorText(e)}` );
			}
		}
	}
	async errorCodeText( sc:StatusCode ):Promise<string>{
		let text = OpcError.statusCodeText( sc );
		if( !text ){
			await this.updateErrorCodes();
			text = OpcError.statusCodeText( sc );
		}
		return `(${sc.toString(16)})${text}`;
	}

	public async browseObjectsFolder( cnnctn:CnnctnSlug, parent:UaNode, snapshot:boolean, log:Log ):Promise<UaNode[]>{
		if( parent.isVariable )
			throw new EvalError( `Cannot browse children of variable node.`, {cause:"Invalid Operation"} );
		const vars = { opc: cnnctn, id: parent.nodeId.toJson() };
		const commonColumns = "id name browse nodeClass refType typeDef description";
		const variableColumns = "dataType value valueRank accessLevel userAccessLevel";
		const ql = `node(opc:$opc, id:$id){children{${commonColumns} ... on Variable{${variableColumns}} }}`;
		const children = (await this.query<any>( ql, vars, (m)=>console.log(m) ))["node"]["children"];
		var y = new Array<UaNode>();
		for( const ref of children ){
			let child:UaNode|undefined;
			switch( <ENodeClass>ref.nodeClass ){
				case ENodeClass.Object: child = new OpcObject(ref, parent); break;
				case ENodeClass.ObjectType: parent.typeDef = new ObjectType(ref); break; //y.push( new ObjectType(ref) ); break;
				case ENodeClass.Variable:
					let variable = new Variable(ref, parent);
					child = variable;
					if( variable.customDataType )
						try{
							const nodeId = <NodeId>variable.customDataType
							let x = await super.querySingle<Type>( `__type( opc: ${StringUtils.qlString(cnnctn)}, ${nodeId.qlArgs()}){ name enumValues{id name description}}`, null, log );
							variable.customDataType = new Enum(nodeId, x);
						}
						catch( e:unknown ){
							log( errorText(e) ?? "Unknown error" );
						}
					break;
				default: console.error( `browseObjectsFolder - unhandled nodeClass ${ref.nodeClass} for '${ref.name}'` );//was `debugger;` — froze the app whenever a debugger (DevTools/automation) was attached
			}
			if( child )
				y.push( child );
		}
		this.store.setNodes( this.slug, cnnctn, parent, y );
		await this.updateErrorCodes();//awaited:  the rows are plain objects, so a name that lands after the first paint repaints nothing
		return y;
	}
	async snapshot( opcId:CnnctnSlug, nodes:NodeId[] ):Promise<Map<NodeId,Reading>>{
		const results = await super.queryArray<{id:NodeId,value:any}>( `nodes( opc: ${StringUtils.qlString(opcId)}, id:[${NodeId.qlArgsArray(nodes)}]){id value}` );
		var y = new Map<NodeId,Reading>();
		for( const snapshot of results )
			y.set( new NodeId(snapshot.id), toReading(snapshot.value) );
		this.updateErrorCodes();
		return y;
	}
	//read and write answer with the Reading - toValue() alone dropped the quality, so a refresh or a write's echo quietly turned an Uncertain row Good
	async read( opcId:CnnctnSlug, n:NodeId ):Promise<Reading>{
		//`id` has to be an OBJECT argument: NodeId::ParseQL (libs/opc/src/uatypes/NodeId.cpp) reads FindPtr<jvalue>("id")
		//and accepts only an object or an array of them, so the flat `ns:…,i:…` qlArgs() form matched nothing, the server
		//answered {"node":{}}, and read() returned undefined for every node - silently blanking the cell on changeDouble's
		//failed-write restore.  snapshot() below already passes id:[{…}] and works, and write() already uses $id; this is
		//the same shape via variables, which also avoids hand-escaping the literal.
		const v = await super.querySingle<{value:any}>( `node( opc: $opc, id: $id ){value}`, {opc: opcId, id: n.toJson()} );
		return toReading( v["value"] );
	}
	async write( opcId:CnnctnSlug, n:NodeId, v:Value, log:Log ):Promise<Reading>{
		const q = `updateVariable( opc: $opc, id: $id, value: $value ){ value }`;
		const vars = { opc: opcId, id: n.toJson(), value: valueJson(v) };
		const data:any = await super.postQL<any>( q, vars, log );
		this.updateErrorCodes();
		//unwrap first, toValue last - mirroring read().  postQL returns the `data` object and the server keys a mutation payload by command name (QLAwait: `result[commandName]`, skipped only for `raw`, which this never requests), so toValue used to run on the wrapper and `["value"]` off its result was always undefined - blanking the cell.  `?? data` keeps the raw/unkeyed shape working too.
		return toReading( (data?.["updateVariable"] ?? data)?.["value"] );
	}

	private onUnsubscriptionResult( requestId:number, result:FromServer.UnsubscribeAck ){
		result.failures?.forEach( (node)=>console.log(`unsubscribe failed for:  ${JSON.stringify(node)}`) );
		const c = this._callbacks.get( requestId );
		if( c ){
			this._callbacks.delete( requestId );//settled requests must be removed or the map grows for the socket's lifetime
			c.resolve( null );
		}
	}

	private subscriptionAck( requestId:number, ack:FromServer.SubscriptionAck ){
		const c = this._callbacks.get( requestId );
		if( c ){
			this._callbacks.delete( requestId );
			c.resolve( ack.results );
		}
	}

	//A failed subscribe costs the caller only the nodes it asked for.  Two ways it used to cost more:  the per-node failure
	//path `delete`d the whole per-node-key entry, taking every OTHER owner's registration with it - the server kept pushing
	//and nodeValues' `opcSubscriptions.get(node.key)?.forEach` silently no-oped - and the catch called clearOwner, which
	//unsubscribed the owner's already-live nodes server-side and errored the shared Subject, ending subscriptions that had
	//nothing to do with the request.  Now only this owner's registration for the FAILED nodes goes, and the failure is
	//reported the way the server reports a bad reading: an OpcError value on the owner's stream, which stays open.
	private async _subscribe( opcId:OpcId, nodes:NodeId[], owner:Owner ):Promise<void>{
		const request:FromClient.Subscribe = { nodes:Gateway.toProto(nodes), opcId:opcId };
		const failed = new Array<{node:NodeId, sc:StatusCode}>();
		try{
			const y = await this.sendPromise<FromServer.MonitoredItemCreateResult[]>( {"subscribe":request}, `subscribe opcId: ${opcId}, nodeCount: ${nodes.length}` );
			//Matched on the node each result names:  the gateway answers in NodeId order, never the request's, so blaming results[i]
			//on nodes[i] marked a healthy row Bad - locked, its pushes dropped - and left the node that failed ticked with nothing
			//said (reviews/m3-closing.md #10).  A result naming no node (a gateway older than the field) can be placed only when
			//there is one node to place it on;  otherwise nothing is blamed rather than the wrong row.
			const asked = new Map( nodes.map(node=>[node.key, node]) );
			const single = y.length==1 && nodes.length==1 ? nodes[0] : undefined;
			for( const result of y ){
				if( !result.statusCode )
					continue;
				const node = result.node ? asked.get( Gateway.toNode(result.node).key ) : single;
				if( node )
					failed.push( {node, sc: result.statusCode} );
				else
					console.warn( `subscribe opcId: ${opcId} - a failed result (${result.statusCode.toString(16)}) ${result.node ? `names a node that was not asked for: ${JSON.stringify(result.node)}` : "names no node"} - left unplaced.` );
			}
		}
		catch( e:unknown ){
			console.error( `subscribe failed - ${errorText(e)}` );
			const sc = <StatusCode>((<{error?:{sc?:number}}>e)?.error?.sc ?? scBadUnexpectedError);
			failed.push( ...nodes.map( node=>({node, sc}) ) );//the request never reached the server, so none of THESE is subscribed - the owner's others still are
		}
		if( !failed.length )
			return;
		const opcSubscriptions = this.getOpcSubscriptions( opcId );
		const subject = this.#ownerSubscriptions.get( owner );
		for( const {node, sc} of failed ){
			this.clearOwnerNode( opcSubscriptions, node.key, owner );//drops the key only when no owner is left on it
			const e = new OpcError( sc, "Subscribe", new Error().stack!, undefined );
			console.log( `Subscription failed for '${node}' - ${e}` );
			subject?.next( {opcId, node, value: e, sc} );
		}
		this.clearUnusedNodes();//nothing was subscribed server-side, so there is nothing to unsubscribe - only the bookkeeping
	}

	#ownerSubscriptions = new Map<Owner,Subject<SubscriptionResult>>();
	#subscriptions = new Map<OpcId,Map<NodeKey, Owner[]>>();
	getOpcSubscriptions(opcId:OpcId):Map<NodeKey, Owner[]>{ return this.#subscriptions.has( opcId ) ? this.#subscriptions.get( opcId )! : this.#subscriptions.set( opcId, new Map<NodeKey, Owner[]> ).get( opcId )!; }
	#nodes = new Map<NodeKey, NodeId>();
	private clearUnusedNodes(){
		this.#nodes.forEach( (_, key)=>{
			const keys = [...this.#subscriptions.entries()].filter( ({1:value})=>value.has(key) ).map( ([key])=>key );
			if( !keys.length )
				this.#nodes.delete(key);
		});
	}
	public addToSubscription( opcId:OpcId, nodes:NodeId[], owner:Owner ){
		let opcSubscriptions = this.getOpcSubscriptions( opcId );
		for( const node of nodes ){
			if( opcSubscriptions.has(node.key) ){
				const owners = opcSubscriptions.get( node.key )!;//the outer `let owners:Owner[]` this shadowed was never read - declared, shadowed, dropped
				if( !owners.includes(owner) )
					owners.push( owner );
			}
			else{
				opcSubscriptions.set( node.key, [owner] );
				this.#nodes.set( node.key, node );
			}
		}
		if( !this.#ownerSubscriptions.has(owner) )
			this.#ownerSubscriptions.set( owner, new Subject<SubscriptionResult>() );
		this._subscribe( opcId, nodes, owner );
	}
	subscribe( opcId:OpcId, nodes:NodeId[], owner:Owner ):Observable<SubscriptionResult>{
		this.addToSubscription( opcId, nodes, owner );
		let subject = this.#ownerSubscriptions.get( owner )!;
		return subject.pipe(
			finalize(() => {//https://stackoverflow.com/questions/62579473/detect-when-a-subject-has-no-more-subscriptions
				if( !subject.observers.length ) {
					this.clearOwner( owner );
				}
			}));
	}

	private static toGuid( proto:Uint8Array ):Guid{ let guid = new Guid(); guid.value = proto; return guid; }
	private static toValue( proto:FromServer.Value ):Value{
		//protobufjs exposed a virtual `of` getter naming the set oneof field; ts-proto emits plain optionals, so each arm
		//tests for undefined rather than truthiness - false, 0 and "" are values, not absence.
		if( proto.boolean!=undefined )      return proto.boolean;
		if( proto.byte!=undefined )         return proto.byte;
		if( proto.byteString!=undefined )   return proto.byteString;
		if( proto.date!=undefined )         return ProtoUtils.fromDate( proto.date )!;//ts-proto decodes google.protobuf.Timestamp to a Date; normalise to the {seconds,nanos} shape the rest of the Value pipeline (GraphQL JSON) uses, so there is one representation
		if( proto.doubleValue!=undefined )  return proto.doubleValue;
		if( proto.duration!=undefined )     return <Duration>proto.duration;
		if( proto.expandedNode!=undefined ) return Gateway.toExpanded( proto.expandedNode );
		if( proto.floatValue!=undefined )   return proto.floatValue;
		if( proto.guid!=undefined )         return Gateway.toGuid( proto.guid );
		if( proto.int16!=undefined )        return proto.int16;
		if( proto.int32!=undefined )        return proto.int32;
		if( proto.int64!=undefined )        return proto.int64;
		if( proto.node!=undefined )         return Gateway.toNode( proto.node );
		if( proto.sbyte!=undefined )        return proto.sbyte;
		if( proto.statusCode!=undefined )   return proto.statusCode;
		if( proto.stringValue!=undefined )  return proto.stringValue;
		if( proto.uint16!=undefined )       return proto.uint16;
		if( proto.uint32!=undefined )       return proto.uint32;
		if( proto.uint64!=undefined )       return proto.uint64;
		if( proto.xmlElement!=undefined )   return proto.xmlElement;
		return undefined!;
	}
	private static toValues( proto:FromServer.Value[] ):Value{
		let value = proto.length==1 ? Gateway.toValue( proto[0] ) : new Array<Value>();
		if( proto.length>1 )
			proto.forEach( v => (<Value[]>value).push( Gateway.toValue(v) ) );
		return value;
	}

	private nodeValues( nodeValues:FromServer.NodeValues ):void{
		let opcSubscriptions = this.#subscriptions.get( nodeValues.opcId! ); if( !opcSubscriptions ){ return console.error(`Could not find opc ${nodeValues.opcId}`);}
		const node = Gateway.toNode( nodeValues.node! );
		const sc = nodeValues.sc ?? 0;//proto3 omits 0/Good from the wire.
		//A Bad push still carries the reading the server holds.  Swapping it for an OpcError here destroyed it, and the table
		//bound the error into its editors;  sc says what the reading is worth.  No values = none was sent - toValues([]) is
		//an empty array, which a row would take for an array reading.
		const value = nodeValues.values?.length ? Gateway.toValues( nodeValues.values ) : undefined;
		opcSubscriptions.get( node.key )?.forEach( owner=>this.#ownerSubscriptions.get(owner)!.next({opcId:nodeValues.opcId!, node:node, value:value, sc:sc}) );
	};

	private clearOwnerNode( opcSubscriptions:Map<NodeKey, Owner[]>,  key:NodeKey, owner:Owner ){
		let owners = opcSubscriptions.get( key );
		if( !owners )
			return false;//node not tracked — nothing to remove, not a tombstone
		const index = owners.indexOf( owner );
		let tombStone = false;
		if( index!=-1 ){
			owners.splice( index, 1 );
			tombStone = !owners.length
			if( tombStone )
				opcSubscriptions.delete( key );
		}
		return tombStone;
	}
	// remove all subscriptions for owner.
	private clearOwner( owner:Owner ){
		this.#ownerSubscriptions.delete( owner );
		let toDeleteKeys = new Map<OpcId,NodeKey[]>();
		for( const [opcId, opcSubscriptions] of this.#subscriptions.entries() ){
			for( const nodeKey of opcSubscriptions.keys() ){
				if( this.clearOwnerNode(opcSubscriptions, nodeKey, owner) )
				toDeleteKeys.has(opcId) ? toDeleteKeys.get(opcId)!.push(nodeKey) : toDeleteKeys.set( opcId, [nodeKey] );
			}
		}
		let toDeleteNodes = new Map<OpcId,NodeId[]>();
		for( const [opcId,keys] of toDeleteKeys ){
			let nodes = toDeleteNodes.set( opcId, [] ).get( opcId )!;
			keys.forEach( key=>nodes.push( this.#nodes.get(key)! ) );
		}
		if( toDeleteNodes.size ){
			for( const [opcId, nodes] of toDeleteNodes ){
				var request:FromClient.Unsubscribe = { nodes:Gateway.toProto(nodes), opcId:opcId };
				this.sendPromise<void>( {"unsubscribe": request}, `unsubscribe opcId: ${opcId}, nodeCount: ${nodes.length}` );
			}
		}
		this.clearUnusedNodes();
	}
	// Unsuscribe, but keep subscription open.
	async unsubscribe( opcId:string, nodes:NodeId[], owner:Owner ):Promise<void>{
		let opcSubscriptions = this.#subscriptions.get( opcId );
		if( !opcSubscriptions )
			return;//opc not tracked — nothing to unsubscribe
		let toDelete = new Array<NodeId>();
		for( const node of nodes ){
			if( this.clearOwnerNode( opcSubscriptions, node.key, owner ) )
				toDelete.push( node );
		}

		this.clearUnusedNodes();
		if( toDelete.length ){
			var request:FromClient.Unsubscribe = { nodes:Gateway.toProto(toDelete), opcId:opcId };
			return this.sendPromise<void>( {"unsubscribe": request}, `unsubscribe opcId: ${opcId}, nodeCount: ${toDelete.length}` );
		}
		else
			return Promise.resolve();
	}
	get name():string{ return this.instances[0].instanceName!; }
	get slug():GatewaySlug{ return this.instances[0].instanceName!; }
}
export type SubscriptionResult = Reading & {opcId:string, node:NodeId};//a pushed Reading - Bad keeps the value the server holds, and sc says so.  A subscribe that FAILED is the one OpcError `value`:  a refused request, not a reading.
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const GATEWAY_SERVICE = new InjectionToken<GatewayService>( 'GatewayService' );