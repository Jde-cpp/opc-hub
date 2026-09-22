if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { HttpClient } from '@angular/common/http';
import { vi } from 'vitest';
import { AuthStore, ETransport } from 'jde-framework';
import { NodeId } from '../model/node-id';
import { OpcError } from '../model/opc-error';
import { scBadUnexpectedError } from '../model/types';
import { Gateway, SubscriptionResult } from './gateway-service';
import { OpcStore } from './opc-store';

const opcId = "local";
const A = new NodeId( {ns:2, i:1} ), B = new NodeId( {ns:2, i:2} );

class TestGateway extends Gateway{
	constructor(){
		super( {host:'localhost', port:1968, instanceName:'gw'} as any, ETransport.Unsecure, {} as HttpClient,
			{user: ()=>undefined, logout: ()=>{}} as unknown as AuthStore, new OpcStore() );
	}
	qlResult:any = {serverConnections: []};
	override async ql<Y>():Promise<Y>{ return this.qlResult as Y; }//the constructor's connection query - no http here;  then whatever a test's read() should get
	posted:any;
	override async post<Y>():Promise<Y>{ return this.posted as Y; }//write()'s mutation
	gets:string[] = [];
	release?:()=>void;//set = a get() parks until the test lets it go
	names:{sc:number, message:string}[] = [];
	override async get<Y>( target:string ):Promise<Y>{
		this.gets.push( target );
		if( this.release )
			await new Promise<void>( r=>this.release = r );
		return {errorCodes: this.names} as Y;
	}
	//the subscribe reply the server would send, or a rejection standing in for a send that never got there.
	statusCodes:( number|undefined )[] = [];
	acked?:{ statusCode?:number, node?:any }[];//set = the reply as given, node ids and all - in the gateway's order, not the request's
	rejectWith?:any;
	sent:any[] = [];
	override sendPromise<T>( m:any, log:string ):Promise<T>{
		this.sent.push( m );
		if( !m.subscribe )
			return Promise.resolve( undefined as T );
		if( this.rejectWith )
			return Promise.reject( this.rejectWith );
		return Promise.resolve( (this.acked ?? this.statusCodes.map( statusCode=>({statusCode}) )) as T );
	}
	get unsubscribed():NodeId[]{ return this.sent.filter( m=>m.unsubscribe ).flatMap( m=>m.unsubscribe.nodes ); }
}
//`unsubscribe` only sends for a node this owner is actually registered on, so a send is proof the registration survived.
const stillRegistered = async ( gateway:TestGateway, node:NodeId, owner:string )=>{
	const before = gateway.unsubscribed.length;
	await gateway.unsubscribe( opcId, [node], owner );
	return gateway.unsubscribed.length>before;
};

