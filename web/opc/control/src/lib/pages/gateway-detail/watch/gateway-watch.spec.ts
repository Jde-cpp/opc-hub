import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { ProfileStore, RouteItem } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { NodeId } from '../../../model/node-id';
import { Variable } from '../../../model/node';
import { WatchItemJson, WatchRow } from '../../../model/watch';
import { WatchParameters } from '../../../model/watch';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OpcStore } from '../../../services/opc-store';
import { SubscriptionResult } from '../../../services/gateway-service';
import { GatewayWatch } from './gateway-watch';
import { WatchAddDialog } from './watch-add-dialog';
import { WatchParametersDialog } from './watch-parameters-dialog';
import { flush, pageTitleStub, profileStoreStub, provideRoute, screenSize } from 'jde-spa/testing';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { Feed, nodeA as A, nodeB as B } from 'jde-opc/testing';

const C = new NodeId( {ns: 2, i: 3} );
const owner = 'watch/g';
const item = ( opc:string, node:NodeId, parameters:WatchParameters={} ):WatchItemJson=>({opc, node: node.toJson(), parameters});
type Asked = { opc:string; nodes:NodeId[]; parameters?:WatchParameters[] };

//Clients 6b (#419, spec *Watch list*):  the page over a gateway that describes the nodes it is given, records what it is asked to
//subscribe, and feeds the stream;  the profile store holds `items`, and `saved` reads what the page wrote back.
const create = async ( items:WatchItemJson[], described:Map<string,{node:Variable; path:string|null}|undefined> )=>{
	const values = new Feed<SubscriptionResult, Asked>();
	const gateway = {
		slug: 'g',
		nodeById: vi.fn( async ( opc:string, id:NodeId )=>described.get( `${opc}/${id.uaString()}` ) ),
		subscribe: vi.fn( ( opc:string, nodes:NodeId[], _owner:string, parameters?:WatchParameters[] )=>values.open( {opc, nodes, parameters} ) ),
		addToSubscription: vi.fn( ( _opc:string, _nodes:NodeId[], _owner:string, _parameters?:WatchParameters[] )=>{} ),
		unsubscribe: vi.fn( async ( _opc:string, _nodes:NodeId[], _owner:string )=>{} ),
		updateErrorCodes: vi.fn( async ()=>{} )
	};
	const snackbar = snackbarStub();
	const profile = profileStoreStub( {'gateways/g/watch': {items}} );
	const dialog = dialogStub();
	TestBed.configureTestingModule( {providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the zoneless fixture's own first change detection would run ngOnInit a second time
		screenSize(),
		pageTitleStub,
		provideRoute( {data: {pageData: {gateway, routing: new RouteItem( {path: 'watch', title: 'Watch list'} )}}} ),
		{ provide: SnackbarService, useValue: snackbar },
		{ provide: ProfileStore, useValue: profile },
		{ provide: HistAdminService, useValue: {connections: vi.fn( async ()=>[{id: 1, slug: 'local', name: 'Local'}, {id: 2, slug: 'plc', name: 'PLC'}] )} },
		{ provide: OpcStore, useValue: {cnnctnName: ( _g:string, slug:string )=>slug} },
		{ provide: MatDialog, useValue: dialog }
	]} );
	const fixture = TestBed.createComponent( GatewayWatch );
	const page = fixture.componentInstance;
	await page.ngOnInit();
	await flush( 3 );
	const saved = ()=>profile.save.mock.calls.filter( ([key])=>key=='gateways/g/watch' ).map( ([, v])=>(<any>v).items as WatchItemJson[] );
	const row = ( opc:string, node:NodeId )=>page.rows().find( r=>r.key==WatchRow.keyOf( opc, node ) )!;
	return { page, fixture, gateway, values, snackbar, dialog, saved, row };
};
const variable = ( i:number, name:string, json:object={} )=>new Variable( <any>{ns: 2, i, name, dataType: {i: 11}, ...json} );
const pumps = ()=>new Map<string,{node:Variable; path:string|null}|undefined>( [
	[`local/${A.uaString()}`, {node: variable( 1, 'rpm', {value: 7} ), path: 'Objects/pump1/rpm'}],
	[`plc/${B.uaString()}`, {node: variable( 2, 'flow' ), path: 'Objects/plc/flow'}],
	[`plc/${C.uaString()}`, {node: variable( 3, 'level' ), path: null}]
] );

