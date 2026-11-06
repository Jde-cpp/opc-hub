import { NodeId } from '../model/node-id';
import { ExtensionObject, valueString } from '../model/value';
import { OpcError } from '../model/opc-error';
import { scBadUnexpectedError } from '../model/types';
import { Gateway, SubscriptionResult } from './gateway-service';
import * as FromServer from 'jde-proto/Opc.FromServer';
import { flush, silence } from 'jde-spa/testing';
import { nodeA as A, nodeB as B, TestGateway } from 'jde-opc/testing';

const opcId = "local";

//TestGateway answering its subscribes at once, and the nodes it unsubscribed.
class AckingGateway extends TestGateway{
	override acking = true;
	get unsubscribed():NodeId[]{ return this.sent.mock.calls.map( ([m])=>m ).filter( m=>m.unsubscribe ).flatMap( m=>m.unsubscribe.nodes ); }
}
//`unsubscribe` only sends for a node this owner is actually registered on, so a send is proof the registration survived.
const stillRegistered = async ( gateway:AckingGateway, node:NodeId, owner:string )=>{
	const before = gateway.unsubscribed.length;
	await gateway.unsubscribe( opcId, [node], owner );
	return gateway.unsubscribed.length>before;
};

describe( 'Gateway subscribe failures', ()=>{
	//angular-review3 #9: the per-node failure path deleted the whole per-node-key entry, so a key a SECOND owner also held
	//lost that owner's registration too - the server kept pushing and nodeValues' forEach silently no-oped.
	it( 'a per-node failure drops only the failing owner from a shared node', async ()=>{
		const gateway = new AckingGateway();
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next:()=>{}, error:()=>{}} );
		await flush();
		gateway.statusCodes = [0x80340000];//BadNodeIdUnknown for owner2's request
		gateway.subscribe( opcId, [A], "owner2" ).subscribe( {next:()=>{}, error:()=>{}} );
		await flush();
		expect( await stillRegistered(gateway, A, "owner1") ).toBe( true );
		expect( await stillRegistered(gateway, A, "owner2") ).toBe( false );
	} );

	//...and the catch called clearOwner, unsubscribing the owner's already-live nodes and erroring the shared Subject.
	it( 'a send error leaves the owner\'s other live nodes alone', async ()=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		let errored:any;
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: e=>errored=e} );
		await flush();
		gateway.rejectWith = { error: {requestId: 1, message: "Connection lost."} };
		gateway.addToSubscription( opcId, [B], "owner1" );
		await flush();

		expect( errored ).toBeUndefined();//erroring the Subject ended subscriptions that had nothing to do with the request
		expect( gateway.unsubscribed ).toHaveLength( 0 );//nothing reached the server, so there is nothing to unsubscribe
		expect( await stillRegistered(gateway, A, "owner1") ).toBe( true );
	} );

	//a `revised` result - the ack's answer for a node that was taken (spec *Watch list*) - is no reading:  these tests read the readings
	const readings = ( results:SubscriptionResult[] )=>results.filter( r=>!r.revised );
	it( 'reports the failed node on the stream as a bad reading', async ()=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>{ if( !r.revised ) results.push(r); }, error: ()=>{}} );
		await flush();
		gateway.rejectWith = { error: {requestId: 1, message: "Connection lost."} };
		gateway.addToSubscription( opcId, [B], "owner1" );
		await flush();

		expect( results ).toHaveLength( 1 );
		expect( results[0].node.equals(B) ).toBe( true );
		expect( results[0].value ).toBeInstanceOf( OpcError );
		expect( results[0].sc ).toBe( scBadUnexpectedError );//no per-node code to report - the request never got there
	} );

	//reviews/m3-closing.md #10:  the gateway answers in NodeId order - never the request's - and the client blamed results[i] on
	//nodes[i].  Asked [B, C, A] with B refused, the ack reads [A, B✗, C]:  C - healthy - was marked Bad and dropped, B stayed on.
	it( 'places a failure on the node the ack names, not on the row at its index', async ()=>{
		const gateway = new AckingGateway();
		const C = new NodeId( {ns:2, i:3} );
		const results:SubscriptionResult[] = [];
		gateway.acked = [ {node: {namespaceIndex: 2, numeric: 1}}, {node: {namespaceIndex: 2, numeric: 2}, statusCode: 0x80340000}, {node: {namespaceIndex: 2, numeric: 3}} ];
		gateway.subscribe( opcId, [B, C, A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await flush();
		expect( readings( results ).map(r=>r.node.key) ).toEqual( [B.key] );
		expect( results.filter( r=>r.revised ).map( r=>r.node.key ) ).toEqual( [A.key, C.key] );//the taken nodes' revised values, in the ack's order
		expect( await stillRegistered(gateway, C, "owner1") ).toBe( true );
		expect( await stillRegistered(gateway, B, "owner1") ).toBe( false );
	} );

	//an ack that names no nodes - a gateway older than the field - can only be trusted positionally for a single node
	it( 'blames nothing when a multi-node ack does not name its nodes', async ()=>{
		const gateway = new AckingGateway();
		const warn = silence();
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [undefined, 0x80340000];
		gateway.subscribe( opcId, [B, A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await flush();
		expect( results ).toHaveLength( 0 );
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	} );

	it( 'carries the server\'s own status code through when it gave one', async ()=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [0x80340000];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await flush();
		expect( results.map(r=>r.sc) ).toEqual( [0x80340000] );
		expect( (results[0].value as OpcError).sc ).toBe( 0x80340000 );
	} );
} );

//OPC 10000-4 7.38:  the quality travels with the value on every path.  read(), write() and snapshot() ran toValue() alone and
//dropped it;  nodeValues swapped a Bad reading's value for an OpcError, though the socket carries the one the server holds.
describe( 'Gateway readings', ()=>{
	const bad = 0x808C0000, uncertain = 0x40940600;
	//a gateway subscribed to A, the readings it put on the stream, and `push` to hand it one from the socket
	const subscribed = async ()=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		gateway.statusCodes = [undefined];
		gateway.subscribe( opcId, [A], "owner1" ).subscribe( {next: r=>{ if( !r.revised ) results.push(r); }, error: ()=>{}} );//the readings:  the ack's revised result is the watch list's (spec *Watch list*)
		await flush();
		const push = ( value:object|undefined, sc?:number )=>(gateway as any).nodeValues( {opcId, node: {namespaceIndex: 2, numeric: 1}, value, sc} );
		return { gateway, results, push };
	};

	it( 'pushes a Good reading as its value', async ()=>{
		const { results, push } = await subscribed();
		push( {doubleValue: 7} );//proto3 leaves 0/Good off the wire
		expect( results ).toMatchObject( [{value: 7, sc: 0}] );
	} );

	//418-watch-items #8:  processMessage had no branch for the gateway's MonitoredItemModified push, so it threw "unknown message"
	//and the rest of the transmission - here a reading - went unread.
	it( 'takes a MonitoredItemModified push without an error, and reads on past it', async ()=>{
		const { gateway, results } = await subscribed();
		const node = {namespaceIndex: 2, numeric: 1};
		const transmission = FromServer.Transmission.encode( FromServer.Transmission.create({messages: [
			{monitoredItemModified: {opcId, node, revisedSamplingInterval: 250, revisedQueueSize: 2}},
			{nodeValues: {opcId, node, value: {doubleValue: 7}}}
		]}) ).finish();
		const error = silence( 'error' );
		const debug = vi.spyOn( console, 'debug' ).mockImplementation( ()=>{} );
		try{
			(gateway as any).processMessage( transmission );
			expect( error ).not.toHaveBeenCalled();
		}
		finally{
			error.mockRestore();
			debug.mockRestore();
		}
		expect( results ).toMatchObject( [{value: 7, sc: 0}] );
	} );

	//toNode built the NodeId from its namespace alone and set the id after - and an id-less NodeId is what the constructor
	//reports, so every push (one a second, per node) wrote "NodeId - unrecognized json" to the console for a node that was fine.
	it( 'builds the pushed node without a console error, whatever kind of id it has', async ()=>{
		const { results, push } = await subscribed();
		const error = silence( 'error' );
		try{
			push( {doubleValue: 7} );
			const toNode = ( proto:object )=>(Gateway as any).toNode( proto );
			expect( toNode({namespaceIndex: 2, numeric: 0}) ).toMatchObject( {ns: 2, id: 0} );//0 and "" are ids, not absence
			expect( toNode({namespaceIndex: 2, string: ""}) ).toMatchObject( {ns: 2, id: ""} );
			expect( toNode({namespaceIndex: 2, byteString: new Uint8Array([1, 2])}).id ).toEqual( new Uint8Array([1, 2]) );
			const expanded = (Gateway as any).toExpanded( {node: {namespaceIndex: 3, numeric: 9}, namespaceUri: "urn:x", serverIndex: 1} );
			expect( expanded ).toMatchObject( {ns: 3, id: 9, nsu: "urn:x", serverIndex: 1} );
			expect( error ).not.toHaveBeenCalled();
			toNode( {namespaceIndex: 2} );//no identifier at all is still worth a line
			expect( error ).toHaveBeenCalledTimes( 1 );
		}
		finally{ error.mockRestore(); }
		expect( results[0].node.equals(A) ).toBe( true );
	} );

	//#195:  the gateway sent BadNotImplemented for these, and a structure is its encoded form, as the historian keeps it.
	it( 'shows the built-in types the gateway used to leave out', async ()=>{
		const { results, push } = await subscribed();
		push( {localizedText: {locale: "en", text: "Hello"}} );
		push( {localizedText: {}} );//no text at all
		push( {qualifiedName: {namespaceIndex: 2, name: "Temperature"}} );
		push( {qualifiedName: {namespaceIndex: 0, name: "Server"}} );
		push( {extensionObject: {typeId: {namespaceIndex: 0, numeric: 886}, binary: new Uint8Array([0, 255])}} );
		push( {array: {values: [{int32: 1}, {stringValue: "x"}], dimensions: [], type: 24}} );
		expect( results.map(r=>r.value) ).toMatchObject( ["Hello", "", "2:Temperature", "Server", {body: new Uint8Array([0, 255])}, [1, "x"]] );
		const x = results[4].value as ExtensionObject;
		expect( x ).toBeInstanceOf( ExtensionObject );
		expect( valueString(x) ).toBe( "i=886 AP8=" );
	} );

	it( 'keeps an Uncertain reading\'s value and says what it is worth', async ()=>{
		const { results, push } = await subscribed();
		push( {doubleValue: 1500}, uncertain );
		expect( results ).toMatchObject( [{value: 1500, sc: uncertain}] );
	} );

	it( 'keeps the value a Bad push carries, rather than an OpcError in its place', async ()=>{
		const { results, push } = await subscribed();
		push( {doubleValue: 612}, bad );
		expect( results ).toMatchObject( [{value: 612, sc: bad}] );
	} );

	it( 'has no value for a Bad push that carries none - not an empty array', async ()=>{
		const { results, push } = await subscribed();
		push( undefined, bad );
		expect( results[0].value ).toBeUndefined();
		expect( results[0].sc ).toBe( bad );
	} );

	//#195 review #2:  the gateway sent one Value per element, so a one-element array read as a scalar and an empty one as no value.
	it( 'keeps an array reading an array, whatever its length', async ()=>{
		const { results, push } = await subscribed();
		push( {array: {values: [{int32: 9}], dimensions: [], type: 6}} );
		push( {array: {values: [], dimensions: [], type: 12}} );//an empty String[]
		push( {} );//an empty Variant
		push( {array: {values: [1, 2, 3, 4, 5, 6].map( i=>({int32: i}) ), dimensions: [2, 3], type: 6}} );
		expect( results.map(r=>r.value) ).toEqual( [[9], [], undefined, [1, 2, 3, 4, 5, 6]] );
	} );

	it( 'reads the quality with the value', async ()=>{
		const { gateway } = await subscribed();
		gateway.qlResult = {node: {value: {v: 1500, sc: uncertain}}};
		expect( await gateway.read(opcId, A) ).toEqual( {value: 1500, sc: uncertain} );
		gateway.qlResult = {node: {value: {sc: bad}}};
		expect( await gateway.read(opcId, A) ).toEqual( {sc: bad} );
		gateway.qlResult = {node: {value: 7}};
		expect( await gateway.read(opcId, A) ).toEqual( {value: 7, sc: 0} );
	} );

	it( 'answers a write with the echo\'s quality', async ()=>{
		const { gateway } = await subscribed();
		gateway.posted = {data: {updateVariable: {value: {v: 9, sc: uncertain}}}};
		expect( await gateway.write(opcId, A, 9) ).toEqual( {value: 9, sc: uncertain} );
	} );

	//a fault holds its code on every push it lasts, and each of those asks.
	it( 'fetches the status names once while a request is out, and as unsigned codes', async ()=>{
		const { gateway } = await subscribed();
		new OpcError( 0x80AC0600, "OpcError", "", undefined );//a name to fetch - one no other test has asked for
		gateway.release = ()=>{};
		const first = gateway.updateErrorCodes(), second = gateway.updateErrorCodes();
		await flush();
		expect( gateway.gets ).toHaveBeenCalledOnce();
		expect( gateway.gets.mock.calls[0][0] ).toContain( String(0x80AC0000) );//the name's key:  flags off, and not the negative int32 `&` makes of a Bad code
		expect( gateway.gets.mock.calls[0][0] ).not.toContain( "-" );
		gateway.names = [{sc: 0x80AC0000, message: "BadTest"}];
		gateway.release();
		await Promise.all( [first, second] );
		expect( OpcError.text(0x80AC0600) ).toBe( "BadTest+High" );
	} );

	it( 'does not reject when the names cannot be fetched', async ()=>{
		const { gateway } = await subscribed();
		new OpcError( 0x80AD0000, "OpcError", "", undefined );
		gateway.get = ()=>Promise.reject( new Error("offline") );
		await expect( gateway.updateErrorCodes() ).resolves.toBeUndefined();
	} );
} );

