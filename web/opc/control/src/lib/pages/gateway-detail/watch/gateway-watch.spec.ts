import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { BehaviorSubject } from 'rxjs';
import { ProfileStore, RouteItem } from 'jde-spa';
import { isGroupHeader, SnackbarService } from 'jde-framework';
import { NodeId } from '../../../model/node-id';
import { Variable } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { WatchItemJson, WatchRow } from '../../../model/watch';
import { WatchParameters } from '../../../model/watch';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OpcStore } from '../../../services/opc-store';
import { SubscriptionResult } from '../../../services/gateway-service';
import { GatewayWatch } from './gateway-watch';
import { WatchAddDialog } from './watch-add-dialog';
import { WatchParametersDialog } from './watch-parameters-dialog';
import { flush, pageTitleStub, profileStoreStub, provideRoute, screenSize, silence } from 'jde-spa/testing';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { Feed, nodeA as A, nodeB as B } from 'jde-opc/testing';

const C = new NodeId( {ns: 2, i: 3} );
const owner = 'watch/g';
const item = ( opc:string, node:NodeId, parameters:WatchParameters={} ):WatchItemJson=>({opc, node: node.toJson(), parameters});
type Asked = { opc:string; nodes:NodeId[]; parameters?:WatchParameters[] };

//Clients 6b (#419, spec *Watch list*):  the page over a gateway that describes the nodes it is given, records what it is asked to
//subscribe, and feeds the stream;  the profile store holds `items`, and `saved` reads what the page wrote back.  The describes wait
//for `hold`, where given, and `open` moves the page to another gateway, `h`, whose list holds the same items.  `more.connections`
//answers the connections list in place of Local and PLC, and `more.known` is what the node pages know of them.
const create = async ( items:WatchItemJson[], described:Map<string,{node:Variable; path:string|null}|undefined>, hold?:Promise<void>, more:{connections?:()=>Promise<object[]>; known?:string[]}={} )=>{
	const values = new Feed<SubscriptionResult, Asked>();
	const gatewayOf = ( slug:string )=>({
		slug,
		nodeById: vi.fn( async ( opc:string, id:NodeId )=>{ if( hold ) await hold; return described.get( `${opc}/${id.uaString()}` ); } ),
		subscribe: vi.fn( ( opc:string, nodes:NodeId[], _owner:string, parameters?:WatchParameters[] )=>values.open( {opc, nodes, parameters} ) ),
		addToSubscription: vi.fn( ( _opc:string, _nodes:NodeId[], _owner:string, _parameters?:WatchParameters[] )=>{} ),
		unsubscribe: vi.fn( async ( _opc:string, _nodes:NodeId[], _owner:string )=>{} ),
		updateErrorCodes: vi.fn( async ()=>{} )
	});
	const gateway = gatewayOf( 'g' );
	const pageData = ( gateway:object )=>({pageData: {gateway, routing: new RouteItem( {path: 'watch', title: 'Watch list'} )}});
	const data = new BehaviorSubject<object>( pageData( gateway ) );
	const snackbar = snackbarStub();
	const profile = profileStoreStub( {'gateways/g/watch': {items}, 'gateways/h/watch': {items}} );
	const dialog = dialogStub();
	TestBed.configureTestingModule( {providers: [
		screenSize(),
		pageTitleStub,
		provideRoute( {data} ),
		{ provide: SnackbarService, useValue: snackbar },
		{ provide: ProfileStore, useValue: profile },
		{ provide: HistAdminService, useValue: {connections: vi.fn( more.connections ?? (async ()=>[{id: 1, slug: 'local', name: 'Local'}, {id: 2, slug: 'plc', name: 'PLC'}]) )} },
		{ provide: OpcStore, useValue: {cnnctnName: ( _g:string, slug:string )=>slug, cnnctnSlugs: ( _g:string )=>more.known ?? []} },
		{ provide: MatDialog, useValue: dialog }
	]} );
	const fixture = TestBed.createComponent( GatewayWatch );
	const page = fixture.componentInstance;
	fixture.detectChanges();//its first change detection runs ngOnInit
	await flush( 3 );
	const saved = ()=>profile.save.mock.calls.filter( ([key])=>key=='gateways/g/watch' ).map( ([, v])=>(<any>v).items as WatchItemJson[] );
	const row = ( opc:string, node:NodeId )=>page.rows().find( r=>r.key==WatchRow.keyOf( opc, node ) )!;
	const open = ( other:object )=>data.next( pageData( other ) );
	return { page, fixture, gateway, gatewayOf, open, values, snackbar, dialog, saved, row };
};
const variable = ( i:number, name:string, json:object={} )=>new Variable( <any>{ns: 2, i, name, dataType: {i: 11}, ...json} );
const pumps = ()=>new Map<string,{node:Variable; path:string|null}|undefined>( [
	[`local/${A.uaString()}`, {node: variable( 1, 'rpm', {value: 7} ), path: 'Objects/pump1/rpm'}],
	[`plc/${B.uaString()}`, {node: variable( 2, 'flow' ), path: 'Objects/plc/flow'}],
	[`plc/${C.uaString()}`, {node: variable( 3, 'level' ), path: null}]
] );