describe( 'GatewayWatch', ()=>{
	it( 'loads the saved rows, describes each and subscribes once per connection with their parameters', async ()=>{
		const { page, gateway, row } = await create( [item( 'local', A, {samplingInterval: 100} ), item( 'plc', B )], pumps() );
		expect( page.rows().map( r=>r.name ) ).toEqual( ['rpm', 'flow'] );
		expect( row( 'local', A ).value ).toBe( 7 );//the describing read's, until a push
		expect( gateway.subscribe ).toHaveBeenCalledTimes( 1 );
		expect( gateway.subscribe.mock.calls[0].slice( 0, 3 ) ).toEqual( ['local', [A], owner] );
		expect( gateway.subscribe.mock.calls[0][3] ).toEqual( [{samplingInterval: 100}] );
		expect( gateway.addToSubscription ).toHaveBeenCalledTimes( 1 );//the second connection joins the owner's stream
		expect( gateway.addToSubscription.mock.calls[0] ).toEqual( ['plc', [B], owner, [{}]] );
		expect( page.cnnctnName( 'local' ) ).toBe( 'Local' );
		expect( page.nodeLink( row( 'local', A ) ) ).toEqual( ['/gateways', 'g', 'local', 'Objects', 'pump1', 'rpm'] );
	} );

	it( 'shows the ack\'s revised values, a push\'s reading and a later modify', async ()=>{
		const { values, row } = await create( [item( 'local', A, {samplingInterval: 100} )], pumps() );
		const a = row( 'local', A );
		values.next( {opcId: 'local', node: A, revised: {samplingInterval: 250, queueSize: 2, sc: 0}} );
		expect( a.revised ).toEqual( {samplingInterval: 250, queueSize: 2, sc: 0} );
		expect( a.value ).toBe( 7 );//no reading in a revised result
		const source = new Date( 2026, 10, 6, 9, 0, 0 ), server = new Date( 2026, 10, 6, 9, 0, 1 );
		values.next( {opcId: 'local', node: A, value: 9, sc: 0x40940600, source, server} );
		expect( a.value ).toBe( 9 );
		expect( a.sc ).toBe( 0x40940600 );
		expect( a.sourceTime ).toBe( source );
		expect( a.serverTime ).toBe( server );
		expect( a.revised ).toEqual( {samplingInterval: 250, queueSize: 2, sc: 0} );//a reading leaves the item's values alone
		values.next( {opcId: 'local', node: A, revised: {samplingInterval: 250, queueSize: 2, sc: 0x80AF0000}} );//a modify the server refused
		expect( a.revised?.sc ).toBe( 0x80AF0000 );
		expect( a.value ).toBe( 9 );
	} );

	it( 'adds picked nodes with the defaults, saves them and subscribes them on their connection', async ()=>{
		const { page, gateway, saved, row } = await create( [item( 'local', A )], pumps() );
		await page.add( [{opc: 'plc', node: B}, {opc: 'plc', node: C}, {opc: 'local', node: A}] );//A is on the list already
		expect( page.rows().map( r=>r.name ) ).toEqual( ['rpm', 'flow', 'level'] );
		expect( saved().at( -1 ) ).toEqual( [item( 'local', A ), item( 'plc', B ), item( 'plc', C )] );
		expect( gateway.addToSubscription.mock.calls ).toEqual( [['plc', [B, C], owner, [{}, {}]]] );
		expect( page.nodeLink( row( 'plc', C ) ) ).toBeUndefined();//a node the gateway cannot place has no page to link
	} );

	it( 'adds what the dialog answers', async ()=>{
		const { page, gateway, dialog } = await create( [item( 'local', A )], pumps() );
		dialog.answers.set( WatchAddDialog, [{opc: 'plc', node: B}] );
		await page.onAdd();
		expect( dialog.open.mock.calls[0][0] ).toBe( WatchAddDialog );
		expect( gateway.addToSubscription.mock.calls ).toEqual( [['plc', [B], owner, [{}]]] );
		dialog.answers.set( WatchAddDialog, undefined );//a cancel
		await page.onAdd();
		expect( page.rows() ).toHaveLength( 2 );
	} );

	it( 'removes a row, saves and unsubscribes it, and the last watched row ends the stream', async ()=>{
		const { page, gateway, values, saved, row } = await create( [item( 'local', A ), item( 'plc', B )], pumps() );
		await page.remove( row( 'plc', B ) );
		expect( page.rows().map( r=>r.name ) ).toEqual( ['rpm'] );
		expect( saved().at( -1 ) ).toEqual( [item( 'local', A )] );
		expect( gateway.unsubscribe.mock.calls ).toEqual( [['plc', [B], owner]] );
		expect( values.subscribers ).toHaveLength( 1 );
		await page.remove( row( 'local', A ) );
		expect( saved().at( -1 ) ).toEqual( [] );
		expect( gateway.unsubscribe ).toHaveBeenCalledTimes( 1 );//the stream's end unsubscribes the owner's nodes at the gateway
		expect( values.subscribers ).toHaveLength( 0 );
		expect( page.subscription ).toBeUndefined();
	} );

	it( 'edits a row\'s parameters by subscribing it again, and saves them', async ()=>{
		const { page, gateway, values, saved, row, dialog } = await create( [item( 'local', A )], pumps() );
		const a = row( 'local', A );
		values.next( {opcId: 'local', node: A, revised: {samplingInterval: 500, queueSize: 1, sc: 0}} );
		dialog.answers.set( WatchParametersDialog, {queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}} );
		await page.onParameters( a );
		expect( (<any>dialog.open.mock.calls[0][1]).data ).toEqual( {name: 'rpm', parameters: {}} );
		expect( a.parameters ).toEqual( {queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}} );
		expect( a.revised ).toBeUndefined();//until the new ack
		expect( gateway.addToSubscription.mock.calls ).toEqual( [['local', [A], owner, [{queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}}]]] );
		expect( saved().at( -1 ) ).toEqual( [item( 'local', A, {queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}} )] );
		dialog.answers.set( WatchParametersDialog, undefined );//a cancel changes nothing
		await page.onParameters( a );
		expect( gateway.addToSubscription ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'keeps a row the connection cannot describe, saying why, and does not subscribe it', async ()=>{
		const described = pumps();
		described.set( `plc/${B.uaString()}`, undefined );
		const { page, gateway, row } = await create( [item( 'local', A ), item( 'plc', B )], described );
		expect( row( 'plc', B ).error ).toContain( 'ns=2;i=2' );
		expect( page.status( row( 'plc', B ) ) ).toContain( 'no node' );
		expect( gateway.addToSubscription ).not.toHaveBeenCalled();
		expect( gateway.subscribe.mock.calls[0][1] ).toEqual( [A] );
	} );

	it( 'asks for its rows again once the stream errors', async ()=>{
		const { page, gateway, values, snackbar } = await create( [item( 'local', A ), item( 'plc', B )], pumps() );
		vi.useFakeTimers();
		try{
			values.error( {message: "Connection to the gateway was lost."} );
			expect( snackbar.exception ).toHaveBeenCalledTimes( 1 );
			expect( page.subscription ).toBeUndefined();
			vi.advanceTimersByTime( 5_000 );
		}
		finally{
			vi.useRealTimers();
		}
		await flush();
		expect( gateway.subscribe ).toHaveBeenCalledTimes( 2 );
		expect( gateway.subscribe.mock.calls[1][3] ).toEqual( [{}] );
		expect( gateway.addToSubscription ).toHaveBeenCalledTimes( 2 );
		expect( values.subscribers ).toHaveLength( 1 );
	} );

	it( 'names an enumeration\'s value and spells the cells', async ()=>{
		const described = pumps();
		described.set( `local/${A.uaString()}`, {node: variable( 1, 'state', {dataType: {ns: 2, i: 3001}, value: 2} ), path: 'Objects/pump1/state'} );
		const { page, row } = await create( [item( 'local', A, {samplingInterval: 100, filter: {trigger: 2, deadbandType: 0, deadbandValue: 0}} )], described );
		const a = row( 'local', A );
		a.variable!.customDataType = <any>{ id: new NodeId( {ns: 2, i: 3001} ), name: 'PumpState', enumValues: [{id: 1, name: 'Off'}, {id: 2, name: 'Running'}] };
		expect( page.valueText( a ) ).toBe( 'Running' );
		a.setReading( {value: 7, sc: 0} );
		expect( page.valueText( a ) ).toBe( '7' );//a value the enumeration does not name
		expect( page.sampling( a ) ).toBe( '100' );
		expect( page.queue( a ) ).toBe( '1' );
		expect( page.discard( a ) ).toBe( 'oldest' );
		expect( page.filter( a ) ).toBe( 'StatusValueTimestamp, no deadband' );
		expect( page.cell( a, 'connection' ) ).toBe( 'Local' );
	} );
} );