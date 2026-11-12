import { TestBed } from '@angular/core/testing';
import { config } from 'rxjs';
import { GroupHeader, isGroupHeader, SnackbarService } from 'jde-framework';
import { ProfileStore } from 'jde-spa';
import { GATEWAY_SERVICE } from '../../../services/gateway-service';
import { Enum } from '../../../model/enum';
import { NodeId } from '../../../model/node-id';
import { Method, OpcObject, UaNode, Variable } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { OpcError } from '../../../model/opc-error';
import { EAccess, ETypes } from '../../../model/types';
import { MethodCallDialog } from './method-call-dialog';
import { NodeChildren } from './node-children';
import { WriteDialog } from './write-dialog';
import { OpcStore } from '../../../services/opc-store';
import { WatchStore } from '../../../services/watch-store';
import { flush, pageTitleStub, provideRoute, screenSize } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
import { fakeGateway, TestGateway, variable } from 'jde-opc/testing';

const X = variable( 1, "x" ), Y = variable( 2, "y" );

describe( 'NodeChildren subscription values', ()=>{
	//the page over a gateway that fakeGateway answers the subscriptions and the code names of, whose writes and re-reads answer what
	//`answer` was last given, and which records the rest
	const create = ()=>{
		let written:any;
		const gateway = Object.assign( fakeGateway( 'local' ), {
			write: vi.fn( async ( _opc:string, _n:NodeId, _v:unknown )=>written ),
			read: vi.fn( async ( _opc:string, _n:NodeId )=>written ),
			addToSubscription: vi.fn(),
			unsubscribe: vi.fn( async ( _cnnctn:string, _nodes:NodeId[], _owner?:string )=>{} )
		} );
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [
			screenSize(),
			provideRoute(),//no ngOnInit load - the state is set by hand below
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: snackbar },
			pageTitleStub
		]});
		const fixture = TestBed.createComponent( NodeChildren );
		const page = fixture.componentInstance;
		const save = vi.spyOn( TestBed.inject(ProfileStore), 'save' ).mockResolvedValue();
		page.pageData = { gateway, route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		return {
			page, fixture, gateway, snackbar, save,
			setNodes: ( nodes:Variable[], resubscribe:boolean )=>(<any>page).setNodes( nodes, resubscribe ),
			unsubscribed: ()=>gateway.unsubscribe.mock.calls.map( c=>c[1] ),
			answer: ( reading:any )=>{ written = reading; }
		};
	};

	//a tick was saved only on the way to the next node, and destroying the page unticked every row:  a switch to the History tab
	//and back came to none ticked
	describe( 'the ticks', ()=>{
		it( 'are saved as they are made', ()=>{
			const { page, setNodes, save } = create();
			setNodes( [X, Y], true );
			page.selections.select( X );
			expect( save ).toHaveBeenCalledTimes( 1 );
			expect( (save.mock.calls[0][1] as any).subscriptions.map( (s:NodeId)=>s.key ) ).toEqual( [X.key] );
			page.selections.deselect( X );
			expect( save ).toHaveBeenCalledTimes( 2 );
			expect( (save.mock.calls[1][1] as any).subscriptions ).toEqual( [] );
		} );
		it( 'are not saved again when restored', ()=>{
			const { page, setNodes, save } = create();
			page.profile.subscriptions = [X.nodeId];
			setNodes( [X, Y], true );
			expect( page.selections.isSelected(X) ).toBe( true );
			expect( save ).not.toHaveBeenCalled();
		} );
		it( 'outlive the tab', ()=>{
			const { page, fixture, setNodes, save } = create();
			setNodes( [X, Y], true );
			page.selections.select( X );
			fixture.destroy();
			expect( save ).toHaveBeenCalledTimes( 1 );
			expect( page.profile.subscriptions.map( s=>s.key ) ).toEqual( [X.key] );
		} );
		it( 'are named by what the header box does', ()=>{
			const { page, setNodes } = create();
			page.viewSet.reset( [NodeView.default()], NodeView.schema );//the rows are the view's
			setNodes( [X, Y], true );
			expect( page.checkboxLabel() ).toBe( 'select all' );
			page.selections.select( X );
			expect( page.checkboxLabel() ).toBe( 'select all' );
			page.selections.select( Y );
			expect( page.checkboxLabel() ).toBe( 'deselect all' );
		} );
	} );

	//angular-review3 #12: `variables.find(...)!.value = …` threw a TypeError inside the observer - and again on every publish
	//tick - once a re-browse had dropped the row the value was for.  RxJS swallows an observer throw into its unhandled-error
	//channel rather than out of next(), so that is where the old behaviour shows.
	it( 'ignores a value for a node that is no longer a row', async ()=>{
		const { page, gateway, setNodes } = create();
		const unhandled = vi.fn();
		const previous = config.onUnhandledError;
		config.onUnhandledError = unhandled;
		try{
			setNodes( [X, Y], true );
			page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
			setNodes( [Y], false );//X is gone from the re-browse
			gateway.values.next( {opcId: 'local', node: X.nodeId, value: 7} as any );
			gateway.values.next( {opcId: 'local', node: Y.nodeId, value: 8} as any );
			await flush();//onUnhandledError is reported on a later task
			expect( unhandled ).not.toHaveBeenCalled();
			expect( Y.value ).toBe( 8 );//the surviving row still updates
		}
		finally{ config.onUnhandledError = previous; }
	} );

	//...and nothing unsubscribed the dropped node, so the server kept publishing it:  a new SelectionModel emits no `changed`.
	it( 'unsubscribes a persisted node the re-browse dropped, keeping the intent', ()=>{
		const { page, gateway, setNodes, unsubscribed } = create();
		page.profile.subscriptions = [X.nodeId, Y.nodeId];
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
		gateway.unsubscribe.mockClear();
		setNodes( [Y], false );
		expect( unsubscribed() ).toHaveLength( 1 );
		expect( unsubscribed()[0].map(n=>n.key) ).toEqual( [X.key] );
		expect( page.profile.subscriptions.map(n=>n.key) ).toContain( X.key );//the intent survives, so a later browse re-subscribes
	} );

	it( 'unsubscribes nothing when every persisted node still has a row', ()=>{
		const { page, gateway, setNodes, unsubscribed } = create();
		page.profile.subscriptions = [X.nodeId, Y.nodeId];
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
		gateway.unsubscribe.mockClear();
		setNodes( [X, Y], false );
		expect( unsubscribed() ).toHaveLength( 0 );
	} );

	//angular-review3 #13: an errored Subscription is dead but still truthy, so the next tick took the addToSubscription
	//branch - which builds a fresh gateway Subject with NO observer.  Values were dropped while the rows showed as live.
	it( 'drops the dead subscription when the socket errors it', ()=>{
		const { page, gateway, setNodes } = create();
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		expect( page.subscription ).toBeDefined();
		gateway.values.error( {message: "Connection to the gateway was lost."} );//Gateway.handleConnectionError
		expect( page.subscription ).toBeUndefined();
	} );

	it( 're-subscribes rather than adding to an observer-less Subject after a drop', ()=>{
		const { page, gateway, setNodes } = create();
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		gateway.values.error( {message: "Connection to the gateway was lost."} );
		gateway.subscribe.mockClear();
		page.onSubscriptionChange( {added: [Y], removed: []} as any );//the user re-ticks a row
		expect( gateway.subscribe ).toHaveBeenCalledTimes( 1 );
		expect( gateway.addToSubscription ).not.toHaveBeenCalled();
		gateway.values.next( {opcId: 'local', node: Y.nodeId, value: 9} as any );
		expect( Y.value ).toBe( 9 );//the values actually arrive now
	} );

	it( 'drops it on complete too', ()=>{
		const { page, gateway, setNodes } = create();
		setNodes( [X], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		gateway.values.complete();
		expect( page.subscription ).toBeUndefined();
	} );

	//OPC 10000-4 7.38:  the push handler set `row.value` alone, so the quality the socket carried never reached the row - and a
	//Bad reading arrived as an OpcError that the number editor bound as its value.  Fresh rows:  X and Y above are shared.
	describe( 'quality', ()=>{
		const bad = 0x808C0000, uncertain = 0x40940600;
		//the page subscribed to one row, and `push` to send it a reading
		const subscribed = ()=>{
			const d = create();
			const row = variable( 7, "q", {value: 5, userAccessLevel: EAccess.Read | EAccess.Write} );
			d.setNodes( [row], true );
			d.page.onSubscriptionChange( {added: [row], removed: []} as any );
			return { ...d, row, push: ( reading:object )=>d.gateway.values.next( {opcId: 'local', node: row.nodeId, ...reading} as any ) };
		};

		it( 'keeps the quality a push carries', ()=>{
			const { page, row, push } = subscribed();
			push( {value: 1500, sc: uncertain} );
			expect( row ).toMatchObject( {value: 1500, sc: uncertain} );
			expect( page.readOnly(row) ).toBe( false );//uncertain is still a reading, and still the user's to write over
			expect( page.qualityIcon(row) ).toBe( "warning" );
		} );

		//reviews-issue/591-views-filter-box.md #7:  the filter box's suggestions kept the values the browse brought
		it( 'suggests the value a push brings', ()=>{
			const { page, push } = subscribed();
			expect( page.filterValues()['snapshot'] ).toEqual( [5] );
			push( {value: 6, sc: 0} );
			expect( page.filterValues()['snapshot'] ).toEqual( [6] );
		} );
		//reviews-issue/595-views-slice-panel.md #5:  sliced, every push read the rows again, so they re-sorted by any slice column with
		//no value held, where an unsliced view's stay put
		it( 'reads the rows again on a push only while the slice holds a value of a column a reading changes', ()=>{
			const { page, row, push } = subscribed();
			page.viewSet.reset( [NodeView.default()], NodeView.schema );
			page.viewSet.edit( view=>{ view.sliceBy = "class"; } );
			page.viewSet.setSlice( "Variable" );
			const rows = page.rows();
			push( {value: 6, sc: 0} );
			expect( page.rows() ).toBe( rows );
			page.viewSet.edit( view=>{ view.sliceBy = "status"; } );
			expect( page.rows() ).toEqual( [row] );
			push( {sc: bad} );
			expect( page.rows() ).toEqual( [row] );//no value held
			page.viewSet.setSlice( OpcError.text(0) );
			expect( page.rows() ).toEqual( [] );
			push( {value: 7, sc: 0} );
			expect( page.rows() ).toEqual( [row] );
		} );
		//reviews-issue/595-views-slice-panel.md #13:  the rows and the counts each ran the filter over every row on a push
		it( 'runs the filter once a push for the rows and the counts', ()=>{
			const { page, row, push } = subscribed();
			page.viewSet.reset( [NodeView.default()], NodeView.schema );
			page.viewSet.edit( view=>{ view.sliceBy = "status"; } );
			push( {value: 6, sc: 0} );
			page.viewSet.setSlice( OpcError.text(0) );
			expect( [page.rows(), page.sliceCounts()] ).toEqual( [[row], [{value: OpcError.text(0), count: 1}]] );
			const predicate = vi.spyOn( NodeView.prototype, 'predicate' );
			push( {sc: bad} );
			expect( [page.rows(), page.sliceCounts().length, predicate.mock.calls.length] ).toEqual( [[], 1, 1] );
			predicate.mockRestore();
		} );
		//#596
		it( 'heads the rows with their groups, cut again on a push where a reading changes the group column, and collapses one', ()=>{
			const { page, row, push } = subscribed();
			page.viewSet.reset( [NodeView.default()], NodeView.schema );
			expect( page.tableRows() ).toBe( page.rows() );
			page.viewSet.setGroupBy( "status" );
			push( {value: 6, sc: 0} );
			const headed = ()=>page.tableRows().map( r=>isGroupHeader( r ) ? `${r.label} ${r.count}` : r.name );
			expect( headed() ).toEqual( [`${OpcError.text(0)} 1`, "q"] );
			push( {sc: bad} );
			expect( headed() ).toEqual( [`${OpcError.text(bad)} 1`, "q"] );
			page.viewSet.toggleGroup( ( page.tableRows()[0] as GroupHeader ).key );
			expect( [headed(), page.rows()] ).toEqual( [[`${OpcError.text(bad)} 1`], [row]] );//the rows, and their count, are the view's still
		} );
		//reviews-issue/596-views-group-by.md #1:  the rows were cut where the status changed, in the order of their last sort, so a push
		//that moved a row to another status split its old group in two, under headers of one key
		it( 'gathers a row a push moves to another status under its new group, one header a value', ()=>{
			const d = create();
			const rows = ["a", "b", "c"].map( ( name, i )=>variable( 11+i, name, {value: 5, userAccessLevel: EAccess.Read | EAccess.Write} ) );
			d.setNodes( rows, true );
			d.page.onSubscriptionChange( {added: rows, removed: []} as any );
			const push = ( row:Variable, reading:object )=>d.gateway.values.next( {opcId: 'local', node: row.nodeId, ...reading} as any );
			d.page.viewSet.reset( [NodeView.default()], NodeView.schema );
			d.page.viewSet.setGroupBy( "status" );
			rows.forEach( r=>push( r, {value: 6, sc: 0} ) );
			const headed = ()=>d.page.tableRows().map( r=>isGroupHeader( r ) ? `${r.label} ${r.count}` : r.name );
			expect( headed() ).toEqual( [`${OpcError.text(0)} 3`, "a", "b", "c"] );
			push( rows[1], {sc: bad} );
			expect( headed() ).toEqual( [`${OpcError.text(0)} 2`, "a", "c", `${OpcError.text(bad)} 1`, "b"] );
		} );
		//the filter is read again on a push for the counts, and the rows follow it where a row left or joined it
		it( 'takes a row out of the rows when a push leaves it outside the filter', ()=>{
			const { page, push } = subscribed();
			page.viewSet.reset( [NodeView.default()], NodeView.schema );
			push( {value: 6, sc: 0} );
			page.viewSet.edit( view=>{ view.filter = `status:${OpcError.text(0)}`; } );
			expect( page.rows().length ).toBe( 1 );
			push( {sc: bad} );
			expect( page.rows() ).toEqual( [] );
		} );

		it( 'holds the last value, locked, through a Bad reading', ()=>{
			const { page, row, push } = subscribed();
			push( {sc: bad} );
			expect( row ).toMatchObject( {value: 5, sc: bad} );
			expect( page.readOnly(row) ).toBe( true );
			expect( page.qualityIcon(row) ).toBe( "error" );
			expect( page.statusTooltip(row) ).toContain( "0x808C0000" );//the Status cell's tooltip:  code and name - the cell's ellipsis eats a long name's tail
		} );

		it( 'never puts a failure in the value', ()=>{
			const { row, push } = subscribed();
			push( {value: new OpcError(0x80340000, "Subscribe", "", undefined), sc: 0x80340000} );
			expect( row.value ).toBe( 5 );
			expect( row.sc ).toBe( 0x80340000 );
		} );

		it( 'unlocks on the next good reading', ()=>{
			const { page, row, push } = subscribed();
			push( {sc: bad} );
			push( {value: 6, sc: 0} );
			expect( row ).toMatchObject( {value: 6, sc: 0} );
			expect( page.readOnly(row) ).toBe( false );
			expect( page.qualityIcon(row) ).toBeUndefined();
			expect( page.status(row) ).toBe( "Good" );
			expect( page.statusTooltip(row) ).toBe( "" );//plain Good has nothing to add
		} );

		//a fault holds its code on every tick it lasts - one request for its name, not one a second.
		it( 'asks for a code\'s name when the code changes, not on every push', ()=>{
			const { gateway, push } = subscribed();
			const code = 0x80AB0000;//one no other test has named
			push( {value: 1, sc: code} );
			push( {value: 2, sc: code} );
			push( {value: 3, sc: code + 0x0600} );//the same name, flagged - still a change of code
			expect( gateway.updateErrorCodes ).toHaveBeenCalledTimes( 2 );
			push( {value: 4, sc: 0} );
			expect( gateway.updateErrorCodes ).toHaveBeenCalledTimes( 2 );//Good needs no name
		} );

		//the icon beside the value stands in for the Status column - beside it, it would only say the same thing twice.
		it( 'leaves the quality to the Status column where the view shows one', ()=>{
			const { page } = subscribed();
			const views = [NodeView.default(), NodeView.fromConfig({name: "quality", configColumns: ["id", "name", "snapshot", "status"], sort: []}, NodeView.schema)];
			page.viewSet.reset( views, NodeView.schema );
			expect( page.statusShown() ).toBe( false );//the default view hides Status, so the icon is what says it
			page.viewSet.select( 1 );
			expect( page.statusShown() ).toBe( true );
		} );

		it( 'takes a write\'s echo as a reading, quality and all', async ()=>{
			const { page, row } = subscribed();
			vi.spyOn( WriteDialog, 'open' ).mockResolvedValue( {value: 9, sc: uncertain} );
			await page.write( row );
			expect( row ).toMatchObject( {value: 9, sc: uncertain} );
		} );
	} );

	//clients spec *Values and writes* (#423):  the cell editors gave way to the write dialog, which types the value as the DataType says,
	//sends it and closes with the value read back;  the row shows that reading, and keeps its own when the dialog is cancelled or refused
	describe( 'the write dialog', ()=>{
		const row = ( value:any )=>variable( 7, "q", {value, dataType: {i: ETypes.Double}, userAccessLevel: EAccess.Read | EAccess.Write} );
		it( 'opens on the row and sends its value through the gateway, index range and all', async ()=>{
			const { page, gateway, answer } = create();
			const n = row( 5 );
			const open = vi.spyOn( WriteDialog, 'open' ).mockImplementation( async ( _d, data )=>{
				expect( data.node ).toBe( n );
				return data.write( [7, 8], "1:2" );
			} );
			answer( {value: [5, 7, 8], sc: 0} );
			await page.write( n );
			expect( open ).toHaveBeenCalledTimes( 1 );
			expect( gateway.write ).toHaveBeenCalledWith( 'local', n.nodeId, [7, 8], "1:2" );
			expect( n.value ).toEqual( [5, 7, 8] );
		} );
		it( 'leaves the row as it was when the dialog is cancelled', async ()=>{
			const { page, gateway } = create();
			const n = row( 5 );
			vi.spyOn( WriteDialog, 'open' ).mockResolvedValue( undefined );
			await page.write( n );
			expect( gateway.write ).not.toHaveBeenCalled();
			expect( n.value ).toBe( 5 );
		} );
		it( 'offers a Write in the cell and on the row menu of a writable row alone', ()=>{
			const { page, fixture } = create();
			const writable = row( 5 ), locked = variable( 8, "r", {value: 1, dataType: {i: ETypes.Double}, userAccessLevel: EAccess.Read} );
			const folder = new OpcObject( <any>{ns: 2, i: 5010, name: '_Test', browse: {ns: 2, name: '_Test'}} );//reviews-issue/423-clients-writes.md #1:  no access level, so readOnly was false
			page.viewSet.reset( [NodeView.fromConfig( {name: "value", configColumns: ["name", "snapshot"], sort: []}, NodeView.schema )], NodeView.schema );
			page.nodes.set( [writable, locked, folder] );
			page.isLoading.set( false );
			fixture.detectChanges();
			expect( fixture.nativeElement.querySelector( 'button[aria-label="Write q"]' ) ).not.toBeNull();
			expect( fixture.nativeElement.querySelector( 'button[aria-label="Write r"]' ) ).toBeNull();
			expect( fixture.nativeElement.querySelector( 'button[aria-label="Write _Test"]' ) ).toBeNull();//an Object has no Value
			expect( fixture.nativeElement.querySelector( 'input[type=number]' ) ).toBeNull();//no cell editor
			expect( [...fixture.nativeElement.querySelectorAll( '.value-text' )].map( e=>e.textContent ) ).toEqual( ['5', '1', ''] );
			const open = vi.spyOn( WriteDialog, 'open' ).mockResolvedValue( undefined );
			fixture.nativeElement.querySelector( 'button[aria-label="Write q"]' ).click();
			expect( open.mock.calls[0][1].node ).toBe( writable );
		} );
	} );

	//install-issues #35.  The gate was `userAccessLevel < EAccess.Write` - a magnitude test on a bitmask, and one that read
	//JSON null as 0.  Against a third-party OPC server every value cell locked itself and the Access column drew nothing,
	//which is exactly what a `null` level does; and a read-only node carrying any higher bit offered an editor the server
	//would refuse.  The level is a mask, and "the server did not say" is not "the server said no".
	describe( 'the write gate', ()=>{
		const row = ( level:any )=>variable( 8, "w", {value: 1, userAccessLevel: level} );
		it( 'reads the CurrentWrite bit, not the magnitude', ()=>{
			const { page } = create();
			expect( page.readOnly(row(EAccess.Read)) ).toBe( true );
			expect( page.readOnly(row(EAccess.Read | EAccess.Write)) ).toBe( false );
			expect( page.readOnly(row(EAccess.Read | EAccess.HistoryRead)) ).toBe( true );//5 - "greater than Write", and read-only
			expect( page.readOnly(row(EAccess.Read | EAccess.StatusWrite)) ).toBe( true );//0x21 - StatusWrite is not CurrentWrite
		} );
		//a server that answers the AccessLevel read with nothing sends JSON null, which compared as 0 and locked the row.
		it( 'treats an unreported level as unknown, not as read-only', ()=>{
			const { page } = create();
			for( const unknown of [null, undefined] ){
				const r = row( unknown );
				expect( r.userAccessLevel ).toBeUndefined();//normalised at the boundary, so one rule covers both
				expect( page.readOnly(r) ).toBe( false );
				expect( page.readOnlyReason(r) ).toBe( "" );
			}
		} );
		it( 'unwraps a level the gateway sent with a status', ()=>{
			expect( row({v: EAccess.Read | EAccess.Write, sc: 0x40940600}).userAccessLevel ).toBe( EAccess.Read | EAccess.Write );
			expect( row({sc: 0x80340000}).userAccessLevel ).toBeUndefined();//a status and no reading says nothing about the level
		} );
		//the finding's own words: "the row should say 'the server reports this node read-only for you' rather than dim in silence"
		it( 'says why a cell is locked', ()=>{
			const { page } = create();
			expect( page.readOnlyReason(row(EAccess.Read)) ).toContain( "read-only for you" );
			expect( page.readOnlyReason(row(EAccess.Read)) ).toContain( "Read" );//and what it did grant
			expect( page.readOnlyReason(row(EAccess.Read | EAccess.Write)) ).toBe( "" );//nothing to explain
			expect( page.readOnlyReason(row(EAccess.None)) ).toBe( "" );//"no read access" is the cell's own text there
		} );
	} );
} );

