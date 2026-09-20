if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute } from '@angular/router';
import { config, NEVER, Subject } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { GATEWAY_SERVICE, GatewayService, SubscriptionResult } from '../../../services/gateway-service';
import { NodeId } from '../../../model/node-id';
import { Variable } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { OpcError } from '../../../model/opc-error';
import { EAccess } from '../../../model/types';
import { NodeChildren } from './node-children';

//Variable's json arg is the wire shape; the UaNode base reads ns/i/name/browse straight off it.
const variable = ( id:number, name:string )=>new Variable( <any>{ns:2, i:id, name, browse:{ns:2, name}} );
const X = variable( 1, "x" ), Y = variable( 2, "y" );

describe( 'NodeChildren subscription values', ()=>{
	let page:NodeChildren;
	let pushes:Subject<SubscriptionResult>;
	let unsubscribed:{nodes:NodeId[]}[];
	let subscribes:number, adds:number, nameFetches:number;
	let written:any;//what the gateway's write/read stub answers with
	beforeEach( ()=>{
		pushes = new Subject<SubscriptionResult>();
		unsubscribed = [];
		subscribes = 0; adds = 0; nameFetches = 0;
		written = undefined;
		const gateway = {
			updateErrorCodes: ()=>{ ++nameFetches; return Promise.resolve(); },
			write: ()=>Promise.resolve( written ),
			read: ()=>Promise.resolve( written ),
			subscribe: ()=>{ ++subscribes; return pushes.asObservable(); },
			addToSubscription: ()=>{ ++adds; },
			unsubscribe: ( _cnnctn:string, nodes:NodeId[] )=>{ unsubscribed.push({nodes}); return Promise.resolve(); }
		};
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: NEVER} },//no ngOnInit load - the state is set by hand below
			{ provide: GATEWAY_SERVICE, useValue: {} },
			{ provide: SnackbarService, useValue: {exception: ()=>{}} },
			{ provide: ComponentPageTitle, useValue: {} }
		]});
		page = TestBed.createComponent( NodeChildren ).componentInstance;
		page.pageData = { gateway, route: {profileKey: 'k'}, server: {connection: {slug: 'local', defaultBrowseNs: 2}}, nodes: [] } as any;
		page.profile = { subscriptions: [] } as any;
	} );
	const setNodes = ( nodes:Variable[], resubscribe:boolean )=>(<any>page).setNodes( nodes, resubscribe );

	//angular-review3 #12: `variables.find(...)!.value = …` threw a TypeError inside the observer - and again on every publish
	//tick - once a re-browse had dropped the row the value was for.  RxJS swallows an observer throw into its unhandled-error
	//channel rather than out of next(), so that is where the old behaviour shows.
	it( 'ignores a value for a node that is no longer a row', async ()=>{
		const unhandled:any[] = [];
		const previous = config.onUnhandledError;
		config.onUnhandledError = e=>unhandled.push( e );
		try{
			setNodes( [X, Y], true );
			page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
			setNodes( [Y], false );//X is gone from the re-browse
			pushes.next( {opcId: 'local', node: X.nodeId, value: 7} as any );
			pushes.next( {opcId: 'local', node: Y.nodeId, value: 8} as any );
			await new Promise( r=>setTimeout(r, 0) );//onUnhandledError is reported on a later task
			expect( unhandled ).toEqual( [] );
			expect( Y.value ).toBe( 8 );//the surviving row still updates
		}
		finally{ config.onUnhandledError = previous; }
	} );

	//...and nothing unsubscribed the dropped node, so the server kept publishing it:  a new SelectionModel emits no `changed`.
	it( 'unsubscribes a persisted node the re-browse dropped, keeping the intent', ()=>{
		page.profile.subscriptions = [X.nodeId, Y.nodeId];
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
		unsubscribed.length = 0;
		setNodes( [Y], false );
		expect( unsubscribed ).toHaveLength( 1 );
		expect( unsubscribed[0].nodes.map(n=>n.key) ).toEqual( [X.key] );
		expect( page.profile.subscriptions.map(n=>n.key) ).toContain( X.key );//the intent survives, so a later browse re-subscribes
	} );

	it( 'unsubscribes nothing when every persisted node still has a row', ()=>{
		page.profile.subscriptions = [X.nodeId, Y.nodeId];
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X, Y], removed: []} as any );
		unsubscribed.length = 0;
		setNodes( [X, Y], false );
		expect( unsubscribed ).toHaveLength( 0 );
	} );

	//angular-review3 #13: an errored Subscription is dead but still truthy, so the next tick took the addToSubscription
	//branch - which builds a fresh gateway Subject with NO observer.  Values were dropped while the rows showed as live.
	it( 'drops the dead subscription when the socket errors it', ()=>{
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		expect( page.subscription ).toBeDefined();
		pushes.error( {message: "Connection to the gateway was lost."} );//Gateway.handleConnectionError
		expect( page.subscription ).toBeUndefined();
	} );

	it( 're-subscribes rather than adding to an observer-less Subject after a drop', ()=>{
		setNodes( [X, Y], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		pushes.error( {message: "Connection to the gateway was lost."} );
		subscribes = 0; adds = 0;
		pushes = new Subject<SubscriptionResult>();//the gateway builds a new Subject too
		page.onSubscriptionChange( {added: [Y], removed: []} as any );//the user re-ticks a row
		expect( subscribes ).toBe( 1 );
		expect( adds ).toBe( 0 );
		pushes.next( {opcId: 'local', node: Y.nodeId, value: 9} as any );
		expect( Y.value ).toBe( 9 );//the values actually arrive now
	} );

	it( 'drops it on complete too', ()=>{
		setNodes( [X], true );
		page.onSubscriptionChange( {added: [X], removed: []} as any );
		pushes.complete();
		expect( page.subscription ).toBeUndefined();
	} );

	//OPC 10000-4 7.38:  the push handler set `row.value` alone, so the quality the socket carried never reached the row - and a
	//Bad reading arrived as an OpcError that the number editor bound as its value.  Fresh rows:  X and Y above are shared.
	describe( 'quality', ()=>{
		const bad = 0x808C0000, uncertain = 0x40940600;
		let row:Variable;
		beforeEach( ()=>{
			row = new Variable( <any>{ns:2, i:7, name:"q", browse:{ns:2, name:"q"}, value: 5, userAccessLevel: EAccess.Read | EAccess.Write} );
			setNodes( [row], true );
			page.onSubscriptionChange( {added: [row], removed: []} as any );
		} );
		const push = ( reading:object )=>pushes.next( {opcId: 'local', node: row.nodeId, ...reading} as any );

		it( 'keeps the quality a push carries', ()=>{
			push( {value: 1500, sc: uncertain} );
			expect( row ).toMatchObject( {value: 1500, sc: uncertain} );
			expect( page.readOnly(row) ).toBe( false );//uncertain is still a reading, and still the user's to write over
			expect( page.qualityIcon(row) ).toBe( "warning" );
		} );

		it( 'holds the last value, locked, through a Bad reading', ()=>{
			push( {sc: bad} );
			expect( row ).toMatchObject( {value: 5, sc: bad} );
			expect( page.readOnly(row) ).toBe( true );
			expect( page.qualityIcon(row) ).toBe( "error" );
			expect( page.statusTooltip(row) ).toContain( "0x808C0000" );
			expect( page.statusCode(row) ).toBe( "0x808C0000" );//the Status cell's tooltip:  the code alone, beside the name the cell shows
		} );

		it( 'never puts a failure in the value', ()=>{
			push( {value: new OpcError(0x80340000, "Subscribe", "", undefined), sc: 0x80340000} );
			expect( row.value ).toBe( 5 );
			expect( row.sc ).toBe( 0x80340000 );
		} );

		it( 'unlocks on the next good reading', ()=>{
			push( {sc: bad} );
			push( {value: 6, sc: 0} );
			expect( row ).toMatchObject( {value: 6, sc: 0} );
			expect( page.readOnly(row) ).toBe( false );
			expect( page.qualityIcon(row) ).toBeUndefined();
			expect( page.status(row) ).toBe( "Good" );
			expect( page.statusTooltip(row) ).toBe( "" );//plain Good has nothing to add
			expect( page.statusCode(row) ).toBe( "" );
		} );

		//a fault holds its code on every tick it lasts - one request for its name, not one a second.
		it( 'asks for a code\'s name when the code changes, not on every push', ()=>{
			const code = 0x80AB0000;//one no other test has named
			push( {value: 1, sc: code} );
			push( {value: 2, sc: code} );
			push( {value: 3, sc: code + 0x0600} );//the same name, flagged - still a change of code
			expect( nameFetches ).toBe( 2 );
			push( {value: 4, sc: 0} );
			expect( nameFetches ).toBe( 2 );//Good needs no name
		} );

		//the icon beside the value stands in for the Status column - beside it, it would only say the same thing twice.
		it( 'leaves the quality to the Status column where the view shows one', ()=>{
			page.views.set( [NodeView.default(), new NodeView({name: "quality", configColumns: ["id", "name", "snapshot", "status"], sort: []}, NodeView.schema)] );
			expect( page.statusShown() ).toBe( false );//the default view hides Status, so the icon is what says it
			page.viewIndex.set( 1 );
			expect( page.statusShown() ).toBe( true );
		} );

		it( 'takes a write\'s echo as a reading, quality and all', async ()=>{
			written = {value: 9, sc: uncertain};
			await page.changeDouble( row, {target: {value: "9"}} as any );
			expect( row ).toMatchObject( {value: 9, sc: uncertain} );
		} );
	} );

	//install-issues #35.  The gate was `userAccessLevel < EAccess.Write` - a magnitude test on a bitmask, and one that read
	//JSON null as 0.  Against a third-party OPC server every value cell locked itself and the Access column drew nothing,
	//which is exactly what a `null` level does; and a read-only node carrying any higher bit offered an editor the server
	//would refuse.  The level is a mask, and "the server did not say" is not "the server said no".
	describe( 'the write gate', ()=>{
		const row = ( level:any )=>new Variable( <any>{ns:2, i:8, name:"w", browse:{ns:2, name:"w"}, value: 1, userAccessLevel: level} );
		it( 'reads the CurrentWrite bit, not the magnitude', ()=>{
			expect( page.readOnly(row(EAccess.Read)) ).toBe( true );
			expect( page.readOnly(row(EAccess.Read | EAccess.Write)) ).toBe( false );
			expect( page.readOnly(row(EAccess.Read | EAccess.HistoryRead)) ).toBe( true );//5 - "greater than Write", and read-only
			expect( page.readOnly(row(EAccess.Read | EAccess.StatusWrite)) ).toBe( true );//0x21 - StatusWrite is not CurrentWrite
		} );
		//a server that answers the AccessLevel read with nothing sends JSON null, which compared as 0 and locked the row.
		it( 'treats an unreported level as unknown, not as read-only', ()=>{
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
			expect( page.readOnlyReason(row(EAccess.Read)) ).toContain( "read-only for you" );
			expect( page.readOnlyReason(row(EAccess.Read)) ).toContain( "Read" );//and what it did grant
			expect( page.readOnlyReason(row(EAccess.Read | EAccess.Write)) ).toBe( "" );//nothing to explain
			expect( page.readOnlyReason(row(EAccess.None)) ).toBe( "" );//"no read access" is the cell's own text there
		} );
	} );
} );

//MVP first-run:  an identity with no role gets userAccessLevel 0 on every node, and the Snapshot cell showed a blank that
//read as a null value.  A row the browse never gave a level is not a denial.
describe( 'NodeView.readDenied', ()=>{
	it( 'flags a variable the server reports no Read right on, and nothing else', ()=>{
		const denied = new Variable( <any>{ns:2, i:3, name:"d", browse:{ns:2, name:"d"}, userAccessLevel: EAccess.None} );
		const readable = new Variable( <any>{ns:2, i:4, name:"r", browse:{ns:2, name:"r"}, userAccessLevel: EAccess.Read} );
		expect( NodeView.readDenied(denied) ).toBe( true );
		expect( NodeView.readDenied(readable) ).toBe( false );
		expect( NodeView.readDenied(variable(5, "u")) ).toBe( false );//no level in the row
	} );
} );