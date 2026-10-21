import { Injectable, InjectionToken, inject } from '@angular/core';
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Subject,Observable, finalize, Subscriber } from 'rxjs';
import { AppService, AUTH_STORE, AuthStore, Duration, ETransport, GoogleAuthService, Guid, Instance, ProtoService, ProtoUtils, StringUtils, Type } from 'jde-framework';
import { EProvider, User } from 'jde-spa';
import { errorMessage, errorText } from 'jde-framework';


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
import { ExtensionObject, Reading, shortestFloat, toReading, Value, valueJson } from '../model/value';
import { Enum } from '../model/enum';
import { histResumeTime, HistPushedModification, HistPushKind, HistValue, isBoundNotFound } from '../model/hist';

type Owner = any;

export type GatewaySlug = string;
@Injectable( {providedIn: 'root'} )
export class GatewayService{
	constructor(){
		this.gateways().catch( ()=>{} );//looked up at once;  a failure is logged below, and the next call looks again
	}
	#authStore = inject( AUTH_STORE );
	#opcStore = inject( OPC_STORE );
	//The promise is the cache, so concurrent callers share one lookup.  A failed one is dropped, so a later call looks the
	//instances up again - once nothing settled it, /gateways awaited forever and a Retry could not help (reviews/m3-closing.md #7).
	gateways():Promise<Gateway[]>{
		return this.#lookup ??= this.appService.gatewayInstances().then(
			instances=>{
				if( instances.length==0 )
					console.error("No IotServies running");
				return instances.map( instance=>new Gateway(instance, this.appService.transport, this.http, this.#authStore, this.#opcStore, this.googleAuth) );
			} )
			.catch( (e:HttpErrorResponse)=>{
				console.error( `Could not get IotServices.  (${e.status})${e.message}` );
				this.#lookup = undefined;
				throw e;
			} );
	} #lookup:Promise<Gateway[]>|undefined;
	//A url segment naming a gateway that is not registered - a stale bookmark, a renamed instance - used to come back as
	//`undefined` behind a `!`, so the miss only surfaced as "cannot read properties of undefined" inside the resolver's
	//first query, with nothing naming the gateway.  Reject, naming it:  a resolver route's caller says so (GatewayResolver's
	//snackbar), and so does a Cards page (its could-not-load state) - a bare rejection reached neither (reviews/m3-closing.md #7).
	async gateway( instanceName:string ):Promise<Gateway>{
		const gateways = await this.gateways();
		const gateway = gateways.find( gateway=>gateway.instances[0].instanceName==instanceName );
		if( !gateway ){
			const known = gateways.map( gateway=>gateway.instances[0].instanceName );
			throw new Error( `No gateway '${instanceName}' is registered.  ${known.length ? `Registered: '${known.join("', '")}'.` : "None are registered."}` );
		}
		return gateway;
	}
	appService = inject(AppService);
	googleAuth = inject(GoogleAuthService);//Gateway is built by hand below, so the silent-renewal service is threaded through rather than injected there
	http = inject(HttpClient);
}


export class Gateway extends ProtoService<FromClient.Transmission,FromServer.Message>{
	//an OpcHub serves the app-server and gateway protocols from one port and tells them apart by the upgrade path; a standalone gateway ignores it.
	protected override get socketPath(){ return "/opc"; }
	protected override get product(){ return "Gateway"; }
	constructor( gateway:Instance, transport:ETransport, http: HttpClient, authStore:AuthStore, private store:OpcStore, googleAuth?:GoogleAuthService ){
		super( FromClient.Transmission, http, transport, authStore, googleAuth );
		super.instances = [gateway];
		//No connection warm-up here.  The navbar's search box injects SEARCH_PROVIDERS, which instantiates NodeSearchProvider,
		//which injects GATEWAY_SERVICE - so a Gateway is built on EVERY page, and a `serverConnections{…}` in this constructor
		//queried the gateway's whole connection list at bootstrap on /access/resources, /help, everywhere.  The list it cached
		//had exactly one reader, a setRoute() nobody called.  OpcStore.getConnection is the real cache: one connection, on
		//demand, memoized per gateway - which is what NodeResolver already uses.
	}
	async login( domain:string, username:string, password:string ):Promise<void>{
		if( this.log.restRequests )	console.log( `Login( opc='${domain}', username='${username}' )` );
		await this.logout();
		console.assert( !this.user()?.authorization );
		await this.postRaw<any>( 'login', {opc:domain, user:username, password:password} );
		if( this.log.restResults ) console.log( `authorization: '${this.user()?.authorization}'` );
		let user = new User();
		user.id = domain ? `${domain}\\${username}` : username;
		user.domain = domain;
		user.name = username;
		user.provider = EProvider.OpcServer;
		this.authStore.append( user );
	}

