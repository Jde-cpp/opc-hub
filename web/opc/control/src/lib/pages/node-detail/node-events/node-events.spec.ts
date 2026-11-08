import { TestBed } from '@angular/core/testing';
import { SnackbarService } from 'jde-framework';
import { EventFields } from '../../../model/event';
import { OpcObject, UaNode } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { HistEventsPage } from '../../../services/gateway-service';
import { NodeEvents } from './node-events';
import { flush } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
import { fakeGateway } from 'jde-opc/testing';

//pumpManual with the SubscribeToEvents bit, as the browse answers it
const pump = ( json:object={} )=>new OpcObject( <any>{ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}, eventNotifier: 1, ...json} );
//an event's fields as the gateway's stream delivers them:  the eight base fields, then the selected
const fields = ( id:number, message:string, severity:number, ms:number, extra:unknown[]=[] ):EventFields=><EventFields>[
	Uint8Array.from( [id] ), new NodeId( {i: 2041} ), new NodeId( {ns: 2, i: 5005} ), 'Pump (Manual)',
	{seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, {seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, {text: message}, severity, ...extra
];

describe( 'NodeEvents', ()=>{
	beforeEach( ()=>localStorage.clear() );
	afterEach( ()=>localStorage.clear() );
	//the tab on pumpManual over a gateway whose events stream the test feeds and whose history fill answers `history`
	const open = ( node:UaNode=pump(), history:HistEventsPage={status: 0, events: []} )=>{
		const gateway = fakeGateway( 'gw' );
		gateway.historyEvents.mockImplementation( async ()=>history );
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbar }] });
		const fixture = TestBed.createComponent( NodeEvents );
		fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'cn'}}} );
		fixture.componentRef.setInput( 'node', node );
		fixture.detectChanges();
		const tab = fixture.componentInstance;
		const cells = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )].map( r=>[...r.querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() ) );
		const headers = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-header-cell' )].map( h=>h.textContent.trim() );
		return { fixture, tab, gateway, snackbar, cells, headers, push: ( f:EventFields )=>gateway.events.next( {opcId: 'cn', node: node.nodeId, fields: f} ) };
	};

	it( 'subscribes the node\'s events with no fields beyond the base eight, and lists them as columns but the EventId', ()=>{
		const { gateway, headers } = open();
		expect( gateway.subscribeEvents ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5005} ), [], expect.any( String ) );
		expect( headers() ).toEqual( ['EventType', 'SourceNode', 'SourceName', 'Time', 'ReceiveTime', 'Message', 'Severity'] );
	} );
	it( 'shows each event as a row, newest first, the message and severity as the server gave them', ()=>{
		const { fixture, tab, cells, push } = open();
		push( fields( 1, 'first', 100, 1_700_000_000_000 ) );
		push( fields( 2, 'second', 500, 1_700_000_001_000 ) );
		fixture.detectChanges();
		expect( tab.rows().map( r=>r.key ) ).toEqual( ['02', '01'] );
		expect( cells().map( c=>[c[5], c[6]] ) ).toEqual( [['second', '500'], ['first', '100']] );
		expect( cells()[0][2] ).toBe( 'Pump (Manual)' );
		expect( cells()[0][3] ).toMatch( /^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}$/ );
	} );
	it( 'holds what arrives while paused and adds it on resume, and Clear empties the table', ()=>{
		const { fixture, tab, push } = open();
		push( fields( 1, 'a', 1, 1_700_000_000_000 ) );
		tab.togglePause();
		push( fields( 2, 'b', 1, 1_700_000_001_000 ) );
		fixture.detectChanges();
		expect( tab.rows() ).toHaveLength( 1 );
		expect( tab.held() ).toBe( 1 );
		tab.togglePause();
		expect( tab.rows().map( r=>r.key ) ).toEqual( ['02', '01'] );
		expect( tab.held() ).toBe( 0 );
		tab.clear();
		expect( tab.rows() ).toEqual( [] );
	} );
	it( 'adds a field as a column and subscribes again with it, refuses a path the gateway would, and drops a removed one', async ()=>{
		const { fixture, tab, gateway, snackbar, headers } = open();
		tab.addField( <any>{value: ' Rpm ', chipInput: {clear: ()=>{}}} );
		await flush();
		fixture.detectChanges();
		expect( tab.select() ).toEqual( ['Rpm'] );
		expect( headers().at( -1 ) ).toBe( 'Rpm' );
		expect( gateway.subscribeEvents ).toHaveBeenLastCalledWith( 'cn', expect.anything(), ['Rpm'], expect.any( String ) );
		expect( gateway.events.subscribers ).toHaveLength( 1 );//the first subscription ended for the second
		tab.addField( <any>{value: '70000:x', chipInput: {clear: ()=>{}}} );
		expect( snackbar.warn ).toHaveBeenCalled();
		expect( tab.select() ).toEqual( ['Rpm'] );
		tab.removeField( 'Rpm' );
		await flush();
		expect( tab.select() ).toEqual( [] );
		expect( gateway.subscribeEvents ).toHaveBeenLastCalledWith( 'cn', expect.anything(), [], expect.any( String ) );
	} );
	it( 'refuses a field that repeats a column, however its namespace is spelled', async ()=>{
		const { tab, gateway, snackbar } = open();
		tab.addField( <any>{value: 'Message', chipInput: {clear: ()=>{}}} );
		expect( snackbar.warn ).toHaveBeenCalledTimes( 1 );
		tab.addField( <any>{value: 'Rpm', chipInput: {clear: ()=>{}}} );
		await flush();
		tab.addField( <any>{value: '0:Rpm', chipInput: {clear: ()=>{}}} );
		await flush();
		expect( snackbar.warn ).toHaveBeenCalledTimes( 2 );
		expect( tab.select() ).toEqual( ['Rpm'] );
		expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 2 );
	} );
	it( 'keeps the fields chosen for the node on this browser', async ()=>{
		const first = open();
		first.tab.addField( <any>{value: 'Rpm', chipInput: {clear: ()=>{}}} );
		await flush();
		first.fixture.destroy();
		TestBed.resetTestingModule();//a second tab on the same node, as a return to the page makes
		const second = open();
		await flush();
		expect( second.tab.select() ).toEqual( ['Rpm'] );
		expect( second.gateway.subscribeEvents ).toHaveBeenLastCalledWith( 'cn', expect.anything(), ['Rpm'], expect.any( String ) );
	} );
	it( 'fills the table from the event history, each event once by its EventId, and says what a server without one answered', async ()=>{
		const history:HistEventsPage = { status: 0, events: [fields( 1, 'stored', 50, 1_699_999_000_000 ), fields( 2, 'live too', 60, 1_699_999_500_000 )] };
		const { fixture, tab, gateway, push } = open( pump(), history );
		push( fields( 2, 'live too', 60, 1_699_999_500_000 ) );
		await tab.load();
		fixture.detectChanges();
		expect( gateway.historyEvents ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5005} ), expect.objectContaining( {select: []} ) );
		expect( tab.rows().map( r=>[r.key, r.fields[6]] ) ).toEqual( [['02', {text: 'live too'}], ['01', {text: 'stored'}]] );
		expect( tab.historyStatus() ).toBeUndefined();

		fixture.destroy();
		TestBed.resetTestingModule();//a second tab in one test, against a server with no event history
		const unsupported = open( pump(), {status: 0x80710000, events: []} );
		await unsupported.tab.load();
		expect( unsupported.tab.historyStatus() ).toContain( '0x80710000' );
		expect( unsupported.tab.rows() ).toEqual( [] );
	} );
	it( 'reads on through every page of the history', async ()=>{
		const { tab, gateway } = open();
		gateway.historyEvents.mockReset();
		gateway.historyEvents
			.mockResolvedValueOnce( {status: 0, continuation: 'c1', events: [fields( 1, 'a', 1, 1_700_000_000_000 )]} )
			.mockResolvedValueOnce( {status: 0, events: [fields( 2, 'b', 1, 1_700_000_001_000 )]} );
		await tab.load();
		expect( gateway.historyEvents.mock.calls.map( c=>c[2].continuation ) ).toEqual( [undefined, 'c1'] );
		expect( tab.rows().map( r=>r.key ) ).toEqual( ['02', '01'] );
		expect( tab.loading() ).toBe( false );
	} );
	it( 'drops a page read for the field list before a chip change', async ()=>{
		const { tab, gateway } = open();
		tab.select.set( ['A', 'B'] );
		await flush();
		let answer!:( p:HistEventsPage )=>void;
		gateway.historyEvents.mockImplementation( ()=>new Promise<HistEventsPage>( r=>answer = r ) );
		const loading = tab.load();
		tab.removeField( 'A' );
		await flush();
		answer( {status: 0, continuation: 'c1', events: [fields( 1, 'x', 1, 1_700_000_000_000, ['a', 'b'] )]} );
		await loading;
		expect( tab.rows() ).toEqual( [] );
		expect( gateway.historyEvents ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'holds at most maxRows while paused, dropping the oldest', ()=>{
		const { tab, push } = open();
		tab.togglePause();
		for( let i=0; i<NodeEvents.maxRows+2; ++i )
			push( fields( i%250, `m${i}`, 1, 1_700_000_000_000+i ) );
		expect( tab.held() ).toBe( NodeEvents.maxRows );
	} );
	it( 'shows a Time that is no date as its value and does not stop rendering', ()=>{
		const { fixture, cells, push } = open();
		const f = fields( 1, 'a', 1, 1_700_000_000_000 );
		f[4] = 'not a date';
		f[5] = 2150891520;
		push( f );
		fixture.detectChanges();
		expect( cells()[0][3] ).toBe( 'not a date' );
		expect( cells()[0][4] ).toBe( '2150891520' );
	} );
	it( 'shows a refused change of the field list and stays subscribed', async ()=>{
		const { fixture, tab, gateway, cells, push } = open();
		gateway.events.next( {opcId: 'cn', node: tab.node().nodeId, fields: [], refused: 0x80450000} );
		await flush();
		expect( tab.error() ).toContain( '0x80450000' );
		expect( tab.rows() ).toEqual( [] );
		expect( tab.live() ).toBe( true );
		push( fields( 1, 'a', 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		expect( cells() ).toHaveLength( 1 );
	} );
	it( 'shows what the gateway said when it refuses, and does not retry', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway } = open();
			gateway.events.error( {error: {requestId: 1, message: "The event field 'Foo:Bar' has a bad index.", sc: 0, httpStatus: 400}} );
			expect( tab.error() ).toContain( 'Foo:Bar' );
			expect( tab.error() ).not.toContain( 'lost' );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 1 );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'shows what the gateway said when it could not subscribe yet, and tries again', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway } = open();
			gateway.events.error( {error: {requestId: 1, message: "Send sessionId before subscribing.", sc: 2083611259, httpStatus: 500}} );
			expect( tab.error() ).toContain( 'Send sessionId' );
			expect( tab.error() ).toContain( 'Trying again' );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 2 );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'shows the status a refused subscription ends with, and no empty-table notice', async ()=>{
		const { fixture, tab, gateway } = open();
		gateway.events.error( new OpcError( 0x80450000, 'SubscribeEvents', '', undefined ) );
		await flush();
		fixture.detectChanges();
		expect( tab.error() ).toContain( '0x80450000' );
		expect( tab.live() ).toBe( false );
		expect( fixture.nativeElement.querySelector( '.jde-empty' ) ).toBeNull();
		expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 1 );//a refusal is not retried
	} );
	it( 'subscribes again after the socket went', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway } = open();
			gateway.events.error( new Error( 'Connection to the gateway was lost.' ) );
			expect( tab.error() ).toContain( 'lost' );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 2 );
			expect( tab.live() ).toBe( true );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'starts over on another node', async ()=>{
		const { fixture, tab, gateway, push } = open();
		push( fields( 1, 'a', 1, 1_700_000_000_000 ) );
		fixture.componentRef.setInput( 'node', new OpcObject( <any>{ns: 0, i: 2253, name: 'Server', browse: {ns: 0, name: 'Server'}, eventNotifier: 1} ) );
		fixture.detectChanges();
		await flush();
		expect( tab.rows() ).toEqual( [] );
		expect( gateway.subscribeEvents ).toHaveBeenLastCalledWith( 'cn', expect.objectContaining( {ns: 0, id: 2253} ), [], expect.any( String ) );
	} );
} );