describe( 'Gateway subscribe failures', ()=>{
	let gateway:TestGateway;
	beforeEach( ()=>{ gateway = new TestGateway(); } );

	//angular-review3 #9: the per-node failure path deleted the whole per-node-key entry, so a key a SECOND owner also held
	//lost that owner's registration too - the server kept pushing and nodeValues' forEach silently no-oped.
	it( 'a per-node failure drops only the failing owner from a shared node', async ()=>{
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next:()=>{}, error:()=>{}} );
		await Promise.resolve();
		gateway.statusCodes = [0x80340000];//BadNodeIdUnknown for owner2's request
		gateway.subscribe( opcId, [A], "owner2" ).subscribe( {next:()=>{}, error:()=>{}} );
		await Promise.resolve();
		expect( await stillRegistered(gateway, A, "owner1") ).toBe( true );
		expect( await stillRegistered(gateway, A, "owner2") ).toBe( false );
	} );

	//...and the catch called clearOwner, unsubscribing the owner's already-live nodes and erroring the shared Subject.
	it( 'a send error leaves the owner\'s other live nodes alone', async ()=>{
		const results:SubscriptionResult[] = [];
		let errored:any;
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: e=>errored=e} );
		await Promise.resolve();
		gateway.rejectWith = { error: {requestId: 1, message: "Connection lost."} };
		gateway.addToSubscription( opcId, [B], "owner1" );
		await Promise.resolve(); await Promise.resolve();

		expect( errored ).toBeUndefined();//erroring the Subject ended subscriptions that had nothing to do with the request
		expect( gateway.unsubscribed ).toHaveLength( 0 );//nothing reached the server, so there is nothing to unsubscribe
		expect( await stillRegistered(gateway, A, "owner1") ).toBe( true );
	} );

	it( 'reports the failed node on the stream as a bad reading', async ()=>{
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await Promise.resolve();
		gateway.rejectWith = { error: {requestId: 1, message: "Connection lost."} };
		gateway.addToSubscription( opcId, [B], "owner1" );
		await Promise.resolve(); await Promise.resolve();

		expect( results ).toHaveLength( 1 );
		expect( results[0].node.equals(B) ).toBe( true );
		expect( results[0].value ).toBeInstanceOf( OpcError );
		expect( results[0].sc ).toBe( scBadUnexpectedError );//no per-node code to report - the request never got there
	} );

	//reviews/m3-closing.md #10:  the gateway answers in NodeId order - never the request's - and the client blamed results[i] on
	//nodes[i].  Asked [B, C, A] with B refused, the ack reads [A, B✗, C]:  C - healthy - was marked Bad and dropped, B stayed on.
	it( 'places a failure on the node the ack names, not on the row at its index', async ()=>{
		const C = new NodeId( {ns:2, i:3} );
		const results:SubscriptionResult[] = [];
		gateway.acked = [ {node: {namespaceIndex: 2, numeric: 1}}, {node: {namespaceIndex: 2, numeric: 2}, statusCode: 0x80340000}, {node: {namespaceIndex: 2, numeric: 3}} ];
		gateway.subscribe( opcId, [B, C, A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await Promise.resolve(); await Promise.resolve();
		expect( results.map(r=>r.node.key) ).toEqual( [B.key] );
		expect( await stillRegistered(gateway, C, "owner1") ).toBe( true );
		expect( await stillRegistered(gateway, B, "owner1") ).toBe( false );
	} );

	//an ack that names no nodes - a gateway older than the field - can only be trusted positionally for a single node
	it( 'blames nothing when a multi-node ack does not name its nodes', async ()=>{
		const warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [undefined, 0x80340000];
		gateway.subscribe( opcId, [B, A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await Promise.resolve(); await Promise.resolve();
		expect( results ).toHaveLength( 0 );
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	} );

	it( 'carries the server\'s own status code through when it gave one', async ()=>{
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [0x80340000];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await Promise.resolve(); await Promise.resolve();
		expect( results.map(r=>r.sc) ).toEqual( [0x80340000] );
		expect( (results[0].value as OpcError).sc ).toBe( 0x80340000 );
	} );
} );

//OPC 10000-4 7.38:  the quality travels with the value on every path.  read(), write() and snapshot() ran toValue() alone and
//dropped it;  nodeValues swapped a Bad reading's value for an OpcError, though the socket carries the one the server holds.
describe( 'Gateway readings', ()=>{
	const bad = 0x808C0000, uncertain = 0x40940600;
	let gateway:TestGateway;
	let results:SubscriptionResult[];
	beforeEach( async ()=>{
		gateway = new TestGateway();
		results = [];
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await Promise.resolve();
	} );
	const push = ( values:object[], sc?:number )=>(gateway as any).nodeValues( {opcId, node: {namespaceIndex: 2, numeric: 1}, values, sc} );

	it( 'pushes a Good reading as its value', ()=>{
		push( [{doubleValue: 7}] );//proto3 leaves 0/Good off the wire
		expect( results ).toMatchObject( [{value: 7, sc: 0}] );
	} );

	//toNode built the NodeId from its namespace alone and set the id after - and an id-less NodeId is what the constructor
	//reports, so every push (one a second, per node) wrote "NodeId - unrecognized json" to the console for a node that was fine.
	it( 'builds the pushed node without a console error, whatever kind of id it has', ()=>{
		const errors:any[] = [];
		const previous = console.error;
		console.error = ( ...args:any[] )=>errors.push( args );
		try{
			push( [{doubleValue: 7}] );
			const toNode = ( proto:object )=>(Gateway as any).toNode( proto );
			expect( toNode({namespaceIndex: 2, numeric: 0}) ).toMatchObject( {ns: 2, id: 0} );//0 and "" are ids, not absence
			expect( toNode({namespaceIndex: 2, string: ""}) ).toMatchObject( {ns: 2, id: ""} );
			expect( toNode({namespaceIndex: 2, byteString: new Uint8Array([1, 2])}).id ).toEqual( new Uint8Array([1, 2]) );
			const expanded = (Gateway as any).toExpanded( {node: {namespaceIndex: 3, numeric: 9}, namespaceUri: "urn:x", serverIndex: 1} );
			expect( expanded ).toMatchObject( {ns: 3, id: 9, nsu: "urn:x", serverIndex: 1} );
			expect( errors ).toEqual( [] );
			toNode( {namespaceIndex: 2} );//no identifier at all is still worth a line
			expect( errors ).toHaveLength( 1 );
		}
		finally{ console.error = previous; }
		expect( results[0].node.equals(A) ).toBe( true );
	} );

	it( 'keeps an Uncertain reading\'s value and says what it is worth', ()=>{
		push( [{doubleValue: 1500}], uncertain );
		expect( results ).toMatchObject( [{value: 1500, sc: uncertain}] );
	} );

	it( 'keeps the value a Bad push carries, rather than an OpcError in its place', ()=>{
		push( [{doubleValue: 612}], bad );
		expect( results ).toMatchObject( [{value: 612, sc: bad}] );
	} );

	it( 'has no value for a Bad push that carries none - not an empty array', ()=>{
		push( [], bad );
		expect( results[0].value ).toBeUndefined();
		expect( results[0].sc ).toBe( bad );
	} );

	it( 'reads the quality with the value', async ()=>{
		gateway.qlResult = {node: {value: {v: 1500, sc: uncertain}}};
		expect( await gateway.read(opcId, A) ).toEqual( {value: 1500, sc: uncertain} );
		gateway.qlResult = {node: {value: {sc: bad}}};
		expect( await gateway.read(opcId, A) ).toEqual( {sc: bad} );
		gateway.qlResult = {node: {value: 7}};
		expect( await gateway.read(opcId, A) ).toEqual( {value: 7, sc: 0} );
	} );

	it( 'answers a write with the echo\'s quality', async ()=>{
		gateway.posted = {data: {updateVariable: {value: {v: 9, sc: uncertain}}}};
		expect( await gateway.write(opcId, A, 9, ()=>{}) ).toEqual( {value: 9, sc: uncertain} );
	} );

	//a fault holds its code on every push it lasts, and each of those asks.
	it( 'fetches the status names once while a request is out, and as unsigned codes', async ()=>{
		new OpcError( 0x80AC0600, "OpcError", "", undefined );//a name to fetch - one no other test has asked for
		gateway.release = ()=>{};
		const first = gateway.updateErrorCodes(), second = gateway.updateErrorCodes();
		await Promise.resolve();
		expect( gateway.gets ).toHaveLength( 1 );
		expect( gateway.gets[0] ).toContain( String(0x80AC0000) );//the name's key:  flags off, and not the negative int32 `&` makes of a Bad code
		expect( gateway.gets[0] ).not.toContain( "-" );
		gateway.names = [{sc: 0x80AC0000, message: "BadTest"}];
		gateway.release();
		await Promise.all( [first, second] );
		expect( OpcError.text(0x80AC0600) ).toBe( "BadTest+High" );
	} );

	it( 'does not reject when the names cannot be fetched', async ()=>{
		new OpcError( 0x80AD0000, "OpcError", "", undefined );
		gateway.get = ()=>Promise.reject( new Error("offline") );
		await expect( gateway.updateErrorCodes() ).resolves.toBeUndefined();
	} );
} );

describe( 'Gateway socket path', ()=>{
	it( 'upgrades on /opc - the path an OpcHub routes the gateway protocol by', ()=>{
		const gateway = new TestGateway();
		expect( (gateway as any).socketUrl ).toBe( 'ws://localhost:1968/opc' );
	});
});