	async logout():Promise<void>{
		if( this.log.restRequests )	console.log( `logout()` );
		try{
			//AppService.logout's header:  options of the caller's own attach no session, and a logout without one ended a session
			//made for the request while the signed-in one lived on (reviews/install-issues.md #54)
			await this.postRaw<string>( 'logout', {}, {observe: "response", headers:{"Authorization":this.user()?.authorization}} );
			if( this.log.restResults ) console.log( `logout` );
		}
		catch( e:unknown ){
			console.log( `logout failed:  ${errorMessage(e)}` );
		}
		this.authStore.logout();
	}

	//the socket carried every subscription, so the server has already forgotten them: drop the local bookkeeping directly rather than via clearOwner, whose unsubscribe sends would re-open the socket just to cancel subscriptions that no longer exist.
	protected override handleConnectionError(){
		const e = { message: "Connection to the gateway was lost." };
		const subjects = [...this.#ownerSubscriptions.values()];
		this.#ownerSubscriptions.clear();//clear before error() so a later addToSubscription starts a fresh Subject, same as the _subscribe failure path
		this.#subscriptions.clear();
		this.#nodes.clear();
		subjects.forEach( s=>s.error(e) );
		for( const live of [...this.#hist.values()] )//the socket carried the historian subscriptions too:  each subscribes again once the gateway is back
			this.#histDropped( live );
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
				else if( message.histSubscribeAck )
					this.#histAck( requestId, message.histSubscribeAck );
				else if( message.histSnapshot )
					this.#histSnapshot( requestId, message.histSnapshot );
				else if( message.histRecords )
					this.#histRecords( requestId, message.histRecords );
				else if( message.exception ){
					const e = message.exception;
					if( !this.processError( e, requestId ) && !this.#histException( requestId, e ) )//a live subscription's id has no callback:  its exception ends it
						throw e;
				}
				else
					throw new Error( `unknown message:  ${JSON.stringify( message )}` );
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

	public async browseObjectsFolder( cnnctn:CnnctnSlug, parent:UaNode, snapshot:boolean ):Promise<UaNode[]>{
		if( parent.isVariable )
			throw new EvalError( `Cannot browse children of variable node.`, {cause:"Invalid Operation"} );
		const vars = { opc: cnnctn, id: parent.nodeId.toJson() };
		const commonColumns = "id name browse nodeClass refType typeDef description";
		const variableColumns = "dataType value valueRank accessLevel userAccessLevel historizing";//historizing gates the History tab
		const ql = `node(opc:$opc, id:$id){children{${commonColumns} ... on Variable{${variableColumns}} }}`;
		const children = (await this.query<any>( ql, vars ))["node"]["children"];
		const y = new Array<UaNode>();
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
							let x = await super.querySingle<Type>( `__type( opc: ${StringUtils.qlString(cnnctn)}, ${nodeId.qlArgs()}){ name enumValues{id name description}}` );
							variable.customDataType = new Enum(nodeId, x);
						}
						catch( e:unknown ){
							console.log( errorMessage(e) );
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
	//read and write answer with the Reading - toValue() alone dropped the quality, so a refresh or a write's echo quietly turned an Uncertain row Good
	async read( opcId:CnnctnSlug, n:NodeId ):Promise<Reading>{
		//`id` has to be an OBJECT argument: NodeId::ParseQL (libs/opc/src/uatypes/NodeId.cpp) reads FindPtr<jvalue>("id")
		//and accepts only an object or an array of them, so the flat `ns:…,i:…` qlArgs() form matched nothing, the server
		//answered {"node":{}}, and read() returned undefined for every node - silently blanking the cell on a failed
		//write's restore (NodeChildren.#write).  write() already uses $id;  this is the same shape via variables, which also
		//avoids hand-escaping the literal.
		const v = await super.querySingle<{value:any}>( `node( opc: $opc, id: $id ){value}`, {opc: opcId, id: n.toJson()} );
		return toReading( v["value"] );
	}
	async write( opcId:CnnctnSlug, n:NodeId, v:Value ):Promise<Reading>{
		const q = `updateVariable( opc: $opc, id: $id, value: $value ){ value }`;
		const vars = { opc: opcId, id: n.toJson(), value: valueJson(v) };
		const data:any = await super.postQL<any>( q, vars );
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

	//A historian subscription (spec *Subscriptions*):  HistSubscribe on this socket, whose request id names it on the wire.  The
	//teardown forgets it as it sends HistUnsubscribe, so a page or push already on the wire, and the ack the gateway still sends
	//for one unsubscribed while registering, find no subscription and are dropped:  the ids aren't reused, so one unknown is an
	//ended subscription's (historian-live-trend #10).  The stream delivers the ack, the snapshot's pages, then the records as the
	//historian stores them, each record's node by its slot in `args.nodes`, which the ack's statuses follow too.  A dropped socket
	//drops the subscription on the gateway, so after `dropped` this subscribes again, histRetryDelay later and again on each
	//failure, from where its nodes' records delivered leave off (histResumeTime) - the snapshot then fills the outage, and each
	//node's bound is a value the caller holds - and the ack and pages follow on the stream, for the caller to merge.  An edit made
	//meanwhile to a record before that isn't delivered:  a modification is pushed, and the snapshot is records.  An exception
	//under the id ends it:  the gateway's NotFound for a group removed, ServiceUnavailable for a historian stopped, or a refusal
	//of the HistSubscribe itself.
	histSubscribe( args:HistSubscribeArgs ):Observable<HistEvent>{
		return new Observable<HistEvent>( subscriber=>{
			const live:HistLive = { args, subscriber, requestId: 0, down: false, latest: new Map() };
			this.#histSend( live, args.from );
			return ()=>{
				clearTimeout( live.retry );
				if( this.#hist.delete( live.requestId ) )//still the gateway's:  not ended by it, nor waiting out a drop
					this.sendPromise<void>( {histUnsubscribe: <FromClient.HistUnsubscribe>{subscription: live.requestId}}, `histUnsubscribe ${live.requestId}` ).catch( e=>console.warn( `histUnsubscribe ${live.requestId} - ${errorText(e)}` ) );
			};
		} );
	}
	#histSend( live:HistLive, from:Date|undefined ){
		const requestId = this.getRequestId();
		live.requestId = requestId;
		live.retry = undefined;
		this.#hist.set( requestId, live );
		const request:FromClient.HistSubscribe = { group: live.args.group, nodes: live.args.nodes.map( n=>Gateway.toExProto(n) ), fromTime: from };
		this.sendWithId( {histSubscribe: request}, requestId, `histSubscribe group: ${live.args.group}, nodeCount: ${live.args.nodes.length}, from: ${from?.toISOString() ?? "now"}` );
	}
	#histAck( requestId:number, ack:FromServer.HistSubscribeAck ){
		const live = this.#hist.get( requestId );
		if( !live )
			return;
		live.down = false;
		live.subscriber.next( {kind: 'ack', statuses: ack.statuses.map( sc=>sc>>>0 )} );
	}
	#histSnapshot( requestId:number, page:FromServer.HistSnapshot ){
		const live = this.#hist.get( requestId );
		if( live )
			live.subscriber.next( {kind: 'snapshot', values: this.#histValues( live, page.values ), more: page.more} );
	}
	#histRecords( requestId:number, records:FromServer.HistRecords ){
		const live = this.#hist.get( requestId );
		if( !live )
			return;
		const modifications:HistPushedModification[] = [];
		for( const m of records.modifications ){
			const modification = Gateway.toHistModification( m, live.args.nodes );
			if( modification )
				modifications.push( modification );
		}
		live.subscriber.next( {kind: 'records', values: this.#histValues( live, records.values ), modifications} );
	}
	//the values as the model holds them, and each slot's latest source time kept:  where a resubscribe starts from.  A bound of
	//Bad_BoundNotFound, the snapshot's stand-in at fromTime for a node with no record before, is no value:  merged, it drew a
	//flagged gap and listed a row for a node that is only new (historian-live-trend #6).
	#histValues( live:HistLive, protos:FromServer.HistValue[] ):HistValue[]{
		const values:HistValue[] = [];
		for( const proto of protos ){
			const v = Gateway.toHistValue( proto, live.args.nodes );
			if( !v || (v.bound && isBoundNotFound(v.status)) )
				continue;
			values.push( v );
			const held = live.latest.get( proto.node );
			if( v.source && (!held || v.source>held) )
				live.latest.set( proto.node, v.source );
		}
		return values;
	}
	#histException( requestId:number, e:{what:string; code:number; statusCode:number} ):boolean{
		const live = this.#hist.get( requestId );
		if( !live )
			return false;
		this.#hist.delete( requestId );
		live.subscriber.error( {error: {requestId, message: e.what, sc: e.code, httpStatus: e.statusCode}} );
		return true;
	}
	//`dropped` once an outage, from its first drop to the next ack:  gated by the ack, a drop before the first was silent, and the
	//tab loaded with nothing said for as long as the gateway was down (historian-live-trend #8)
	#histDropped( live:HistLive ){
		this.#hist.delete( live.requestId );
		if( !live.down ){
			live.down = true;
			live.subscriber.next( {kind: 'dropped'} );
		}
		clearTimeout( live.retry );
		live.retry = setTimeout( ()=>this.#histSend( live, histResumeTime( live.latest.values() ) ?? live.args.from ), Gateway.histRetryDelay );
	}
	#hist = new Map<number,HistLive>();//by the HistSubscribe's request id, which names the subscription on the wire
	static readonly histRetryDelay = 5_000;//ms after a drop before subscribing again, and between tries while the gateway is down
	//a record of a subscription, its node by slot (spec *Subscriptions*);  one naming a slot the subscribe didn't is dropped
	private static toHistValue( proto:FromServer.HistValue, nodes:NodeId[] ):HistValue|undefined{
		const node = nodes[proto.node];
		if( !node )
			return void console.warn( `A historian record names slot ${proto.node} of ${nodes.length} nodes.` );
		return { node, source: proto.source ?? proto.server ?? null, server: proto.server ?? null, status: proto.sc>>>0, value: proto.value ? Gateway.toValue( proto.value ) : undefined, bound: proto.bound, heartbeat: proto.heartbeat };
	}
	private static toHistModification( proto:FromServer.HistModification, nodes:NodeId[] ):HistPushedModification|undefined{
		const node = nodes[proto.node], kind = Gateway.histPushKinds[proto.type];
		if( !node || !kind || !proto.target )
			return void console.warn( `A historian modification names slot ${proto.node} of ${nodes.length} nodes, type ${proto.type}${proto.target ? "" : ", and no target"}.` );
		const value = proto.value ? Gateway.toHistValue( proto.value, nodes ) : undefined;
		return { node, target: proto.target, kind, value, time: proto.time ?? null, user: proto.user };
	}
	private static readonly histPushKinds:Record<number,HistPushKind> = { 1: 'insert', 2: 'replace', 3: 'update', 4: 'delete' };//Part 11's HistoryUpdateType
	//HistSubscribe.nodes are ExpandedNodeIds, as a read names them:  the server's index, as the page holds the node
	private static toExProto( node:NodeId ):Common.ExpandedNodeId{
		return { node: Gateway.toProto( [node] )[0], namespaceUri: "", serverIndex: 0 };
	}

	private static toGuid( proto:Uint8Array ):Guid{ let guid = new Guid(); guid.value = proto; return guid; }
	private static toValue( proto:Common.Value ):Value{
		//protobufjs exposed a virtual `of` getter naming the set oneof field; ts-proto emits plain optionals, so each arm
		//tests for undefined rather than truthiness - false, 0 and "" are values, not absence.
		if( proto.boolean!=undefined )      return proto.boolean;
		if( proto.byte!=undefined )         return proto.byte;
		if( proto.byteString!=undefined )   return proto.byteString;
		if( proto.date!=undefined )         return ProtoUtils.fromDate( proto.date )!;//ts-proto decodes google.protobuf.Timestamp to a Date; normalise to the {seconds,nanos} shape the rest of the Value pipeline (GraphQL JSON) uses, so there is one representation
		if( proto.doubleValue!=undefined )  return proto.doubleValue;
		if( proto.duration!=undefined )     return <Duration>proto.duration;
		if( proto.expandedNode!=undefined ) return Gateway.toExpanded( proto.expandedNode );
		if( proto.floatValue!=undefined )   return shortestFloat( proto.floatValue );
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
		if( proto.localizedText!=undefined ) return proto.localizedText.text ?? "";//the text alone, as REST gives it
		if( proto.qualifiedName!=undefined ){ const q = proto.qualifiedName; return q.namespaceIndex ? `${q.namespaceIndex}:${q.name}` : q.name; }
		if( proto.extensionObject!=undefined ){
			const x = proto.extensionObject;
			return new ExtensionObject( Gateway.toNode(x.typeId ?? Common.NodeId.create()), x.binary ?? x.xml );
		}
		if( proto.array!=undefined )        return proto.array.values.map( v=>Gateway.toValue(v) );
		return undefined!;
	}
	private nodeValues( nodeValues:FromServer.NodeValues ):void{
		let opcSubscriptions = this.#subscriptions.get( nodeValues.opcId! ); if( !opcSubscriptions ){ return console.error(`Could not find opc ${nodeValues.opcId}`);}
		const node = Gateway.toNode( nodeValues.node! );
		const sc = nodeValues.sc ?? 0;//proto3 omits 0/Good from the wire.
		//A Bad push still carries the reading the server holds.  Swapping it for an OpcError here destroyed it, and the table
		//bound the error into its editors;  sc says what the reading is worth.  No member = an empty Variant, undefined rather
		//than the empty array an empty array reading is.
		const value = nodeValues.value ? Gateway.toValue( nodeValues.value ) : undefined;
		//the timestamps as the server sent them - ts-proto decodes google.protobuf.Timestamp to a Date, and absent stays undefined
		opcSubscriptions.get( node.key )?.forEach( owner=>this.#ownerSubscriptions.get(owner)!.next({opcId:nodeValues.opcId!, node:node, value:value, sc:sc, source:nodeValues.source, server:nodeValues.server}) );
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
				const request:FromClient.Unsubscribe = { nodes:Gateway.toProto(nodes), opcId:opcId };
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
			const request:FromClient.Unsubscribe = { nodes:Gateway.toProto(toDelete), opcId:opcId };
			return this.sendPromise<void>( {"unsubscribe": request}, `unsubscribe opcId: ${opcId}, nodeCount: ${toDelete.length}` );
		}
		else
			return Promise.resolve();
	}
	get name():string{ return this.instances[0].instanceName!; }
	get slug():GatewaySlug{ return this.instances[0].instanceName!; }
}
//a pushed Reading - Bad keeps the value the server holds, and sc says so.  A subscribe that FAILED is the one OpcError `value`:  a
//refused request, not a reading.  `source`/`server` are the reading's timestamps when the server sent them:  the history
//trend places a push after its read by source time.
export type SubscriptionResult = Reading & {opcId:string, node:NodeId, source?:Date, server?:Date};
//A historian subscription's arguments (spec *Subscriptions*):  the group by its id, its nodes, and the time the snapshot starts
//from - left out, it is now, and the snapshot is each node's value alone.
export type HistSubscribeArgs = { group:number; nodes:NodeId[]; from?:Date };
//What a historian subscription delivers, in order:  the ack, a status per node asked, Good or Bad_NodeIdUnknown for one the
//group doesn't hold;  the snapshot's pages, `more` until the last, each node's bound first - none for a node with no record
//before `from`, whose Bad_BoundNotFound stand-in is dropped;  then the records as the historian stores them, values and the
//modifications edits make, and `dropped` when the socket went, once until the next ack, whether or not one came before -
//after which the ack and a snapshot from where the records delivered leave off follow, once the gateway is back.
export type HistEvent =
	{ kind:'ack'; statuses:StatusCode[] } |
	{ kind:'snapshot'; values:HistValue[]; more:boolean } |
	{ kind:'records'; values:HistValue[]; modifications:HistPushedModification[] } |
	{ kind:'dropped' };
type HistLive = { args:HistSubscribeArgs; subscriber:Subscriber<HistEvent>; requestId:number; down:boolean; latest:Map<number,Date>; retry?:ReturnType<typeof setTimeout> };//down:  dropped and not acked since;  latest:  by slot
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const GATEWAY_SERVICE = new InjectionToken<GatewayService>( 'GatewayService' );