//A Refresh assigns pageData.nodes, which NodeDetail's History tab gate never saw (historian-web-trend #10):  it hands them up.
//Clients 6b (#419, spec *Watch list*):  a Variable row's menu adds it to the gateway's watch list, and a `revised` result on the
//stream - what the server runs the node's item at - is the watch list's to show, no reading for the row.
describe( 'NodeChildren watch', ()=>{
	const create = ( added=true )=>{
		const gateway = Object.assign( fakeGateway( 'gw' ), { addToSubscription: vi.fn(), unsubscribe: vi.fn( async ()=>{} ) } );
		const snackbar = snackbarStub();
		const add = vi.fn( async ( _gateway:string, _opc:string, _node:NodeId )=>added );
		TestBed.configureTestingModule( {providers: [
			screenSize(),
			provideRoute(),
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: snackbar },
			{ provide: WatchStore, useValue: {add} },
			pageTitleStub
		]} );
		const page = TestBed.createComponent( NodeChildren ).componentInstance;
		vi.spyOn( TestBed.inject( ProfileStore ), 'save' ).mockResolvedValue();
		page.pageData = { gateway, route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		return { page, gateway, snackbar, add };
	};

	it( 'adds the row to the gateway\'s watch list and says so', async ()=>{
		const { page, snackbar, add } = create();
		await page.watch( X );
		expect( add.mock.calls ).toEqual( [['gw', 'local', X.nodeId]] );
		expect( snackbar.info ).toHaveBeenCalledWith( 'Watching x.' );
		expect( page.watchLink ).toEqual( ['/apps', 'gateways', 'gw', 'watch'] );
	} );

	it( 'says when the row is on the list already', async ()=>{
		const { page, snackbar } = create( false );
		await page.watch( X );
		expect( snackbar.info ).toHaveBeenCalledWith( 'x is on the watch list already.' );
	} );

	it( 'takes a revised result as no reading', async ()=>{
		const { page, gateway } = create();
		(<any>page).setNodes( [X], true );
		page.selections.select( X );
		await flush();
		gateway.values.next( {opcId: 'local', node: X.nodeId, value: 7} as any );
		expect( X.value ).toBe( 7 );
		gateway.values.next( {opcId: 'local', node: X.nodeId, revised: {samplingInterval: 250, queueSize: 2, sc: 0}} as any );
		expect( X.value ).toBe( 7 );
		expect( X.sc ).toBe( 0 );
	} );

	it( 'shows the row menu last, whatever the view', ()=>{
		const { page } = create();
		page.viewSet.reset( [NodeView.default()], NodeView.schema );
		expect( page.displayedColumns().at( -1 ) ).toBe( 'menu' );
	} );
} );

