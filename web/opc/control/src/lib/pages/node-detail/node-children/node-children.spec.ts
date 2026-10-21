import { TestBed } from '@angular/core/testing';
import { config } from 'rxjs';
import { SnackbarService } from 'jde-framework';
import { GATEWAY_SERVICE } from '../../../services/gateway-service';
import { NodeId } from '../../../model/node-id';
import { UaNode, Variable } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { OpcError } from '../../../model/opc-error';
import { EAccess } from '../../../model/types';
import { NodeChildren } from './node-children';
import { flush, pageTitleStub, provideRoute, silence } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
import { fakeGateway, variable } from 'jde-opc/testing';

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
			provideRoute(),//no ngOnInit load - the state is set by hand below
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: snackbar },
			pageTitleStub
		]});
		const page = TestBed.createComponent( NodeChildren ).componentInstance;
		page.pageData = { gateway, route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
		return {
			page, gateway, snackbar,
			setNodes: ( nodes:Variable[], resubscribe:boolean )=>(<any>page).setNodes( nodes, resubscribe ),
			unsubscribed: ()=>gateway.unsubscribe.mock.calls.map( c=>c[1] ),
			answer: ( reading:any )=>{ written = reading; }
		};
	};

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
			page.viewSet.reset( views );
			expect( page.statusShown() ).toBe( false );//the default view hides Status, so the icon is what says it
			page.viewSet.reset( views, 1 );
			expect( page.statusShown() ).toBe( true );
		} );

		it( 'takes a write\'s echo as a reading, quality and all', async ()=>{
			const { page, answer, row } = subscribed();
			answer( {value: 9, sc: uncertain} );
			await page.changeDouble( row, {target: {value: "9"}} as any );
			expect( row ).toMatchObject( {value: 9, sc: uncertain} );
		} );
	} );

	//A21:  the five editors wrote through one #write, which leaves the control showing the row's reading - the echo, or after a
	//failure the value read back - since a binding only re-applies on a change, and a refused write kept the typed text.
	describe( 'writes', ()=>{
		const row = ( value:any )=>variable( 7, "q", {value} );
		it( 'puts the value read back in a box whose write failed', async ()=>{
			const { page, gateway, snackbar, answer } = create();
			const n = row( "a" ), box = {value: "typed"};
			gateway.write.mockRejectedValue( new Error("refused") );
			answer( {value: "b", sc: 0} );
			await page.changeString( n, {target: box} as any );
			expect( snackbar.exception ).toHaveBeenCalledWith( "Could not write q.", expect.any(Error) );
			expect( n.value ).toBe( "b" );
			expect( box.value ).toBe( "b" );
		} );
		it( 'keeps the row\'s value when the re-read fails too', async ()=>{
			const { page, gateway } = create();
			const n = row( 5 ), box = {value: "9"};
			const warn = silence();
			gateway.write.mockRejectedValue( new Error("Connection to the gateway was lost.") );
			gateway.read.mockRejectedValue( new Error("Connection to the gateway was lost.") );
			await page.changeDouble( n, {target: box} as any );
			expect( warn ).toHaveBeenCalled();
			expect( box.value ).toBe( "5" );
			warn.mockRestore();
		} );
		it( 'shows the echo on the checkbox, not the click', async ()=>{
			const { page, answer } = create();
			const n = row( false ), source = {checked: true};
			answer( {value: false, sc: 0} );//the server kept its value
			await page.toggleValue( n, {checked: true, source} as any );
			expect( source.checked ).toBe( false );
			answer( {value: true, sc: 0} );
			source.checked = true;
			await page.toggleValue( n, {checked: true, source} as any );
			expect( n.value ).toBe( true );
			expect( source.checked ).toBe( true );
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
describe( 'NodeChildren refresh', ()=>{
	it( 'hands the refreshed nodes up', async ()=>{
		const fresh = [ variable( 3, "z" ) ];
		TestBed.configureTestingModule({ providers: [
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