//Clients 6b (#419, spec *Watch list*):  the parameters a watch row asks go on the wire at the node's position, the ack's revised
//values and a MonitoredItemModified push reach the stream as `revised` results, and subscribing a node again is how a listener
//changes what it asks.
describe( 'Gateway watch parameters', ()=>{
	const owner = "watch/gw";
	const subscribes = ( gateway:AckingGateway )=>gateway.sent.mock.calls.map( ([m])=>m ).filter( m=>m.subscribe ).map( m=>m.subscribe );
	//a gateway subscribed to A and B for `owner`, with the parameters given, and the results the stream delivered
	const watching = async ( parameters?:any[], acked?:any[] )=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		gateway.acked = acked ?? [ {node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 250, revisedQueueSize: 2}, {node: {namespaceIndex: 2, numeric: 2}, revisedSamplingInterval: 500, revisedQueueSize: 1} ];
		gateway.subscribe( opcId, [A, B], owner, parameters ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await flush();
		return { gateway, results };
	};

	it( 'sends each node\'s parameters at its position, and none for the defaults', async ()=>{
		const { gateway } = await watching( [ {samplingInterval: 100, queueSize: 5, discardOldest: false, filter: {trigger: 2, deadbandType: 1, deadbandValue: 0.5}}, {} ] );
		const [sent] = subscribes( gateway );
		expect( sent.nodes ).toHaveLength( 2 );
		expect( sent.parameters ).toEqual( [ {samplingInterval: 100, queueSize: 5, discardOldest: false, filter: {trigger: 2, deadbandType: 1, deadbandValue: 0.5}}, {samplingInterval: undefined, queueSize: undefined, discardOldest: undefined} ] );
		const defaults = new AckingGateway();
		defaults.subscribe( opcId, [A], "page" ).subscribe( {next: ()=>{}, error: ()=>{}} );
		await flush();
		expect( subscribes( defaults )[0].parameters ).toEqual( [] );//a node page asks nothing:  the gateway's defaults
	} );

	it( 'leaves a filter\'s trigger out for the server\'s default', async ()=>{
		const { gateway } = await watching( [ {filter: {deadbandType: 2, deadbandValue: 10}} ] );
		expect( subscribes( gateway )[0].parameters[0].filter ).toEqual( {trigger: undefined, deadbandType: 2, deadbandValue: 10} );
	} );

	it( 'merges the ack\'s revised values into the stream, one revised result per node', async ()=>{
		const { results } = await watching( [ {samplingInterval: 100}, {} ] );
		expect( results ).toHaveLength( 2 );
		expect( results.map( r=>r.node.key ) ).toEqual( [A.key, B.key] );
		expect( results[0].revised ).toEqual( {samplingInterval: 250, queueSize: 2, sc: 0} );
		expect( results[1].revised ).toEqual( {samplingInterval: 500, queueSize: 1, sc: 0} );
		expect( results[0].value ).toBeUndefined();//no reading:  a reader of values alone skips it
	} );

	it( 'reports a refused node as a bad reading and the other as revised', async ()=>{
		const { results } = await watching( undefined, [ {node: {namespaceIndex: 2, numeric: 1}, statusCode: 0x80340000}, {node: {namespaceIndex: 2, numeric: 2}, revisedSamplingInterval: 500, revisedQueueSize: 1} ] );
		expect( results.find( r=>r.node.equals(B) )?.revised ).toEqual( {samplingInterval: 500, queueSize: 1, sc: 0} );
		const refused = results.find( r=>r.node.equals(A) )!;
		expect( refused.revised ).toBeUndefined();
		expect( refused.value ).toBeInstanceOf( OpcError );
		expect( refused.sc ).toBe( 0x80340000 );
	} );

	it( 'delivers a MonitoredItemModified push to every owner of the node as a revised result', async ()=>{
		const { gateway, results } = await watching();
		const other:SubscriptionResult[] = [];
		gateway.acked = [ {node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 250, revisedQueueSize: 2} ];
		gateway.subscribe( opcId, [A], "page" ).subscribe( {next: r=>other.push(r), error: ()=>{}} );
		await flush();
		results.length = other.length = 0;
		gateway.receive( {monitoredItemModified: {opcId, node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 100, revisedQueueSize: 4}} );
		expect( results ).toMatchObject( [{opcId, revised: {samplingInterval: 100, queueSize: 4, sc: 0}}] );
		expect( results[0].node.equals(A) ).toBe( true );
		expect( other ).toMatchObject( [{revised: {samplingInterval: 100, queueSize: 4, sc: 0}}] );
		//a modify the server refused:  the item stands as it was, at the values given, and the status says why
		gateway.receive( {monitoredItemModified: {opcId, node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 250, revisedQueueSize: 2, statusCode: 0x80AF0000}} );
		expect( results[1].revised ).toEqual( {samplingInterval: 250, queueSize: 2, sc: 0x80AF0000} );
	} );

	it( 'ignores a push for a node nothing here watches, without an error', async ()=>{
		const { gateway, results } = await watching();
		const error = silence( 'error' ), debug = vi.spyOn( console, 'debug' ).mockImplementation( ()=>{} );
		try{
			gateway.receive( {monitoredItemModified: {opcId: "other", node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 100, revisedQueueSize: 4}} );
			gateway.receive( {monitoredItemModified: {opcId, node: {namespaceIndex: 2, numeric: 9}, revisedSamplingInterval: 100, revisedQueueSize: 4}} );
			expect( error ).not.toHaveBeenCalled();
		}
		finally{
			error.mockRestore();
			debug.mockRestore();
		}
		expect( results.filter( r=>r.revised?.samplingInterval==100 ) ).toHaveLength( 0 );
	} );

	it( 'subscribes a node again with other parameters, and the ack revises the row', async ()=>{
		const { gateway, results } = await watching( [ {samplingInterval: 100}, {} ] );
		gateway.acked = [ {node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 1000, revisedQueueSize: 10} ];
		gateway.addToSubscription( opcId, [A], owner, [ {samplingInterval: 1000, queueSize: 10} ] );
		await flush();
		expect( subscribes( gateway ) ).toHaveLength( 2 );
		expect( subscribes( gateway )[1].parameters ).toEqual( [ {samplingInterval: 1000, queueSize: 10, discardOldest: undefined} ] );
		expect( results.at( -1 )?.revised ).toEqual( {samplingInterval: 1000, queueSize: 10, sc: 0} );
		expect( await stillRegistered( gateway, A, owner ) ).toBe( true );//still one registration:  asked again, not added twice
		expect( await stillRegistered( gateway, A, owner ) ).toBe( false );
	} );

	//a request that failed cost the owner a node it already held:  the item ran on at the old parameters, its pushes found no
	//owner, and nothing unsubscribed it (419-watch #4)
	it( 'keeps a node it held when asking it again fails, and says so as a revised result', async ()=>{
		const { gateway, results } = await watching( [ {samplingInterval: 100}, {} ] );
		const C = new NodeId( {ns: 2, i: 3} );
		gateway.rejectWith = { error: {requestId: 2, message: "Connection lost.", sc: 0x80AE0000} };
		gateway.addToSubscription( opcId, [A, C], owner, [ {samplingInterval: 1000}, {} ] );
		await flush();
		const [a, c] = results.slice( -2 );
		expect( a.node.key ).toBe( A.key );
		expect( a.revised ).toEqual( {sc: 0x80AE0000} );
		expect( c.node.key ).toBe( C.key );
		expect( c.value ).toBeInstanceOf( OpcError );//never held, so not subscribed
		expect( await stillRegistered( gateway, A, owner ) ).toBe( true );
		expect( await stillRegistered( gateway, C, owner ) ).toBe( false );
	} );

	it( 'drops the ack\'s revised result for a node unsubscribed while the ack was out', async ()=>{
		const gateway = new AckingGateway();
		const results:SubscriptionResult[] = [];
		let answer:( y:unknown )=>void = ()=>{};
		gateway.sendPromise = <T>( m:any, _log:string ):Promise<T>=>{ gateway.sent( m ); return m.subscribe ? new Promise<T>( r=>answer = r as any ) : Promise.resolve( undefined as T ); };
		gateway.subscribe( opcId, [A], owner, [ {} ] ).subscribe( {next: r=>results.push(r), error: ()=>{}} );
		await gateway.unsubscribe( opcId, [A], owner );
		answer( [ {node: {namespaceIndex: 2, numeric: 1}, revisedSamplingInterval: 250, revisedQueueSize: 2} ] );
		await flush();
		expect( results ).toHaveLength( 0 );
	} );
} );

describe( 'Gateway socket path', ()=>{
	it( 'upgrades on /opc - the path an OpcHub routes the gateway protocol by', ()=>{
		const gateway = new AckingGateway();
		expect( (gateway as any).socketUrl ).toBe( 'ws://localhost:1968/opc' );
	});
});

describe( 'Gateway.toValue', ()=>{
	//A pushed Float decoded widened to a double, 21.3 as 21.299999237060547 (historian-web-trend, Tooltip decimals).
	it( 'takes a Float at its shortest', ()=>{
		expect( (<any>Gateway).toValue( {floatValue: Math.fround(21.3)} ) ).toBe( 21.3 );
		expect( (<any>Gateway).toValue( {doubleValue: 0.1+0.2} ) ).toBe( 0.30000000000000004 );//a double is its own
	} );
} );