describe( 'NodeChildren refresh', ()=>{
	it( 'hands the refreshed nodes up', async ()=>{
		const fresh = [ variable( 3, "z" ) ];
		TestBed.configureTestingModule({ providers: [
			screenSize(),
			provideRoute(),
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: snackbarStub() },
			pageTitleStub
		]});
		const fixture = TestBed.createComponent( NodeChildren );
		fixture.componentRef.setInput( 'node', X );
		const page = fixture.componentInstance;
		page.pageData = { gateway: fakeGateway( 'local', {}, new Map([[X.key, fresh]]) ), route: {profileKey: 'k'}, server: {connection: {slug: 'local'}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		const handed:UaNode[][] = [];
		page.refreshed.subscribe( nodes=>handed.push(nodes) );
		await page.onRefresh();
		expect( handed ).toEqual( [fresh] );
	} );
} );

//reviews-issue/318-opc-calls.md #1:  only a code registered as pending is fetched, and the dialog's `names` fetched the pending set
//as it stood - which the call's codes were not yet in, so a refusal read "Bad 0x801F0000" for good.
describe( 'NodeChildren a Method\'s Call', ()=>{
	it( 'fetches the names of the codes the call answered', async ()=>{
		const gateway = new TestGateway();
		const code = 0x801F0000;//BadUserAccessDenied, which no other test here names
		gateway.names = [{sc: code, message: 'BadUserAccessDenied'}];
		TestBed.configureTestingModule({ providers: [screenSize(), provideRoute(), { provide: GATEWAY_SERVICE, useValue: {} }, { provide: SnackbarService, useValue: snackbarStub() }, pageTitleStub] });
		const fixture = TestBed.createComponent( NodeChildren );
		const page = fixture.componentInstance;
		page.pageData = { gateway, route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		const pump = new OpcObject( <any>{ns: 2, i: 5005, name: 'pumpManual', browse: {ns: 2, name: 'pumpManual'}} );
		fixture.componentRef.setInput( 'node', pump );
		const open = vi.spyOn( MethodCallDialog, 'open' ).mockResolvedValue( undefined );
		page.call( new Method( <any>{ns: 2, i: 6055, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}}, pump ) );
		open.mock.calls[0][1].names!( [code] );
		await flush( 3 );
		expect( gateway.gets.mock.calls.map( ([target])=>target.split( /[=,]/ ).slice( 1 ).map( Number ) ).flat() ).toContain( code );//asked for - TestGateway answers its names whatever was asked
		expect( OpcError.text(code) ).toBe( 'BadUserAccessDenied' );
	} );
} );

//reviews-issue/318-opc-calls.md #6:  a Method row's Call is in the Snapshot cell, and a view that hides Snapshot left none anywhere -
//so the Name cell carries it there, as the Snapshot cell's quality icon stands in for a hidden Status column.  One per row.
describe( 'NodeChildren a Method\'s Call button', ()=>{
	it( 'is in the Snapshot cell, or in the Name cell where the view hides Snapshot', async ()=>{
		TestBed.configureTestingModule({ providers: [screenSize(), provideRoute(), { provide: GATEWAY_SERVICE, useValue: {} }, { provide: SnackbarService, useValue: snackbarStub() }, pageTitleStub] });
		const fixture = TestBed.createComponent( NodeChildren );
		const page = fixture.componentInstance;
		const pump = new OpcObject( <any>{ns: 2, i: 5005, name: 'pumpManual', browse: {ns: 2, name: 'pumpManual'}} );
		const toHertz = new Method( <any>{ns: 2, i: 6055, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}, description: {text: 'Converts rpm to Hz.'}}, pump );
		page.pageData = { gateway: fakeGateway( 'local' ), route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		fixture.componentRef.setInput( 'node', pump );
		(<any>page).setNodes( [toHertz], false );
		page.isLoading.set( false );//the route's load, which provideRoute() leaves out
		const views = [NodeView.default(), NodeView.fromConfig({name: "no snapshot", configColumns: ["id", "name", "description"], sort: []}, NodeView.schema)];
		const cell = ( b:Element )=>b.closest( 'mat-cell' )?.classList.contains( 'mat-column-name' ) ? 'name' : b.closest( 'mat-cell' )?.classList.contains( 'mat-column-snapshot' ) ? 'snapshot' : '?';
		const calls = ()=>[...fixture.nativeElement.querySelectorAll( 'button[aria-label="Call toHertz"]' )].map( cell );
		page.viewSet.reset( views, NodeView.schema );
		page.viewSet.select( 0 );//whichever view an earlier test left remembered
		fixture.detectChanges();
		expect( calls() ).toEqual( ['snapshot'] );
		expect( fixture.nativeElement.querySelector( 'ql-list-settings, button[aria-label="Edit view"]' ) ).toBeNull();//#593:  no view editor
		page.viewSet.select( 1 );
		fixture.detectChanges();
		expect( page.snapshotShown() ).toBe( false );
		expect( calls() ).toEqual( ['name'] );
		const open = vi.spyOn( MethodCallDialog, 'open' ).mockResolvedValue( undefined );
		fixture.nativeElement.querySelector( 'button[aria-label="Call toHertz"]' ).click();
		expect( open.mock.calls[0][1].method ).toBe( toHertz );
	} );
} );

//the #421 walk's W1:  every value of a type of the server's own took the enumeration's select, so a structure showed an empty one;  with
//8b (#422) a structure is a table of its members, nested where they are, and a body no type explains says it is undecoded - whatever the
//DataType says, a BaseDataType variable's too, and an array of structures a table each (reviews-issue/422-structures.md #9)
describe( 'NodeChildren a value of the server\'s own type', ()=>{
	it( 'shows an enumeration by name and a structure as a table of its members', ()=>{
		TestBed.configureTestingModule({ providers: [screenSize(), provideRoute(), { provide: GATEWAY_SERVICE, useValue: {} }, { provide: SnackbarService, useValue: snackbarStub() }, pageTitleStub] });
		const fixture = TestBed.createComponent( NodeChildren );
		const page = fixture.componentInstance;
		const health = variable( 1, "health", {dataType: {ns: 2, i: 3002}, value: 1} );
		health.customDataType = new Enum( new NodeId({ns: 2, i: 3002}), <any>{name: "Health", enumValues: [{id: 1, name: "OK"}]} );
		const sample = variable( 2, "sample", {dataType: {ns: 2, i: 3001}, value: {Rpm: 1480.5, Running: true, Limits: {Low: 0, High: 3000}}} );
		const opaque = variable( 3, "legacy", {dataType: {ns: 2, i: 3009}, value: {typeId: {ns: 2, i: 3010}, body: "AAAA"}} );
		const any = variable( 4, "any", {dataType: {i: ETypes.BaseDataType}, value: {Rpm: 7}} );
		const args = variable( 5, "args", {dataType: {i: 296}, valueRank: 1, value: [{Name: "a"}, {Name: "b"}]} );
		page.pageData = { gateway: fakeGateway( 'local' ), route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		fixture.componentRef.setInput( 'node', new OpcObject(<any>{ns: 2, i: 5010, name: '_Test', browse: {ns: 2, name: '_Test'}}) );
		(<any>page).setNodes( [health, sample, opaque, any, args], false );
		page.isLoading.set( false );//the route's load, which provideRoute() leaves out
		page.viewSet.reset( [NodeView.default()], NodeView.schema );
		fixture.detectChanges();
		const snapshot = ( name:string ):Element=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )]
			.find( (r:Element)=>r.querySelector('.mat-column-name')?.textContent?.trim()==name )!.querySelector( '.mat-column-snapshot' )!;
		expect( snapshot('health').querySelector('.value-text')?.textContent ).toBe( 'OK' );//by name (Variable.valueName)
		expect( snapshot('health').querySelector('button[aria-label="Write health"]') ).not.toBeNull();
		expect( snapshot('sample').querySelector('button[aria-label="Write sample"]') ).not.toBeNull();//member by member, in the dialog
		const rows = ( table:Element )=>[...table.querySelectorAll( ':scope > tbody > tr' )].map( r=>[r.querySelector('th')!.textContent!.trim(), r.querySelector(':scope > td')!.textContent!.trim().replace( /\s+/g, ' ' )] );
		const table = snapshot('sample').querySelector( 'structure-table > table' )!;
		expect( table ).not.toBeNull();
		expect( rows(table) ).toEqual( [['Rpm', '1480.5'], ['Running', 'true'], ['Limits', 'Low0High3000']] );//the nested table's text, run together
		expect( rows(table.querySelector('structure-table > table')!) ).toEqual( [['Low', '0'], ['High', '3000']] );//nested
		expect( snapshot('legacy').querySelector('structure-table table') ).toBeNull();
		expect( snapshot('legacy').querySelector('.undecoded')!.textContent!.trim() ).toBe( 'undecoded ns=2;i=3010' );
		expect( snapshot('legacy').querySelector('.undecoded')!.getAttribute('tabindex') ).toBe( '-1' );//the reason a keyboard's too:  the table is a grid (#597), whose cell's Enter reaches it
		expect( rows(snapshot('any').querySelector('structure-table > table')!) ).toEqual( [['Rpm', '7']] );
		const elements = [...snapshot('args').querySelectorAll( 'structure-table > structure-table > table' )];
		expect( elements.map( t=>rows(t) ) ).toEqual( [[['Name', 'a']], [['Name', 'b']]] );
	} );
} );

