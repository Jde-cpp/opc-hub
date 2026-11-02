import { TestBed } from '@angular/core/testing';
import Long from 'long';
import { config } from 'rxjs';
import { DateUtils, ProtoUtils, SnackbarService, Timestamp, ViewType } from 'jde-framework';
import { ProfileStore } from 'jde-spa';
import { GATEWAY_SERVICE } from '../../../services/gateway-service';
import { NodeId } from '../../../model/node-id';
import { UaNode, Variable } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { OpcError } from '../../../model/opc-error';
import { EAccess, ETypes } from '../../../model/types';
import { NodeChildren } from './node-children';
import { flush, pageTitleStub, provideRoute, silence, screenSize } from 'jde-spa/testing';
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
			page.viewSet.reset( [NodeView.default()] );//the rows are the view's
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

	//clients spec, Gaps #9:  a 64-bit integer went as a JS number, which holds 53 bits - `+text` rounded what was typed past
	//2^53 before the gateway saw it.  It goes as a Long built from the text, the {low,high,unsigned} form Value::Set reads back.
	describe( 'a 64-bit integer', ()=>{
		const row = ( type:ETypes, value:any=5 )=>variable( 9, "big", {dataType: {i: type}, value} );
		const sent = ( gateway:ReturnType<typeof create>["gateway"] )=>gateway.write.mock.calls[0][2];
		it( 'goes as a Long built from the text, exact past 2^53', async ()=>{
			const { page, gateway, answer } = create();
			answer( {value: Long.fromString("9007199254740993"), sc: 0} );
			await page.changeDouble( row(ETypes.Int64), {target: {value: "9007199254740993"}} as any );
			expect( sent(gateway) ).toBeInstanceOf( Long );
			expect( sent(gateway).toString() ).toBe( "9007199254740993" );
			expect( (sent(gateway) as Long).unsigned ).toBe( false );
		} );
		it( 'takes a negative, and an unsigned one to the top of its range', async ()=>{
			const { page, gateway, answer } = create();
			answer( {value: Long.fromString("-9223372036854775808"), sc: 0} );
			await page.changeDouble( row(ETypes.Int64), {target: {value: "-9223372036854775808"}} as any );
			answer( {value: Long.fromString("18446744073709551615", true), sc: 0} );
			await page.changeDouble( row(ETypes.UInt64), {target: {value: "18446744073709551615"}} as any );
			expect( gateway.write.mock.calls.map( c=>[c[2].toString(), (c[2] as Long).unsigned] ) ).toEqual( [["-9223372036854775808", false], ["18446744073709551615", true]] );
		} );
		it( 'writes nothing for text that is not a whole number in range, and shows the row\'s value again', async ()=>{
			const { page, gateway } = create();
			for( const [type, text] of [[ETypes.Int64, "1.5"], [ETypes.Int64, "9223372036854775808"], [ETypes.UInt64, "-1"], [ETypes.UInt64, "18446744073709551616"], [ETypes.Int64, "abc"]] as [ETypes, string][] ){
				const box = {value: text};
				await page.changeDouble( row(type), {target: box} as any );
				expect( box.value ).toBe( "5" );
			}
			expect( gateway.write ).not.toHaveBeenCalled();
		} );
		it( 'still sends a 32-bit integer and a double as numbers', async ()=>{
			const { page, gateway, answer } = create();
			answer( {value: 7, sc: 0} );
			await page.changeDouble( row(ETypes.Int32), {target: {value: "7"}} as any );
			answer( {value: 7.5, sc: 0} );
			await page.changeDouble( row(ETypes.Double), {target: {value: "7.5"}} as any );
			expect( gateway.write.mock.calls.map( c=>c[2] ) ).toEqual( [7, 7.5] );
		} );
		//reviews-issue/412-clients-web-fixes.md #2:  `+""` is 0, so a cleared box wrote 0 to the device
		it( 'writes nothing for a cleared box of any numeric type, and shows the row\'s value again', async ()=>{
			const { page, gateway } = create();
			for( const type of [ETypes.Int32, ETypes.Double, ETypes.Byte, ETypes.Int64] ){
				const box = {value: ""};
				await page.changeDouble( row(type), {target: box} as any );
				expect( box.value ).toBe( "5" );
			}
			expect( gateway.write ).not.toHaveBeenCalled();
		} );
	} );

	//clients spec, Gaps #7:  the date picker wrote the start of the day, in UTC - a DateTime takes the date and time typed, to
	//the millisecond, in the browser's local time, as the history edit dialog's times do.
	describe( 'the DateTime editor', ()=>{
		const typed = new Date( 2026, 9, 9, 13, 4, 5, 6 );//local time
		const row = ( at?:Date )=>variable( 9, "when", {dataType: {i: ETypes.DateTime}, value: at ? ProtoUtils.fromDate( at ) : undefined} );
		it( 'writes the date and time typed, to the millisecond', async ()=>{
			const { page, gateway, answer } = create();
			answer( {value: ProtoUtils.fromDate( typed ), sc: 0} );
			await page.changeDate( row(new Date(2026, 9, 9)), {target: {value: DateUtils.toLocalInput( typed )}} as any );
			expect( ProtoUtils.toDate( gateway.write.mock.calls[0][2] as Timestamp )?.getTime() ).toBe( typed.getTime() );
		} );
		it( 'shows the row\'s value in local time to the millisecond, and nothing for none', ()=>{
			const { page } = create();
			expect( page.dateValue( row(typed) ) ).toBe( "2026-10-09T13:04:05.006" );
			expect( page.dateValue( row() ) ).toBe( "" );
		} );
		it( 'writes nothing for a cleared box, and shows the row\'s value again', async ()=>{
			const { page, gateway } = create();
			const box = {value: ""};
			await page.changeDate( row(typed), {target: box} as any );
			expect( gateway.write ).not.toHaveBeenCalled();
			expect( box.value ).toBe( "2026-10-09T13:04:05.006" );
		} );
		it( 'writes nothing for a box left as it was', async ()=>{
			const { page, gateway } = create();
			await page.changeDate( row(typed), {target: {value: "2026-10-09T13:04:05.006"}} as any );
			expect( gateway.write ).not.toHaveBeenCalled();
		} );
		//reviews-issue/412-clients-web-fixes.md #1:  `change` fires on each part of the date edited, so typing a year wrote the
		//years on the way to it.  The box writes on blur, and Enter blurs it.
		it( 'writes when the box is left, not on its change event', ()=>{
			const { page, fixture } = create();
			const write = vi.spyOn( page, 'changeDate' ).mockResolvedValue( undefined );
			const n = row( typed );
			page.viewSet.reset( [NodeView.fromConfig( {name: "value", configColumns: ["name", "snapshot"], sort: []}, NodeView.schema )] );
			page.nodes.set( [n] );
			page.isLoading.set( false );
			fixture.detectChanges();
			const box = <HTMLInputElement>fixture.nativeElement.querySelector( 'input[type=datetime-local]' );
			expect( box.getAttribute( 'aria-label' ) ).toBe( "when" );
			box.value = "0002-10-09T13:04:05.006";
			box.dispatchEvent( new Event('change') );
			expect( write ).not.toHaveBeenCalled();
			box.dispatchEvent( new KeyboardEvent('keydown', {key: 'Enter'}) );
			box.dispatchEvent( new Event('blur') );
			expect( write ).toHaveBeenCalledTimes( 1 );
		} );
		//reviews-issue/412-clients-web-fixes.md #9:  the text went through new Date, which takes the hour DST repeats as its earlier half
		describe( 'in the hour DST repeats', ()=>{
			beforeEach( ()=>vi.stubEnv( 'TZ', 'America/New_York' ) );
			afterEach( ()=>vi.unstubAllEnvs() );
			it( 'keeps an edit of a time in the later half there', async ()=>{
				const { page, gateway, answer } = create();
				const later = new Date( Date.UTC(2026, 10, 1, 6, 30) );//01:30 EST
				answer( {value: ProtoUtils.fromDate( later ), sc: 0} );
				await page.changeDate( row(later), {target: {value: "2026-11-01T01:30:00.500"}} as any );
				expect( ProtoUtils.toDate( gateway.write.mock.calls[0][2] as Timestamp )?.getTime() ).toBe( later.getTime() + 500 );
			} );
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

//reviews-issue/web-refactor-review.md #6:  a refused Delete said "Could not save view."
describe( 'NodeChildren Delete', ()=>{
	it( 'says the delete failed, not a save', async ()=>{
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [
			screenSize(),
			provideRoute(),
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: snackbar },
			pageTitleStub
		]});
		const page = TestBed.createComponent( NodeChildren ).componentInstance;
		const mine = NodeView.copy( NodeView.default() );
		mine.name = "Mine";
		mine.type = ViewType.User;
		page.viewSet.reset( [NodeView.default(), mine], 1 );
		vi.spyOn( TestBed.inject(ProfileStore), 'save' ).mockRejectedValue( new Error( 'offline' ) );
		page.onViewDelete( mine );
		await flush();
		expect( snackbar.exception.mock.calls ).toEqual( [["Could not delete view.", expect.any( Error )]] );
	} );
} );