describe( 'GatewayWatch', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
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

	//a describe out when the page was left subscribed for it, and nothing ever ended that subscription (419-watch #2)
	it( 'subscribes nothing once left while the rows are described', async ()=>{
		let release!:()=>void;
		const { fixture, gateway } = await create( [item( 'local', A )], pumps(), new Promise<void>( r=>release=r ) );
		fixture.destroy();
		release();
		await flush( 3 );
		expect( gateway.subscribe ).not.toHaveBeenCalled();
		expect( gateway.addToSubscription ).not.toHaveBeenCalled();
	} );
	//...and one out when it moved to another gateway subscribed that gateway's rows, before they were described, then again (419-watch #2)
	it( 'subscribes the next gateway\'s rows once, and none of the last\'s, when it moves on while they are described', async ()=>{
		let release!:()=>void;
		const { gateway, gatewayOf, open } = await create( [item( 'local', A )], pumps(), new Promise<void>( r=>release=r ) );
		const next = gatewayOf( 'h' );
		open( next );
		await flush( 3 );
		release();
		await flush( 3 );
		expect( gateway.subscribe ).not.toHaveBeenCalled();
		expect( next.subscribe ).toHaveBeenCalledTimes( 1 );
		expect( next.addToSubscription ).not.toHaveBeenCalled();
	} );

	//each message builds a Guid or ByteString id afresh, and the row was found by identity (419-watch #3)
	it( 'takes a push for a Guid or ByteString row', async ()=>{
		const guid = ()=>new NodeId( {ns: 3, g: '12345678-1234-1234-1234-123456789abc'} ), bytes = ()=>new NodeId( {ns: 3, b: 'AQID'} );
		const described = new Map<string,{node:Variable; path:string|null}|undefined>( [
			[`local/${guid().uaString()}`, {node: variable( 4, 'g' ), path: null}],
			[`local/${bytes().uaString()}`, {node: variable( 5, 'b' ), path: null}]
		] );
		const { values, row } = await create( [item( 'local', guid() ), item( 'local', bytes() )], described );
		values.next( {opcId: 'local', node: guid(), value: 1, sc: 0} );
		values.next( {opcId: 'local', node: bytes(), value: 2, sc: 0} );
		expect( row( 'local', guid() ).value ).toBe( 1 );
		expect( row( 'local', bytes() ).value ).toBe( 2 );
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

	//the dialog's choice was the connections list alone, a snapshot taken whether it had been read or not (419-watch #11)
	it( 'offers the connections the rows and the node pages know, where the list is refused', async ()=>{
		silence();
		const { page, dialog } = await create( [item( 'local', A ), item( 'plc', B )], pumps(), undefined, {connections: async ()=>{ throw new Error( 'Forbidden' ); }, known: ['plc', 'tank']} );
		await page.onAdd();
		expect( (<any>dialog.open.mock.calls[0][1]).data.connections ).toEqual( [{slug: 'local', name: 'local'}, {slug: 'plc', name: 'plc'}, {slug: 'tank', name: 'tank'}] );
	} );
	it( 'says there is nothing to add from, where nothing knows a connection', async ()=>{
		silence();
		const { page, dialog, snackbar } = await create( [], pumps(), undefined, {connections: async ()=>{ throw new Error( 'Forbidden' ); }} );
		await page.onAdd();
		expect( dialog.open ).not.toHaveBeenCalled();
		expect( snackbar.warn ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'waits for the connections list before it opens the dialog', async ()=>{
		let release!:()=>void;
		const listed = new Promise<void>( r=>release=r );
		const { page, dialog } = await create( [], pumps(), undefined, {connections: async ()=>{ await listed; return [{id: 1, slug: 'local', name: 'Local'}]; }} );
		const adding = page.onAdd();
		release();
		await adding;
		expect( (<any>dialog.open.mock.calls[0][1]).data.connections ).toEqual( [{id: 1, slug: 'local', name: 'Local'}] );
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
		expect( a.revised ).toEqual( {samplingInterval: 500, queueSize: 1, sc: 0} );//what the item runs at until the new ack
		expect( gateway.addToSubscription.mock.calls ).toEqual( [['local', [A], owner, [{queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}}]]] );
		expect( saved().at( -1 ) ).toEqual( [item( 'local', A, {queueSize: 5, filter: {deadbandType: 1, deadbandValue: 2}} )] );
		values.next( {opcId: 'local', node: A, revised: {sc: 0x80AE0000}} );//the request failed before the server answered (419-watch #4)
		expect( a.revised ).toEqual( {samplingInterval: 500, queueSize: 1, sc: 0x80AE0000} );
		expect( page.revisedTooltip( a ) ).toContain( 'the item runs as it was' );
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

	//the error a failed describe left never cleared, and Refresh described the row without watching it (419-watch #5)
	it( 'watches a row Refresh describes after a failed describe', async ()=>{
		const described = pumps(), flow = described.get( `plc/${B.uaString()}` );
		described.set( `plc/${B.uaString()}`, undefined );
		const { page, gateway, row } = await create( [item( 'local', A ), item( 'plc', B )], described );
		expect( row( 'plc', B ).error ).toBeDefined();
		described.set( `plc/${B.uaString()}`, flow );
		await page.onRefresh();
		expect( row( 'plc', B ).error ).toBeUndefined();
		expect( page.status( row( 'plc', B ) ) ).toBe( '' );
		expect( gateway.addToSubscription.mock.calls ).toEqual( [['plc', [B], owner, [{}]]] );//only the row that was not watched
		await page.onRefresh();
		expect( gateway.addToSubscription ).toHaveBeenCalledTimes( 1 );
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

	//each retry while the gateway is down errored the stream again, and each error showed the snackbar again (419-watch #8)
	it( 'says once that the stream ended, however many retries fail', async ()=>{
		const { values, snackbar } = await create( [item( 'local', A )], pumps() );
		const lost = {message: "Connection to the gateway was lost."};
		vi.useFakeTimers();
		try{
			values.error( lost );
			for( let i=0; i<3; ++i ){
				vi.advanceTimersByTime( 5_000 );
				values.error( lost );
			}
			expect( snackbar.exception ).toHaveBeenCalledTimes( 1 );
			vi.advanceTimersByTime( 5_000 );
			values.next( {opcId: 'local', node: A, revised: {samplingInterval: 500, queueSize: 1, sc: 0}} );//back:  the ack
			values.error( lost );
			expect( snackbar.exception ).toHaveBeenCalledTimes( 2 );
		}
		finally{
			vi.useRealTimers();
		}
	} );

	//names, readings and edits change the rows in place, and `shown` kept the order and filters of the rows as they first were (419-watch #7)
	it( 'sorts again once the rows are described, and as pushes arrive', async ()=>{
		let release!:()=>void;
		const described = new Map<string,{node:Variable; path:string|null}|undefined>( [
			[`local/${A.uaString()}`, {node: variable( 1, 'zeta' ), path: null}],
			[`local/${B.uaString()}`, {node: variable( 2, 'alpha' ), path: null}]
		] );
		const { page, values } = await create( [item( 'local', A ), item( 'local', B )], described, new Promise<void>( r=>release=r ) );
		expect( page.shown().map( r=>r.name ) ).toEqual( [A.uaString(), B.uaString()] );//by id until described
		release();
		await flush( 3 );
		expect( page.shown().map( r=>r.name ) ).toEqual( ['alpha', 'zeta'] );
		page.onSortChange( {active: 'value', direction: 'desc'} );
		values.next( {opcId: 'local', node: A, value: 1, sc: 0} );
		values.next( {opcId: 'local', node: B, value: 2, sc: 0} );
		expect( page.shown().map( r=>r.name ) ).toEqual( ['alpha', 'zeta'] );
		values.next( {opcId: 'local', node: A, value: 3, sc: 0} );
		expect( page.shown().map( r=>r.name ) ).toEqual( ['zeta', 'alpha'] );
	} );
	//reviews-issue/591-views-filter-box.md #7:  the filter box's suggestions kept the rows as they were set, before the describes and pushes
	it( 'suggests the names and readings the rows take in place', async ()=>{
		let release!:()=>void;
		const described = new Map<string,{node:Variable; path:string|null}|undefined>( [
			[`local/${A.uaString()}`, {node: variable( 1, 'zeta' ), path: null}],
			[`local/${B.uaString()}`, {node: variable( 2, 'alpha' ), path: null}]
		] );
		const { page, values } = await create( [item( 'local', A ), item( 'local', B )], described, new Promise<void>( r=>release=r ) );
		expect( page.filterValues()['value'] ).toEqual( [] );
		release();
		await flush( 3 );
		expect( page.filterValues()['node'] ).toEqual( ['alpha', 'zeta'] );
		values.next( {opcId: 'local', node: A, value: 3, sc: 0} );
		expect( page.filterValues()['value'] ).toEqual( [3] );
	} );

	//Clients 7 (#420, spec *Live trend*):  every push goes to the trend's buffer under the row's own series, a revised result and a
	//refused subscribe's OpcError are no readings, and a row removed takes its pushes and its tick
	it( "buffers each push for the trend, and forgets a removed row's", async ()=>{
		const { page, values, row } = await create( [item( 'local', A ), item( 'plc', B )], pumps() );
		const a = row( 'local', A ), b = row( 'plc', B );
		values.next( {opcId: 'local', node: A, value: 1, sc: 0, source: new Date( 1000 )} );
		values.next( {opcId: 'local', node: A, revised: {samplingInterval: 500, queueSize: 1, sc: 0}} );
		values.next( {opcId: 'local', node: A, value: 2, sc: 0, server: new Date( 2000 )} );//no source time:  the server's
		values.next( {opcId: 'plc', node: B, value: new OpcError( 0x80340000, "Subscribe", "", undefined ), sc: 0x80340000} );
		expect( page.buffer.values( [a.key] ).map( v=>[v.source!.getTime(), v.value, v.series] ) ).toEqual( [[1000, 1, a.trendKey], [2000, 2, a.trendKey]] );
		expect( page.buffer.size( b.key ) ).toBe( 0 );
		page.toggleTick( a );
		expect( page.tickedRows() ).toEqual( [a] );
		await page.remove( a );
		expect( page.buffer.size( a.key ) ).toBe( 0 );
		expect( page.tickedRows() ).toEqual( [] );
	} );
	it( 'ticks rows for the trend, up to eight, none without a value, in list order', async ()=>{
		const described = pumps();
		const ids = [1, 2, 3, 4, 5, 6, 7, 8, 9].map( i=>new NodeId( {ns: 4, i} ) );
		ids.forEach( (id, i)=>described.set( `local/${id.uaString()}`, {node: variable( 10+i, `v${i+1}` ), path: null} ) );
		described.set( `plc/${B.uaString()}`, undefined );
		const { page, row } = await create( [item( 'plc', B ), ...ids.map( id=>item( 'local', id ) )], described );
		expect( page.canTick( row( 'plc', B ) ) ).toBe( false );
		page.toggleTick( row( 'plc', B ) );
		expect( page.tickedRows() ).toEqual( [] );
		for( const id of [...ids].reverse() )
			page.toggleTick( row( 'local', id ) );
		expect( page.tickedRows().map( r=>r.name ) ).toEqual( ['v2', 'v3', 'v4', 'v5', 'v6', 'v7', 'v8', 'v9'] );//the ninth tick, v1's, was refused
		expect( page.canTick( row( 'local', ids[0] ) ) ).toBe( false );
		expect( page.tickTooltip( row( 'local', ids[0] ) ) ).toContain( `${GatewayWatch.maxSeries} rows` );
		page.toggleTick( row( 'local', ids[8] ) );//unticked, there is room again
		expect( page.canTick( row( 'local', ids[0] ) ) ).toBe( true );
		page.toggleTick( row( 'local', ids[0] ) );
		expect( page.tickedRows().map( r=>r.name ) ).toEqual( ['v1', 'v2', 'v3', 'v4', 'v5', 'v6', 'v7', 'v8'] );
		expect( page.displayedColumns()[0] ).toBe( 'trend' );
	} );
	//a refused subscribe's reading goes to no buffer, and a tick drew an empty line in one of the eight slots (420 #7)
	it( "doesn't tick a row whose subscribe was refused", async ()=>{
		const { page, values, row } = await create( [item( 'plc', B )], pumps() );
		values.next( {opcId: 'plc', node: B, value: new OpcError( 0x80340000, "Subscribe", "", undefined ), sc: 0x80340000} );
		expect( page.canTick( row( 'plc', B ) ) ).toBe( false );
		expect( page.tickTooltip( row( 'plc', B ) ) ).toMatch( /^No value to trend:  / );
		values.next( {opcId: 'plc', node: B, value: 2, sc: 0, source: new Date( 1000 )} );//a value at last
		expect( page.canTick( row( 'plc', B ) ) ).toBe( true );
	} );
	//the ticked rows are session state:  a page reopened on Trend showed none (420 #9)
	it( 'opens on the list, and again for the next gateway', async ()=>{
		localStorage.setItem( 'gatewayWatch/tabIndex', '1' );
		const { page, gatewayOf, open } = await create( [item( 'local', A )], pumps() );
		expect( page.tabIndex() ).toBe( 0 );
		page.tabIndex.set( 1 );
		open( gatewayOf( 'h' ) );
		await flush( 3 );
		expect( page.tabIndex() ).toBe( 0 );
		localStorage.removeItem( 'gatewayWatch/tabIndex' );
	} );
	//the checkbox of a ticked row that a Refresh failed to describe was disabled, so the row could not be unticked (420 #2)
	it( 'unticks a ticked row that errors later', async ()=>{
		const described = pumps();
		const { page, row } = await create( [item( 'local', A )], described );
		page.toggleTick( row( 'local', A ) );
		described.set( `local/${A.uaString()}`, undefined );
		await page.onRefresh();
		expect( row( 'local', A ).error ).toBeDefined();
		expect( page.canTick( row( 'local', A ) ) ).toBe( true );
		page.toggleTick( row( 'local', A ) );
		expect( page.tickedRows() ).toEqual( [] );
		expect( page.canTick( row( 'local', A ) ) ).toBe( false );//once unticked, it is a row without a value again
		expect( page.tickTooltip( row( 'local', A ) ) ).toBe( 'No value to trend.' );
	} );
	it( "keeps the window picked for the trend, and the buffer's", async ()=>{
		const { page } = await create( [item( 'local', A )], pumps() );
		expect( page.trendWindow() ).toBe( 10 );
		page.setTrendWindow( 60 );
		expect( page.buffer.window ).toBe( 3_600_000 );
		expect( localStorage.getItem( 'gatewayWatch/trendWindow' ) ).toBe( '60' );
		page.setTrendWindow( 10 );
		expect( localStorage.getItem( 'gatewayWatch/trendWindow' ) ).toBeNull();
		expect( page.buffer.window ).toBe( 600_000 );
		expect( GatewayWatch.windowOf( 7 ) ).toBe( 10 );//a value saved by hand that is no window offered
	} );

	it( 'names an enumeration\'s value and spells the cells', async ()=>{
		const described = pumps(), state = variable( 1, 'state', {dataType: {ns: 2, i: 3001}, value: 2} );
		state.customDataType = <any>{ id: new NodeId( {ns: 2, i: 3001} ), name: 'PumpState', enumValues: [{id: 1, name: 'Off'}, {id: 2, name: 'Running'}] };//as the gateway's describe resolves it
		described.set( `local/${A.uaString()}`, {node: state, path: 'Objects/pump1/state'} );
		const { page, row } = await create( [item( 'local', A, {samplingInterval: 100, filter: {trigger: 2, deadbandType: 0, deadbandValue: 0}} )], described );
		const a = row( 'local', A );
		expect( page.valueText( a ) ).toBe( 'Running' );
		expect( page.cell( a, 'value' ) ).toBe( 'Running' );//what the filter and the sort read:  the number, while the cell said the name (419-watch #13)
		expect( page.filterValues()['value'] ).toEqual( ['Running'] );
		a.setReading( {value: 7, sc: 0} );
		expect( page.valueText( a ) ).toBe( '7' );//a value the enumeration does not name
		expect( page.sampling( a ) ).toBe( '100' );
		expect( page.queue( a ) ).toBe( '1' );
		expect( page.discard( a ) ).toBe( 'oldest' );
		expect( page.filter( a ) ).toBe( 'StatusValueTimestamp, no deadband' );
		expect( page.cell( a, 'connection' ) ).toBe( 'Local' );
	} );

	//#593:  the Sort tab was the editor's last, and the editor went with it
	it( 'renders its list with no view editor', async ()=>{
		const { fixture } = await create( [item( 'local', A )], pumps() );
		fixture.detectChanges();
		await flush( 3 );
		const element = fixture.nativeElement as HTMLElement;
		expect( [!!element.querySelector( 'view-toolbar' ), !!element.querySelector( 'mat-table' )] ).toEqual( [true, true] );
		expect( element.querySelector( 'ql-list-settings, button[aria-label="Edit view"]' ) ).toBeNull();
	} );

	//#596:  the list's groups, cut from the rows shown, with a header's count and sums its group's
	it( 'heads the rows with their groups, summed, a header row each, and collapses one', async ()=>{
		const { page, fixture } = await create( [item( 'local', A, {samplingInterval: 100} ), item( 'plc', B, {samplingInterval: 250} ), item( 'plc', C, {samplingInterval: 500} )], pumps() );
		const draw = async ()=>{
			fixture.detectChanges();
			await flush( 3 );
		};
		page.viewSet.edit( view=>{ view.setGroupBy( "connection" ); view.toggleSum( "sampling" ); } );
		const headed = ()=>page.tableRows().map( r=>isGroupHeader( r ) ? `${r.label} ${r.count} ${r.sums.map( s=>`${s.label} ${s.sum}` )}` : r.name );
		expect( headed() ).toEqual( ["Local 1 Sampling 100", "rpm", "PLC 2 Sampling 750", "flow", "level"] );
		await draw();
		const element = fixture.nativeElement as HTMLElement;
		expect( [element.querySelectorAll( 'mat-row.jde-group-row' ).length, element.querySelectorAll( 'mat-row' ).length] ).toEqual( [2, 5] );
		page.viewSet.toggleGroup( "string:PLC" );
		expect( [headed(), page.shown().length] ).toEqual( [["Local 1 Sampling 100", "rpm", "PLC 2 Sampling 750"], 3] );
	} );
} );