//reviews-issue/416-address-space.md #5:  the toggle off hid a ns-0 Variable page's own row - the row the page is for - and the sidenav
//followed the toggle only at the next navigation.
describe( 'NodeChildren the System nodes toggle', ()=>{
	const create = ()=>{
		TestBed.configureTestingModule({ providers: [screenSize(), provideRoute(), { provide: GATEWAY_SERVICE, useValue: {} }, { provide: SnackbarService, useValue: snackbarStub() }, pageTitleStub] });
		const fixture = TestBed.createComponent( NodeChildren );
		const page = fixture.componentInstance;
		const self = new Variable( <any>{ns: 0, i: 2258, name: 'CurrentTime', browse: {ns: 0, name: 'CurrentTime'}} );
		const route = { profileKey: 'k', node: self };
		page.pageData = { gateway: fakeGateway( 'local' ), route, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		fixture.componentRef.setInput( 'node', self );
		page.viewSet.reset( [NodeView.default()], NodeView.schema );
		return { fixture, page, self };
	};

	it( "keeps a ns-0 Variable page's own row while it hides the other system nodes", ()=>{
		const { page, self } = create();
		const sibling = new Variable( <any>{ns: 0, i: 2257, name: 'StartTime', browse: {ns: 0, name: 'StartTime'}} );
		const own = new Variable( <any>{ns: 2, i: 30, name: 'mine', browse: {ns: 2, name: 'mine'}} );
		(<any>page).setNodes( [self, sibling, own], false );
		page.setShowSystem( false );
		expect( page.rows().map( n=>n.name ) ).toEqual( ['CurrentTime', 'mine'] );
		page.setShowSystem( true );
		expect( page.rows().map( n=>n.name ) ).toEqual( ['CurrentTime', 'mine', 'StartTime'] );
	} );

	//reviews-issue/416-address-space.md #12:  the Types folder's children are all system nodes, so its page was empty by default and said
	//"No node matches this view." - the view's filters, which weren't the cause
	//reviews-issue/596-views-group-by.md #15, ruled 2026-11-11:  the page's own row, first whatever the sort, started a group of its
	//own ahead of the others;  it goes in its group, which comes first
	it( "puts a Variable page's own row first in its group, which comes first", ()=>{
		const { page, self } = create();
		const bad = 0x808C0000;
		const node = ( i:number, name:string, sc:number )=>Object.assign( new Variable( <any>{ns: 2, i, name, browse: {ns: 2, name}} ), {value: 1, sc} );
		Object.assign( self, {value: 1, sc: 0} );
		(<any>page).setNodes( [self, node( 31, 'bad', bad ), node( 32, 'good', 0 )], false );
		page.viewSet.setGroupBy( "status" );
		const headed = page.tableRows().map( r=>isGroupHeader( r ) ? `${r.label} ${r.count}` : r.name );
		expect( headed ).toEqual( [`${OpcError.text(0)} 2`, "CurrentTime", "good", `${OpcError.text(bad)} 1`, "bad"] );
	} );
	//reviews-issue/596-views-group-by.md #7, ruled 2026-11-11:  select-all took the variables of a collapsed group
	it( "selects all over the rows on screen, a collapsed group's left as they were", ()=>{
		const { page } = create();
		const node = ( i:number, name:string, sc:number )=>Object.assign( new Variable( <any>{ns: 2, i, name, browse: {ns: 2, name}} ), {value: 1, sc} );
		const [bad, good, other] = [node( 31, 'bad', 0x808C0000 ), node( 32, 'good', 0 ), node( 33, 'other', 0 )];
		(<any>page).setNodes( [bad, good, other], false );
		page.viewSet.setGroupBy( "status" );
		page.selections.select( bad );
		page.viewSet.toggleGroup( `string:${OpcError.text(0x808C0000)}` );
		expect( page.isAllSelected() ).toBe( false );
		page.toggleAllRows();
		expect( [page.selections.selected, page.isAllSelected()] ).toEqual( [[bad, good, other], true] );
		page.toggleAllRows();
		expect( page.selections.selected ).toEqual( [bad] );
	} );
	it( 'says the toggle hid every row, and turns it on from there', ()=>{
		const { fixture, page } = create();
		const objectTypes = new OpcObject( <any>{ns: 0, i: 88, name: 'ObjectTypes', browse: {ns: 0, name: 'ObjectTypes'}, description: {text: ''}} );//as the browse answers it
		(<any>page).setNodes( [objectTypes], false );
		page.setShowSystem( false );
		page.isLoading.set( false );
		fixture.detectChanges();
		const empty = ()=>fixture.nativeElement.querySelector( '.jde-empty' );
		expect( empty()?.textContent ).toContain( 'System nodes are hidden.' );
		expect( empty()?.textContent ).not.toContain( 'No node matches this view.' );
		empty()!.querySelector( 'button' )!.click();
		fixture.detectChanges();
		expect( page.showSystem() ).toBe( true );
		expect( page.rows().map( n=>n.name ) ).toEqual( ['ObjectTypes'] );
		expect( empty() ).toBeNull();
	} );
	//reviews-issue/593-views-sort.md #1:  with no sort, MatSort was handed Name ascending, so Name's header showed a sort not applied
	it( 'shows no header sorted where the view has no sort', ()=>{
		const { fixture, page } = create();
		const node = ( i:number, name:string )=>new Variable( <any>{ns: 2, i, name, browse: {ns: 2, name}} );
		(<any>page).setNodes( [node( 31, 'b' ), node( 32, 'a' )], false );
		page.viewSet.edit( view=>{ view.sort = []; } );//`No sorting`
		page.isLoading.set( false );
		fixture.detectChanges();
		const sorted = [...( fixture.nativeElement as HTMLElement ).querySelectorAll( 'mat-header-cell[aria-sort]' )].filter( c=>c.getAttribute( 'aria-sort' )!='none' );
		expect( [page.rows().map( n=>n.name ), page.sort, sorted.length] ).toEqual( [['b', 'a'], {active: '', direction: ''}, 0] );
	} );
	it( 'says the view matched nothing when the toggle hid nothing', ()=>{
		const { page } = create();
		page.setShowSystem( false );
		(<any>page).setNodes( [], false );
		expect( page.hiddenBySystem() ).toBe( false );
	} );
	//reviews-issue/595-views-slice-panel.md #1:  the toggle's test left the slice out, so a value no row holds said the toggle hid the rows
	it( "says the toggle hid the rows only where it hid the slice's", ()=>{
		const { page } = create();
		const own = new Variable( <any>{ns: 2, i: 30, name: 'mine', browse: {ns: 2, name: 'mine'}} );
		const types = new OpcObject( <any>{ns: 0, i: 88, name: 'ObjectTypes', browse: {ns: 0, name: 'ObjectTypes'}} );
		(<any>page).setNodes( [own, types], false );
		page.setShowSystem( false );
		page.viewSet.edit( view=>{ view.sliceBy = "class"; } );
		page.viewSet.setSlice( "Object" );
		expect( [page.rows().length, page.hiddenBySystem()] ).toEqual( [0, true] );
		page.viewSet.setSlice( "Method" );
		expect( [page.rows().length, page.hiddenBySystem()] ).toEqual( [0, false] );
	} );

	//reviews-issue/416-address-space.md #15:  the toggle's state was kept in four places - this component's signal, LocalPrefs, the
	//store's copy and the resolver's re-read - and the store's drifted until the next navigation.  The store holds the one.
	it( "is the store's", ()=>{
		const { page } = create();
		page.setShowSystem( false );
		page.setShowSystem( true );
		expect( TestBed.inject( OpcStore ).showSystem() ).toBe( true );
		expect( page.showSystem ).toBe( TestBed.inject( OpcStore ).showSystem );
	} );
} );