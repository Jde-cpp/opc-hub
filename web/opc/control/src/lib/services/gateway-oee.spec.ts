import { OeeLiveEvent } from '../model/oee-live';
import { Gateway } from './gateway-service';
import { TestGateway } from 'jde-opc/testing';

//TestGateway's socket:  the OEE subscriptions it sent, and the cancellations.
class OeeGateway extends TestGateway{
	get subscribes(){ return this.#wired().filter( s=>s.m.oeeSubscribe ); }
	get unsubscribes(){ return this.#wired().filter( s=>s.m.oeeUnsubscribe ); }
	#wired(){ return this.wire.mock.calls.map( ([requestId, m])=>({requestId, m}) ); }
}
const listen = ( gateway:OeeGateway, machine=4 )=>{
	const events:OeeLiveEvent[] = [];
	let error:any;
	const subscription = gateway.oeeSubscribe( machine ).subscribe( {next: e=>events.push( e ), error: e=>error = e} );
	const requestId = gateway.subscribes.at( -1 )!.requestId;
	return { events, subscription, requestId, get error(){ return error; } };
};
const since = new Date( Date.UTC( 2026, 10, 11, 8, 12 ) );
const status = ( state:number, extra:object={} )=>({ state, since, planned: true, calendarMissing: false, ...extra });

describe( 'Gateway.oeeSubscribe', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );

	it( "sends OeeSubscribe with the machine, and delivers the ack's status, then each pushed under its id", ()=>{
		const gateway = new OeeGateway();
		const s = listen( gateway );
		expect( gateway.subscribes.map( x=>x.m.oeeSubscribe ) ).toEqual( [{machine: 4, line: 0}] );
		gateway.receive( {requestId: s.requestId, oeeSubscribeAck: {status: status( 1 )}} );
		gateway.receive( {requestId: s.requestId, oeeStatus: status( 2, {stop: {start: since, small: true}} )} );
		expect( s.events.map( e=>e.kind=='status' && e.status.state ) ).toEqual( ['running', 'stopped'] );
		expect( s.events[1] ).toMatchObject( {kind: 'status', status: {since, stop: {start: since, small: true, reason: undefined}}} );
		s.subscription.unsubscribe();
	} );
	it( 'keeps two machines apart by their ids', ()=>{
		const gateway = new OeeGateway();
		const a = listen( gateway, 4 ), b = listen( gateway, 5 );
		gateway.receive( {requestId: b.requestId, oeeSubscribeAck: {status: status( 2 )}} );
		expect( [a.events.length, b.events.length] ).toEqual( [0, 1] );
		a.subscription.unsubscribe();
		b.subscription.unsubscribe();
	} );
	it( 'sends OeeUnsubscribe naming the subscription when the listener goes, and drops what is still on the wire', ()=>{
		const gateway = new OeeGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, oeeSubscribeAck: {status: status( 1 )}} );
		s.subscription.unsubscribe();
		expect( gateway.unsubscribes.map( x=>x.m.oeeUnsubscribe ) ).toEqual( [{subscription: s.requestId}] );
		expect( gateway.unsubscribes[0].requestId ).not.toBe( s.requestId );
		gateway.receive( {requestId: s.requestId, oeeStatus: status( 2 )} );
		expect( s.events ).toHaveLength( 1 );
	} );
	//the gateway ends a subscription with an exception under its id (spec *Live*):  the machine deleted, OEE stopping
	it( 'ends on an exception under the id, with its status, and unsubscribes nothing', ()=>{
		const gateway = new OeeGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, oeeSubscribeAck: {status: status( 1 )}} );
		gateway.receive( {requestId: s.requestId, exception: {what: "Machine 'Pump 1' was deleted.", statusCode: 404}} );
		expect( s.error ).toEqual( {error: {requestId: s.requestId, message: "Machine 'Pump 1' was deleted.", sc: 0, httpStatus: 404}} );
		s.subscription.unsubscribe();
		expect( gateway.unsubscribes ).toHaveLength( 0 );
	} );
	it( 'ends on a refusal of the OeeSubscribe itself', ()=>{
		const gateway = new OeeGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, exception: {what: "No Read on the group.", statusCode: 403}} );
		expect( s.error?.error?.httpStatus ).toBe( 403 );
		expect( s.events ).toEqual( [] );
	} );

	describe( 'after a drop', ()=>{
		beforeEach( ()=>vi.useFakeTimers() );
		afterEach( ()=>vi.useRealTimers() );
		it( 'says so once, subscribes again under a new id, and its ack follows', ()=>{
			const gateway = new OeeGateway();
			const s = listen( gateway );
			gateway.receive( {requestId: s.requestId, oeeSubscribeAck: {status: status( 1 )}} );
			gateway.drop();
			expect( s.events.at( -1 ) ).toEqual( {kind: 'dropped'} );
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			gateway.drop();//the retry's connect failed too
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 3 );
			expect( s.events.filter( e=>e.kind=='dropped' ) ).toHaveLength( 1 );
			const again = gateway.subscribes[2];
			expect( again.requestId ).not.toBe( s.requestId );
			gateway.receive( {requestId: s.requestId, oeeStatus: status( 2 )} );//the old id's:  no subscription
			gateway.receive( {requestId: again.requestId, oeeSubscribeAck: {status: status( 2 )}} );
			expect( s.events.map( e=>e.kind=='status' ? e.status.state : e.kind ) ).toEqual( ['running', 'dropped', 'stopped'] );
			s.subscription.unsubscribe();
			expect( gateway.unsubscribes.map( x=>x.m.oeeUnsubscribe ) ).toEqual( [{subscription: again.requestId}] );
		} );
		it( 'sends nothing for a listener that goes while waiting, and tries no more', ()=>{
			const gateway = new OeeGateway();
			const s = listen( gateway );
			gateway.drop();
			s.subscription.unsubscribe();
			vi.advanceTimersByTime( 2*Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 1 );
			expect( gateway.unsubscribes ).toHaveLength( 0 );
		} );
		it( 'subscribes no more for a listener that goes on hearing of the drop', ()=>{
			const gateway = new OeeGateway();
			const subscription = gateway.oeeSubscribe( 4 ).subscribe( e=>{ if( e.kind=='dropped' ) subscription.unsubscribe(); } );
			gateway.drop();
			vi.advanceTimersByTime( 2*Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 1 );
			expect( gateway.unsubscribes ).toHaveLength( 0 );
		} );
	} );
} );