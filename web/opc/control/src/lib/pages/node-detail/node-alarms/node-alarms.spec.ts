import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { SnackbarService } from 'jde-framework';
import { badEventIdUnknown, conditionSelect } from '../../../model/alarm';
import { EventFields } from '../../../model/event';
import { OpcObject, UaNode } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { NodeAlarms } from './node-alarms';
import { AlarmCommentDialog } from './alarm-comment-dialog';
import { flush } from 'jde-spa/testing';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { fakeGateway } from 'jde-opc/testing';

//the Server object, which gets the refresh bracket
const server = ( json:object={} )=>new OpcObject( <any>{ns: 0, i: 2253, name: 'Server', browse: {ns: 0, name: 'Server'}, eventNotifier: 1, ...json} );
const overspeed = new NodeId( {ns: 2, i: 5020} );
const condition = ( id:number, ms:number, {active=true, acked=false, confirmed=false, retain=true, comment=''}={} ):EventFields=><EventFields>[
	Uint8Array.from( [id] ), new NodeId( {i: 2915} ), new NodeId( {ns: 2, i: 5005} ), 'Pump (Manual)',
	{seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, {seconds: Math.floor( ms/1000 ), nanos: ms%1000*1_000_000}, 'too fast', 700,
	overspeed, 'overspeed', undefined, retain, 'Enabled', true, active ? 'Active' : 'Inactive', active, acked ? 'Acknowledged' : 'Unacknowledged', acked, confirmed ? 'Confirmed' : 'Unconfirmed', confirmed, comment
];
const bracket = ( type:number, ms:number ):EventFields=><EventFields>[Uint8Array.from( [type] ), new NodeId( {i: type} ), new NodeId( {i: 2253} ), 'Server', {seconds: ms/1000, nanos: 0}, {seconds: ms/1000, nanos: 0}, undefined, 100];

describe( 'NodeAlarms', ()=>{
	//the tab on the Server object over a gateway whose events stream the test feeds, the comment dialog answering `comment` - null, cancelled
	const open = ( node:UaNode=server(), comment:string|null='seen' )=>{
		const gateway = fakeGateway( 'gw' );
		const snackbar = snackbarStub(), dialog = dialogStub( comment ?? undefined );
		TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbar }, { provide: MatDialog, useValue: dialog }] });
		const fixture = TestBed.createComponent( NodeAlarms );
		fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'cn'}}} );
		fixture.componentRef.setInput( 'node', node );
		fixture.detectChanges();
		const tab = fixture.componentInstance;
		const cells = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )].map( r=>[...r.querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() ) );
		const push = ( f:EventFields )=>gateway.events.next( {opcId: 'cn', node: node.nodeId, fields: f} );
		const acked = ()=>gateway.events.next( {opcId: 'cn', node: node.nodeId, fields: [], acked: true} );
		const restored = ()=>gateway.events.next( {opcId: 'cn', node: node.nodeId, fields: [], restored: true} );
		return { fixture, tab, gateway, snackbar, dialog, cells, push, acked, restored };
	};

	it( 'subscribes the node\'s events with the condition fields, and refreshes once the ack comes', async ()=>{
		const { gateway, acked } = open();
		expect( gateway.subscribeEvents ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 0, id: 2253} ), [...conditionSelect], expect.any( String ) );
		expect( gateway.refreshConditions ).not.toHaveBeenCalled();
		acked();
		await flush();
		expect( gateway.refreshConditions ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 0, id: 2253} ) );
	} );
	it( 'shows a condition\'s latest event as its row, the states as the server spells them, and drops it when Retain goes', ()=>{
		const { fixture, tab, cells, push } = open();
		push( condition( 1, 1_700_000_000_000 ) );
		push( condition( 2, 1_700_000_001_000, {acked: true, comment: 'seen'} ) );
		fixture.detectChanges();
		expect( tab.rows() ).toHaveLength( 1 );
		expect( cells()[0].slice( 0, 10 ) ).toEqual( ['Pump (Manual)', 'overspeed', '', '700', 'too fast', 'Enabled', 'Active', 'Acknowledged', 'Unconfirmed', 'seen'] );
		push( condition( 3, 1_700_000_002_000, {active: false, acked: true, confirmed: true, retain: false} ) );
		fixture.detectChanges();
		expect( tab.rows() ).toEqual( [] );
		expect( fixture.nativeElement.querySelector( '.jde-empty' ) ).not.toBeNull();
	} );
	it( 'brackets the replay:  Refreshing… between RefreshStart and RefreshEnd, and a row not replayed goes', async ()=>{
		const { fixture, tab, push, acked } = open();
		acked();
		await flush();
		push( condition( 1, 1_700_000_000_000 ) );
		push( bracket( 2787, 1_700_000_001_000 ) );
		fixture.detectChanges();
		expect( tab.refreshing() ).toBe( true );
		expect( fixture.nativeElement.textContent ).toContain( 'Refreshing' );
		push( bracket( 2788, 1_700_000_002_000 ) );
		fixture.detectChanges();
		expect( tab.refreshing() ).toBe( false );
		expect( tab.rows() ).toEqual( [] );
	} );
	it( 'acknowledges through call on the condition node with the EventId and the comment, Confirm once acknowledged', async ()=>{
		const { fixture, tab, gateway, dialog, push } = open();
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		const row = tab.rows()[0];
		expect( tab.canAcknowledge( row ) ).toBe( true );
		expect( tab.canConfirm( row ) ).toBe( false );
		await tab.acknowledge( row );
		expect( dialog.open ).toHaveBeenCalledWith( AlarmCommentDialog, expect.objectContaining( {data: expect.objectContaining( {action: 'Acknowledge', condition: 'overspeed'} )} ) );
		expect( gateway.call ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5020} ), expect.objectContaining( {ns: 0, id: 9111} ), [Uint8Array.from( [1] ), {text: 'seen'}] );
		push( condition( 2, 1_700_000_001_000, {acked: true, comment: 'seen'} ) );
		const acked = tab.rows()[0];
		expect( tab.canAcknowledge( acked ) ).toBe( false );
		expect( tab.canConfirm( acked ) ).toBe( true );
		await tab.confirm( acked );
		expect( gateway.call ).toHaveBeenLastCalledWith( 'cn', expect.anything(), expect.objectContaining( {ns: 0, id: 9113} ), [Uint8Array.from( [2] ), {text: 'seen'}] );
		await tab.comment( acked );
		expect( gateway.call ).toHaveBeenLastCalledWith( 'cn', expect.anything(), expect.objectContaining( {ns: 0, id: 9029} ), [Uint8Array.from( [2] ), {text: 'seen'}] );
	} );
	it( 'sends nothing when the dialog is cancelled', async ()=>{
		const { fixture, tab, gateway, push } = open( server(), null );
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		await tab.acknowledge( tab.rows()[0] );
		expect( gateway.call ).not.toHaveBeenCalled();
	} );
	it( 'shows a refusal\'s status on the row until the condition\'s next event, and a failure before the call in the snackbar', async ()=>{
		const { fixture, tab, gateway, snackbar, cells, push } = open();
		gateway.call.mockResolvedValueOnce( {statusCode: 0x80D50000, inputArgumentResults: [], outputArguments: []} );
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		await tab.acknowledge( tab.rows()[0] );
		fixture.detectChanges();
		expect( tab.rows()[0].problem ).toContain( '0x80D50000' );
		expect( cells()[0][4] ).toContain( 'Acknowledge refused' );
		push( condition( 2, 1_700_000_001_000, {acked: true} ) );
		expect( tab.rows()[0].problem ).toBeUndefined();
		gateway.call.mockRejectedValueOnce( new Error( 'no session' ) );
		await tab.comment( tab.rows()[0] );
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not comment the condition.', expect.any( Error ) );
	} );
	it( 'shows the status the server answered the refresh with, and what a failed refresh threw', async ()=>{
		const { tab, gateway, snackbar } = open();
		gateway.refreshConditions.mockResolvedValueOnce( 0x80340000 );
		await tab.refresh();
		expect( tab.refreshStatus() ).toContain( '0x80340000' );
		gateway.refreshConditions.mockRejectedValueOnce( new Error( 'No events subscription on i=2253 to refresh.' ) );
		await tab.refresh();
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not refresh the conditions.', expect.any( Error ) );
		expect( tab.busy() ).toBe( false );
	} );
	it( 'shows the status a refused subscription ends with, and subscribes again after the socket went', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway } = open();
			gateway.events.error( new OpcError( 0x80450000, 'SubscribeEvents', '', undefined ) );
			expect( tab.error() ).toContain( '0x80450000' );
			expect( tab.live() ).toBe( false );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 1 );//a refusal is not retried
		}
		finally{
			vi.useRealTimers();
		}
		vi.useFakeTimers();
		try{
			TestBed.resetTestingModule();//a second tab, whose socket goes
			const { tab, gateway, acked } = open();
			gateway.events.error( new Error( 'Connection to the gateway was lost.' ) );
			expect( tab.error() ).toContain( 'lost' );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( gateway.subscribeEvents ).toHaveBeenCalledTimes( 2 );
			expect( tab.live() ).toBe( false );
			acked();
			expect( tab.live() ).toBe( true );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'is live once the ack comes, and Refresh waits for it', async ()=>{
		const { fixture, tab, acked } = open();
		fixture.detectChanges();
		const button = ()=><HTMLButtonElement>fixture.nativeElement.querySelector( 'mat-toolbar button' );
		expect( tab.live() ).toBe( false );
		expect( button().disabled ).toBe( true );
		acked();
		await flush();
		fixture.detectChanges();
		expect( tab.live() ).toBe( true );
		expect( button().disabled ).toBe( false );
	} );
	it( 'keeps the rows through a lost socket and starts over from the refresh when the item is back', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway, push, acked } = open();
			acked();
			await vi.advanceTimersByTimeAsync( 0 );
			push( condition( 1, 1_700_000_000_000 ) );
			push( bracket( 2787, 1_700_000_001_000 ) );
			expect( tab.refreshing() ).toBe( true );
			gateway.events.error( new Error( 'Connection to the gateway was lost.' ) );
			expect( tab.refreshing() ).toBe( false );
			await vi.advanceTimersByTimeAsync( 6_000 );
			expect( tab.rows() ).toHaveLength( 1 );
			gateway.refreshConditions.mockClear();
			acked();
			expect( tab.rows() ).toEqual( [] );
			await vi.advanceTimersByTimeAsync( 0 );
			expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 1 );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'starts over from the refresh when a rebuild of the server\'s session puts the item back', async ()=>{
		const { tab, gateway, push, acked, restored } = open();
		acked();
		await flush();
		push( bracket( 2787, 1_700_000_000_000 ) );
		push( condition( 1, 1_700_000_001_000 ) );
		expect( tab.refreshing() ).toBe( true );
		gateway.refreshConditions.mockClear();
		restored();
		expect( tab.rows() ).toEqual( [] );
		expect( tab.refreshing() ).toBe( false );
		await flush();
		expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'refreshes a new node even while the old node\'s refresh is out, and says nothing of the old one failing', async ()=>{
		const { fixture, tab, gateway, snackbar, acked } = open();
		let fail!:( e:Error )=>void;
		gateway.refreshConditions.mockImplementationOnce( ()=>new Promise<number>( ( _, reject )=>fail = reject ) );
		acked();
		expect( tab.busy() ).toBe( true );
		fixture.componentRef.setInput( 'node', new OpcObject( <any>{ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}, eventNotifier: 1} ) );
		fixture.detectChanges();
		await flush();
		acked();
		expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 1 );
		fail( new Error( 'no session' ) );
		await flush();
		expect( snackbar.exception ).not.toHaveBeenCalled();
		expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 2 );
		expect( gateway.refreshConditions ).toHaveBeenLastCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5005} ) );
		expect( tab.busy() ).toBe( false );
	} );
	it( 'names a refusal\'s status once the names arrive, but never over the row\'s next event', async ()=>{
		const { fixture, tab, gateway, push } = open();
		const refused = {statusCode: 0x80D70000, inputArgumentResults: [], outputArguments: []};
		let named!:()=>void;
		gateway.updateErrorCodes.mockImplementationOnce( ()=>new Promise<void>( r=>named = r ) );
		gateway.call.mockResolvedValueOnce( refused );
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		await tab.acknowledge( tab.rows()[0] );
		push( condition( 2, 1_700_000_001_000, {acked: true} ) );
		named();
		await flush();
		expect( tab.rows()[0].problem ).toBeUndefined();

		gateway.updateErrorCodes.mockImplementationOnce( async ()=>OpcError.setMessages( [{sc: 0x80D80000, message: 'Bad_Named'}] ) );
		gateway.call.mockResolvedValueOnce( {...refused, statusCode: 0x80D80000} );
		await tab.comment( tab.rows()[0] );
		await flush();
		expect( tab.rows()[0].problem ).toContain( 'Bad_Named' );
	} );
	it( 'brings the dialog back with the comment when the condition moved on, and only then sends the new EventId', async ()=>{
		const { fixture, tab, gateway, dialog, push } = open();
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		gateway.call.mockImplementationOnce( async ()=>{
			push( condition( 2, 1_700_000_001_000 ) );
			return {statusCode: badEventIdUnknown, inputArgumentResults: [], outputArguments: []};
		} );
		await tab.acknowledge( tab.rows()[0] );
		expect( dialog.open ).toHaveBeenCalledTimes( 2 );
		expect( dialog.open ).toHaveBeenLastCalledWith( AlarmCommentDialog, expect.objectContaining( {data: expect.objectContaining( {comment: 'seen'} )} ) );
		expect( gateway.call ).toHaveBeenCalledTimes( 2 );
		expect( gateway.call ).toHaveBeenLastCalledWith( 'cn', expect.anything(), expect.anything(), [Uint8Array.from( [2] ), {text: 'seen'}] );
	} );
	it( 'shows the refusal when the condition moved on to where the action no longer applies', async ()=>{
		const { fixture, tab, gateway, dialog, push } = open();
		push( condition( 1, 1_700_000_000_000 ) );
		fixture.detectChanges();
		gateway.call.mockImplementationOnce( async ()=>{
			push( condition( 2, 1_700_000_001_000, {acked: true} ) );
			return {statusCode: badEventIdUnknown, inputArgumentResults: [], outputArguments: []};
		} );
		await tab.acknowledge( tab.rows()[0] );
		expect( dialog.open ).toHaveBeenCalledTimes( 1 );
		expect( tab.rows()[0].problem ).toContain( '0x809A0000' );
	} );
	it( 'runs the refresh a server asks for, keeping the table and adding no row', async ()=>{
		const { tab, gateway, push, acked } = open();
		acked();
		await flush();
		push( condition( 1, 1_700_000_000_000 ) );
		gateway.refreshConditions.mockClear();
		push( bracket( 2789, 1_700_000_001_000 ) );
		await flush();
		expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 1 );
		expect( tab.rows() ).toHaveLength( 1 );
		expect( tab.incomplete() ).toBe( false );
	} );
	it( 'follows a server\'s asks at most once per interval, and says the list may be short for the rest', async ()=>{
		vi.useFakeTimers();
		try{
			const { tab, gateway, push, acked } = open();
			acked();
			await vi.advanceTimersByTimeAsync( 0 );
			gateway.refreshConditions.mockClear();
			push( bracket( 2789, 1_700_000_000_000 ) );
			await vi.advanceTimersByTimeAsync( 0 );
			push( bracket( 2789, 1_700_000_001_000 ) );
			await vi.advanceTimersByTimeAsync( 0 );
			expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 1 );
			expect( tab.incomplete() ).toBe( true );
			await vi.advanceTimersByTimeAsync( NodeAlarms.refreshRequiredGap );
			push( bracket( 2789, 1_700_000_012_000 ) );
			await vi.advanceTimersByTimeAsync( 0 );
			expect( gateway.refreshConditions ).toHaveBeenCalledTimes( 2 );
			expect( tab.incomplete() ).toBe( false );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	it( 'says a queue overflow may have shortened the list, with no refresh of its own, until the next refresh', async ()=>{
		const { fixture, tab, gateway, push, acked } = open();
		acked();
		await flush();
		gateway.refreshConditions.mockClear();
		push( bracket( 3035, 1_700_000_000_000 ) );
		await flush();
		fixture.detectChanges();
		expect( gateway.refreshConditions ).not.toHaveBeenCalled();
		expect( tab.rows() ).toEqual( [] );
		expect( fixture.nativeElement.textContent ).toContain( 'may be incomplete' );
		await tab.refresh();
		fixture.detectChanges();
		expect( fixture.nativeElement.textContent ).not.toContain( 'may be incomplete' );
	} );
	it( 'starts over on another node', async ()=>{
		const { fixture, tab, gateway, push } = open();
		push( condition( 1, 1_700_000_000_000 ) );
		expect( tab.rows() ).toHaveLength( 1 );
		fixture.componentRef.setInput( 'node', new OpcObject( <any>{ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}, eventNotifier: 1} ) );
		fixture.detectChanges();
		await flush();
		expect( tab.rows() ).toEqual( [] );
		expect( gateway.subscribeEvents ).toHaveBeenLastCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5005} ), [...conditionSelect], expect.any( String ) );
	} );
} );