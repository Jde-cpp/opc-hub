//Spec helpers, imported as `jde-opc/testing` - see jde-spa/testing.
import { HttpClient } from '@angular/common/http';
import { Observable, Subscriber } from 'rxjs';
import { ETransport } from 'jde-framework';
import { anonymousAuthStore, qlAnswering } from 'jde-framework/testing';
import * as FromServer from 'jde-proto/Opc.FromServer';
import { HistTime, HistValue } from '../lib/model/hist';
import { UaNode, Variable } from '../lib/model/node';
import { NodeId, NodeKey } from '../lib/model/node-id';
import { Value } from '../lib/model/value';
import { Gateway, HistEvent, HistSubscribeArgs, SubscriptionResult } from '../lib/services/gateway-service';
import { OpcStore } from '../lib/services/opc-store';

export const nodeA = new NodeId( {ns:2, i:1} ), nodeB = new NodeId( {ns:2, i:2} );
//A Variable as a browse answers it:  ns 2, browsed by `name`, with `json` on top.
export const variable = ( i:number, name:string, json:object={} )=>new Variable( <any>{ns:2, i, name, browse: {ns:2, name}, ...json} );

//A historian record of `node` at `ms` - a push's shape:  a read's carries `sourceTime: histTick(ms)` as well.
export const histValue = ( node:NodeId, ms:number|null, value?:Value, extra:Partial<HistValue>={} ):HistValue=>
	({ node: node instanceof UaNode ? node.nodeId : node, source: ms==null ? null : new Date(ms), server: null, status: 0, value, bound: false, heartbeat: false, ...extra });
export const histTick = ( ms:number, nanos=ms%1000*1_000_000 ):HistTime=>({ seconds: Math.floor(ms/1000), nanos });

//fetch, for the trend's add-on probe:  `answer` gives every url's response - fetchAnswer one with a status and a content type.
//vi.unstubAllGlobals() puts the real one back.
export const fetchAnswer = ( status:number, type='text/plain' )=>async ( _url?:string )=>({ status, ok: status>=200 && status<300, headers: {get: ( name:string )=>name.toLowerCase()=='content-type' ? type : null} });
export function stubFetch( answer:( url:string )=>Promise<unknown> ){
	const fetch = vi.fn( answer );
	vi.stubGlobal( 'fetch', fetch );
	return fetch;
}

//The pumps server the historian specs browse, as OpcStore.getConnection describes it.
export const Pumps = 'urn:jde:pumps';
export const pumpsServer = { connection: {name: 'Local'}, namespaces: [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: Pumps}], uri( ns:number ){ return this.namespaces.find( n=>n.index==ns )?.uri; } };

//A stream a test feeds:  `subscribers` are the subscriptions open on it, each with what it asked for, and next/error/complete
//reach the latest - an error or a completion closes it.
export class Feed<T, A=unknown>{
	open( args:A ):Observable<T>{
		return new Observable<T>( subscriber=>{
			const entry = { args, subscriber };
			this.subscribers.push( entry );
			return ()=>{ this.subscribers.splice( this.subscribers.indexOf(entry), 1 ); };
		} );
	}
	get latest(){ return this.subscribers.at( -1 ); }
	next( value:T ){ this.latest!.subscriber.next( value ); }
	error( e:unknown ){ this.latest!.subscriber.error( e ); }
	complete(){ this.latest!.subscriber.complete(); }
	subscribers:{ args:A; subscriber:Subscriber<T> }[] = [];
}

//A Gateway for a page or a resolver to read through:  query/queryArray answer from `answers` as qlAnswering does, querySingle
//nothing, browseObjectsFolder a node's children in `tree` by its key, and subscribe/histSubscribe open `values`/`histEvents` for the
//test to feed.  Assign over whatever else a test needs.
export function fakeGateway( slug:string, answers:Record<string,unknown>={}, tree=new Map<NodeKey,UaNode[]>() ){
	const values = new Feed<SubscriptionResult, NodeId[]>(), histEvents = new Feed<HistEvent, HistSubscribeArgs>();
	return {
		slug, values, histEvents,
		query: qlAnswering( answers ),
		queryArray: qlAnswering( answers, {unwrapped: true} ),
		querySingle: vi.fn( async ( _ql:string, _vars?:any ):Promise<any>=>null ),
		browseObjectsFolder: vi.fn( async ( _opc:string, node:UaNode )=>tree.get( node.key ) ?? [] ),
		subscribe: vi.fn( ( _opc:string, nodes:NodeId[], _owner?:string )=>values.open( nodes ) ),
		histSubscribe: vi.fn( ( args:HistSubscribeArgs )=>histEvents.open( args ) ),
		updateErrorCodes: vi.fn( async ()=>{} )
	};
}

//A Gateway with no socket and no http.  The http calls answer fields:  ql (the constructor's connection query too) `qlResult`, a
//mutation `posted`, and a get the status code names, parked while `release` is set;  `gets` is told each target.  The socket:
//`wire` is told every message sent, `receive` hands the gateway what the server would send, `drop` loses the connection - or,
//with `acking`, sendPromise answers at once:  a subscribe with `acked` or a status per `statusCodes` (or `rejectWith`), anything
//else with nothing, and `sent` is told each message.
export class TestGateway extends Gateway{
	constructor(){
		super( {host:'localhost', port:1968, instanceName:'gw'} as any, ETransport.Unsecure, {} as HttpClient, anonymousAuthStore(), new OpcStore() );
	}
	qlResult:any = {serverConnections: []};
	override async ql<Y>():Promise<Y>{ return this.qlResult as Y; }
	posted:any;
	override async postRaw<Y>():Promise<Y>{ return this.posted as Y; }
	gets = vi.fn<( target:string )=>void>();
	release?:()=>void;
	names:{sc:number, message:string}[] = [];
	override async get<Y>( target:string ):Promise<Y>{
		this.gets( target );
		if( this.release )
			await new Promise<void>( r=>this.release = r );
		return {errorCodes: this.names} as Y;
	}

	wire = vi.fn<( requestId:number, m:any )=>void>();
	protected override sendWithId( m:any, requestId:number, _log:string ){ this.wire( requestId, m ); }
	receive( ...messages:any[] ){ (<any>this).processMessage( FromServer.Transmission.encode( FromServer.Transmission.create( {messages} ) ).finish() ); }
	drop(){ (<any>this).handleConnectionError( {message: "Connection to the gateway was lost."} ); }

	acking = false;
	statusCodes:( number|undefined )[] = [];
	acked?:{ statusCode?:number, node?:any }[];//the reply as given, node ids and all - in the gateway's order, not the request's
	rejectWith?:any;
	sent = vi.fn<( m:any )=>void>();
	override sendPromise<T>( m:any, log:string ):Promise<T>{
		if( !this.acking )
			return super.sendPromise<T>( m, log );
		this.sent( m );
		if( !m.subscribe )
			return Promise.resolve( undefined as T );
		if( this.rejectWith )
			return Promise.reject( this.rejectWith );
		return Promise.resolve( (this.acked ?? this.statusCodes.map( statusCode=>({statusCode}) )) as T );
	}
}