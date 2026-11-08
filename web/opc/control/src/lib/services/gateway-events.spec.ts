import { NodeId } from '../model/node-id';
import { OpcError } from '../model/opc-error';
import { EventPush, Gateway } from './gateway-service';
import { ProtoUtils } from 'jde-framework';
import { flush } from 'jde-spa/testing';
import { TestGateway } from 'jde-opc/testing';

const opcId = 'local', pump = new NodeId( {ns: 2, i: 5005} );
//TestGateway answering its subscribes at once, and what it sent of each kind.
class AckingGateway extends TestGateway{
	override acking = true;
	sentOf( key:string ):any[]{ return this.sent.mock.calls.map( ([m])=>m ).filter( m=>m[key] ).map( m=>m[key] ); }
}
//the socket's EventValues for pump:  the eight base fields, then the selected
const eventValues = ( message:string, severity:number, extra:object[]=[] )=>({ opcId, node: {namespaceIndex: 2, numeric: 5005}, fields: [
	{byteString: Uint8Array.from( [1, 2] )}, {node: {namespaceIndex: 0, numeric: 2041}}, {node: {namespaceIndex: 2, numeric: 5005}}, {stringValue: 'Pump (Manual)'},
	{date: new Date( 1_700_000_000_250 )}, {date: new Date( 1_700_000_000_300 )}, {localizedText: {text: message}}, {uint16: severity}, ...extra
] });

//An events subscription over the socket (clients spec *Events*, #424):  SubscribeEvents out, EventValues in, each to the owner's stream.
describe( 'Gateway.subscribeEvents', ()=>{
	const subscribed = async ( select:string[]=['Rpm'] )=>{
		const gateway = new AckingGateway();
		const pushes:EventPush[] = [];
		let errored:unknown;
		gateway.statusCodes = [undefined];
		const subscription = gateway.subscribeEvents( opcId, pump, select, 'tab' ).subscribe( {next: e=>pushes.push( e ), error: e=>errored = e} );
		await flush();
		return { gateway, pushes, subscription, get errored(){ return errored; } };
	};
	it( 'sends SubscribeEvents with the node and the fields selected', async ()=>{
		const { gateway } = await subscribed( ['Rpm', '2:Foo'] );
		expect( gateway.sentOf( 'subscribeEvents' ) ).toEqual( [{opcId, node: {namespaceIndex: 2, numeric: 5005}, select: ['Rpm', '2:Foo']}] );
	} );
	it( 'delivers an EventValues push as the fields in the list\'s order, an empty Value as undefined', async ()=>{
		const { gateway, pushes } = await subscribed();
		gateway.receive( {eventValues: eventValues( 'hello', 500, [{doubleValue: 1480.5}] )}, {eventValues: eventValues( 'later', 1, [{}] )} );
		expect( pushes ).toHaveLength( 2 );
		expect( pushes[0].node.equals( pump ) ).toBe( true );
		expect( pushes[0].fields ).toHaveLength( 9 );
		expect( pushes[0].fields[0] ).toEqual( Uint8Array.from( [1, 2] ) );
		expect( pushes[0].fields[3] ).toBe( 'Pump (Manual)' );
		expect( ProtoUtils.toDate( <any>pushes[0].fields[4] )?.getTime() ).toBe( 1_700_000_000_250 );//the {seconds, nanos} the Value pipeline carries a DateTime as
		expect( pushes[0].fields[6] ).toBe( 'hello' );
		expect( pushes[0].fields[7] ).toBe( 500 );
		expect( pushes[0].fields[8] ).toBe( 1480.5 );
		expect( pushes[1].fields[8] ).toBeUndefined();
	} );
	it( 'errors the stream with the status the ack refused the item with, and sends no unsubscribe for it', async ()=>{
		const gateway = new AckingGateway();
		gateway.statusCodes = [0x80450000];//Bad_FilterNotAllowed
		let errored:unknown;
		const subscription = gateway.subscribeEvents( opcId, pump, [], 'tab' ).subscribe( {next: ()=>{}, error: e=>errored = e} );
		await flush();
		expect( errored ).toBeInstanceOf( OpcError );
		expect( (<OpcError>errored).sc ).toBe( 0x80450000 );
		subscription.unsubscribe();
		expect( gateway.sentOf( 'unsubscribeEvents' ) ).toEqual( [] );
	} );
	it( 'sends UnsubscribeEvents when the stream\'s observer leaves, once no other owner holds the node', async ()=>{
		const { gateway, subscription } = await subscribed();
		const other = gateway.subscribeEvents( opcId, pump, [], 'other' ).subscribe( {next: ()=>{}, error: ()=>{}} );
		await flush();
		subscription.unsubscribe();
		expect( gateway.sentOf( 'unsubscribeEvents' ) ).toEqual( [] );
		other.unsubscribe();
		expect( gateway.sentOf( 'unsubscribeEvents' ) ).toEqual( [{opcId, node: {namespaceIndex: 2, numeric: 5005}}] );
	} );
	it( 'ends the stream when the socket goes, so the tab subscribes again', async ()=>{
		const s = await subscribed();
		s.gateway.drop();
		expect( s.errored ).toBeDefined();
		expect( s.errored ).not.toBeInstanceOf( OpcError );
	} );
} );

//historyEvents( opc, node, start, end, select, limit, continuation ){ status continuation events{ fields } } (hist.jsonnet)
describe( 'Gateway.historyEvents', ()=>{
	it( 'names only the arguments given, the start and the fields always', ()=>{
		const start = new Date( 1_700_000_000_000 );
		const { ql, vars } = Gateway.historyEventsQuery( opcId, pump, {start, select: ['Rpm']} );
		expect( ql ).toBe( 'historyEvents( opc: $opc, node: $node, start: $start, select: $select ){ status continuation events{ fields } }' );
		expect( vars ).toEqual( {opc: opcId, node: {ns: 2, i: 5005}, start: {seconds: 1_700_000_000, nanos: 0}, select: ['Rpm']} );
		const more = Gateway.historyEventsQuery( opcId, pump, {start, end: new Date( 1_700_000_001_000 ), select: [], limit: 50, continuation: '133'} );
		expect( more.ql ).toContain( 'end: $end, limit: $limit, continuation: $continuation' );
		expect( more.vars ).toMatchObject( {limit: 50, continuation: '133'} );
	} );
	it( 'reads the answer\'s status, continuation and each event\'s fields, a null field undefined', async ()=>{
		const gateway = new TestGateway();
		gateway.posted = { data: { historyEvents: { status: 0x80710000, continuation: null, events: [{fields: ['0102', {i: 2041}, {ns: 2, i: 5005}, 'Pump (Manual)', {seconds: 1, nanos: 0}, {seconds: 1, nanos: 0}, {text: 'hi'}, 7, null]}] } } };//what postQL unwraps
		const page = await gateway.historyEvents( opcId, pump, {start: new Date( 0 ), select: ['Rpm']} );
		expect( page.status ).toBe( 0x80710000 );
		expect( page.continuation ).toBeUndefined();
		expect( page.events ).toHaveLength( 1 );
		expect( page.events[0][6] ).toEqual( {text: 'hi'} );
		expect( page.events[0][7] ).toBe( 7 );
		expect( page.events[0][8] ).toBeUndefined();
	} );
} );