import Long from 'long';
import { NodeId } from './node-id';
import { OpcError } from './opc-error';
import { alignUp, applyHistModification, applyHistModifications, editRefused, editRefusedAll, editValueJson, histMemberIndexed, histPathText, HistPushedModification, histResumeReach, histResumeTime, histThresholdBlank, histThresholdFieldText, histThresholdText, HistValue, isNoData, latestHistTimes, mergeHistValues, parseHistPath, pushValue, qlTime, toHistAggregates, toHistEditResult, toHistPage, toHistThresholds } from './hist';
import { histValue, nodeA as A, nodeB as B } from 'jde-opc/testing';

const keyOf = ( v:HistValue )=>`${v.node.toString()}@${v.source?.getTime() ?? "none"}`;
const at = ( node:NodeId, ms:number|null, status=0 )=>histValue( node, ms, ms ?? undefined, {status} );

describe( 'toHistPage', ()=>{
	it( 'reads times, status, value and the node list off a page', ()=>{
		const page = toHistPage( {
			continuation: 'abc',
			values: [
				{ node: {ns:2, i:1}, source: {seconds: 1700000000, nanos: 500000000}, server: null, status: 0x40000000, value: 1.5, bound: true, heartbeat: false },
				{ node: {ns:2, i:1}, source: {seconds: 1700000001, nanos: 0}, value: {low: 7, high: 0, unsigned: false} },//status omitted = Good;  a Long rides as its parts
				{ node: {ns:2, i:1}, source: {seconds: 1700000002, nanos: 0}, status: 0x80000000, value: null, modification: {time: {seconds: 1700000003, nanos: 0}, type: 'Replace', user: 'me'} }
			],
			nodes: [ {node: {ns:2, i:1}, status: 0x00A50000} ]
		} );
		expect( page.continuation ).toBe( 'abc' );
		expect( page.values ).toHaveLength( 3 );
		expect( page.values[0].source?.getTime() ).toBe( 1700000000500 );
		expect( page.values[0].server ).toBeNull();
		expect( page.values[0].status ).toBe( 0x40000000 );
		expect( page.values[0].bound ).toBe( true );
		expect( page.values[1].status ).toBe( 0 );
		expect( page.values[1].value ).toBeInstanceOf( Long );
		expect( (<Long>page.values[1].value).toNumber() ).toBe( 7 );
		expect( page.values[2].value ).toBeUndefined();//Bad with no value
		expect( page.values[2].modification ).toEqual( {time: new Date(1700000003000), type: 'Replace', user: 'me'} );
		expect( page.nodes[0].node.key ).toBe( A.key );
		expect( page.nodes[0].status ).toBe( 0x00A50000 );
	} );
	//A record with no source time had the key `node@none`, so the merge kept one per node (historian-web-trend #5).  It takes
	//the server's time, as the gateway's merge orders it and pushValue places a push.
	it( 'places a record with no source time at its server time', ()=>{
		const page = toHistPage( {values: [
			{ node: {ns:2, i:1}, server: {seconds: 1700000000, nanos: 0} },
			{ node: {ns:2, i:1}, server: {seconds: 1700000001, nanos: 0} },
			{ node: {ns:2, i:1} }//neither
		]} );
		expect( page.values.map( v=>v.source?.getTime() ?? null ) ).toEqual( [1700000000000, 1700000001000, null] );
		expect( page.values.map( v=>v.sourceTime ) ).toEqual( [undefined, undefined, undefined] );//a stand-in, which no edit names
		expect( page.values[0].server?.getTime() ).toBe( 1700000000000 );
		expect( mergeHistValues( [], page.values ) ).toHaveLength( 3 );
	} );
	//The table showed a Bad record's value as its status again (historian-web-trend #6).
	it( "reads a Bad record's status-only value as none", ()=>{
		const page = toHistPage( {values: [{ node: {ns:2, i:1}, source: {seconds: 1700000000, nanos: 0}, status: 0x80000000, value: {sc: 0x80000000} }]} );
		expect( page.values[0].value ).toBeUndefined();
		expect( page.values[0].status ).toBe( 0x80000000 );
	} );
	it( 'is empty for a missing result', ()=>{
		expect( toHistPage( undefined ) ).toEqual( {values: [], continuation: null, nodes: []} );
	} );
	//A row's Delete and Replace sent the Date, cut to the millisecond, so they named a time with no record (historian-web-edits #1).
	it( 'keeps the source time to the tick, which the Date cuts to the millisecond', ()=>{
		const page = toHistPage( {values: [{ node: {ns:2, i:1}, source: {seconds: 1700000000, nanos: 123400}, value: 1 }]} );
		expect( page.values[0].source?.getTime() ).toBe( 1700000000000 );
		expect( page.values[0].sourceTime ).toEqual( {seconds: 1700000000, nanos: 123400} );
	} );
} );

describe( 'toHistAggregates', ()=>{
	//the folder's objects by browse name, Part 13's and the server's own, in its order;  a name from the display name
	it( 'lists the objects of the AggregateFunctions folder, by display and browse name', ()=>{
		const list = toHistAggregates( {children: [
			{ id: {i: 2342}, name: {locale: 'en', text: 'Average'}, browse: {ns: 0, name: 'Average'}, nodeClass: 1 },
			{ id: {ns: 1, s: 'Median'}, name: 'Median', browse: {ns: 1, name: 'Median'}, nodeClass: 1 },
			{ id: {i: 3}, browse: {ns: 0, name: 'Count'}, nodeClass: 1 },//no display name:  the browse name stands in
			{ id: {i: 5}, name: {locale: 'en', text: ''}, browse: {ns: 0, name: 'Range'}, nodeClass: 1 },//nor for one with no text (historian-aggregate-picker #10)
			{ id: {i: 6}, name: {locale: 'en'}, browse: {ns: 0, name: 'Delta'}, nodeClass: 1 },
			{ id: {i: 4}, name: {text: 'Icon'}, browse: {ns: 0, name: 'Icon'}, nodeClass: 2 }//a variable is no aggregate
		]} );
		expect( list ).toEqual( [{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}, {name: 'Count', browse: 'Count'}, {name: 'Range', browse: 'Range'}, {name: 'Delta', browse: 'Delta'}] );
		expect( toHistAggregates( undefined ) ).toEqual( [] );
	} );
} );

describe( 'alignUp', ()=>{
	it( 'rounds up to the interval boundary on the local clock, and leaves a boundary where it is', ()=>{
		const t = new Date( 2026, 9, 10, 13, 4, 5, 6 ).getTime();
		const minute = alignUp( t, 60_000 );
		expect( minute ).toBe( new Date( 2026, 9, 10, 13, 5 ).getTime() );
		expect( alignUp( minute, 60_000 ) ).toBe( minute );
		expect( alignUp( t, 3_600_000 ) ).toBe( new Date( 2026, 9, 10, 14 ).getTime() );
		expect( alignUp( t, 86_400_000 ) ).toBe( new Date( 2026, 9, 11 ).getTime() );//local midnight, whatever the zone's offset
		expect( alignUp( t, 1000 ) ).toBe( new Date( 2026, 9, 10, 13, 4, 6 ).getTime() );
	} );
} );

describe( 'isNoData', ()=>{
	it( 'is Bad_NoData with any flags, and nothing else', ()=>{
		expect( isNoData( 0x809B0000 ) ).toBe( true );
		expect( isNoData( 0x809B0404 ) ).toBe( true );
		expect( isNoData( 0x80000000 ) ).toBe( false );
		expect( isNoData( 0 ) ).toBe( false );
	} );
} );

describe( 'qlTime', ()=>{
	it( 'is plain seconds and nanos, not a Long', ()=>{
		const t = qlTime( new Date(1700000000500) );
		expect( t ).toEqual( {seconds: 1700000000, nanos: 500000000} );
		expect( typeof t.seconds ).toBe( 'number' );
		expect( JSON.parse(JSON.stringify(t)) ).toEqual( t );
	} );
	it( 'passes a time to the tick through', ()=>{
		expect( qlTime( {seconds: 1700000000, nanos: 123400} ) ).toEqual( {seconds: 1700000000, nanos: 123400} );
	} );
} );

describe( 'mergeHistValues', ()=>{
	it( 'keeps one record per node and source time, in time order', ()=>{
		const existing = [ at(A, 1000), at(A, 3000) ];
		const merged = mergeHistValues( existing, [at(A, 3000), at(A, 2000), at(B, 3000), at(A, 4000)] );
		expect( merged.map(keyOf) ).toEqual( [at(A,1000), at(A,2000), at(A,3000), at(B,3000), at(A,4000)].map(keyOf) );
	} );
	it( 'keeps one of a record twice in the values merged', ()=>{
		expect( mergeHistValues( [], [at(A, 2000), at(B, 2000), at(A, 2000)] ).map(keyOf) ).toEqual( [at(A,2000), at(B,2000)].map(keyOf) );
	} );
	it( 'puts an earlier page in its place', ()=>{
		const existing = [ at(A, 3000), at(B, 3000), at(A, 5000) ];
		const merged = mergeHistValues( existing, [at(B, 4000), at(A, 1000), at(A, 3000), at(B, 2000)] );
		expect( merged.map(keyOf) ).toEqual( [at(A,1000), at(B,2000), at(A,3000), at(B,3000), at(B,4000), at(A,5000)].map(keyOf) );
		expect( existing.map(keyOf) ).toEqual( [at(A,3000), at(B,3000), at(A,5000)].map(keyOf) );//not touched:  a new array
	} );
	//Every push stringified each NodeId held to rebuild the keys, and re-sorted the lot:  hundreds of milliseconds a push after
	//a few hours of Live (historian-web-trend #11).  A push is looked up by time, and a newest one appended.
	it( 'appends a newest push without stringifying what it holds', ()=>{
		const existing = Array.from( {length: 1000}, (_, i)=>at(i%2 ? A : B, i) );
		existing.forEach( v=>v.node.key );//cached, as the trend's draws leave them
		const toString = vi.spyOn( NodeId.prototype, 'toString' );
		try{
			const merged = mergeHistValues( existing, [at(A, 5000)] );
			expect( toString ).not.toHaveBeenCalled();
			expect( merged ).toHaveLength( 1001 );
			expect( merged.at(-1)!.source!.getTime() ).toBe( 5000 );
			expect( mergeHistValues( merged, [at(A, 999)] ) ).toBe( merged );//held:  found by time, not by key
		}
		finally{ toString.mockRestore(); }
	} );
	it( 'returns the same array when nothing is new, so a signal stays quiet', ()=>{
		const existing = [ at(A, 1000), at(A, 2000) ];
		expect( mergeHistValues( existing, [at(A, 1000)] ) ).toBe( existing );
		expect( mergeHistValues( existing, [] ) ).toBe( existing );
	} );
	//The live tail's first push is the newest value the opening read also holds.  Arriving first, it kept its place and its
	//Date, so the row offered no edit (historian-web-edits #1).
	it( "takes a read's record in place of its push, and keeps it over a later push", ()=>{
		const pushed = at( A, 2000 ), read = { ...at(A, 2000), sourceTime: {seconds: 2, nanos: 123400} };
		const existing = [ at(A, 1000), pushed ];
		const merged = mergeHistValues( existing, [read] );
		expect( merged ).not.toBe( existing );
		expect( merged.map( v=>v.sourceTime ) ).toEqual( [undefined, read.sourceTime] );
		expect( existing[1] ).toBe( pushed );//not touched:  a new array
		expect( mergeHistValues( merged, [at(A, 2000)] ) ).toBe( merged );
	} );
	it( 'puts a value with no source time last', ()=>{
		const merged = mergeHistValues( [at(A, null)], [at(A, 1000)] );
		expect( merged.map(v=>v.source?.getTime() ?? null) ).toEqual( [1000, null] );
	} );
	//A modified read holds several records of one node at one source time - the Insert at it and the Delete that took it back -
	//which the one-record-per-node-and-time rule folded into one.  They are told apart by the edit that made them.
	it( 'keeps the modifications of one node at one time apart, and still folds a repeat', ()=>{
		const mod = ( type:string, ms:number )=>({ ...at(A, 2000), modification: {time: new Date(ms), type, user: 'me'} });
		const inserted = mod( 'Insert', 9000 ), deleted = mod( 'Delete', 9500 );
		const merged = mergeHistValues( [inserted], [deleted, inserted] );
		expect( merged.map( v=>v.modification!.type ) ).toEqual( ['Insert', 'Delete'] );
		expect( mergeHistValues( merged, [mod( 'Delete', 9500 )] ) ).toBe( merged );
		expect( mergeHistValues( [], [deleted, inserted, mod( 'Insert', 9000 )] ) ).toHaveLength( 2 );
	} );
} );

describe( 'toHistEditResult', ()=>{
	it( 'reads each value\'s result and each node\'s entry status', ()=>{
		const r = toHistEditResult( { values: [{node: {ns:2, i:1}, source: {seconds: 1, nanos: 0}, status: 0x00A20000}, {node: {ns:2, i:1}, source: null}], nodes: [{node: {ns:2, i:1}, status: 0}] } );
		expect( r.values ).toHaveLength( 2 );
		expect( r.values[0].source?.getTime() ).toBe( 1000 );
		expect( r.values[0].status ).toBe( 0x00A20000 );
		expect( r.values[1].node.toString() ).toBe( A.toString() );//not toEqual on a NodeId:  its key is a Symbol of its own once asked for
		expect( r.values[1] ).toMatchObject( {source: null, status: 0} );
		expect( r.nodes.map( n=>[n.node.toString(), n.status] ) ).toEqual( [[A.toString(), 0]] );
		expect( toHistEditResult( undefined ) ).toEqual( {values: [], nodes: []} );
	} );
} );

describe( 'editRefused', ()=>{
	it( 'is a Bad in any value or node, and not a Good_EntryInserted', ()=>{
		expect( editRefused( {values: [{node: A, source: null, status: 0x00A20000}], nodes: [{node: A, status: 0}]} ) ).toBe( false );
		expect( editRefused( {values: [{node: A, source: null, status: 0x809F0000}], nodes: [{node: A, status: 0}]} ) ).toBe( true );//Bad_EntryExists
		expect( editRefused( {values: [], nodes: [{node: A, status: 0x809B0000}]} ) ).toBe( true );//a range purge's Bad_NoData, per node alone
	} );
} );

//A range purge over two nodes, one with nothing in the range, deleted the other's values and was shown as refused, so the
//dialog stayed open and the tab kept showing what was gone (historian-web-edits #2).
describe( 'editRefusedAll', ()=>{
	it( 'is every value Bad, or for a range purge every node, and nothing for no answer', ()=>{
		expect( editRefusedAll( {values: [], nodes: [{node: A, status: 0}, {node: B, status: 0x809B0000}]} ) ).toBe( false );
		expect( editRefusedAll( {values: [], nodes: [{node: A, status: 0x809B0000}, {node: B, status: 0x809B0000}]} ) ).toBe( true );
		expect( editRefusedAll( {values: [{node: A, source: null, status: 0x00A20000}, {node: A, source: null, status: 0x809F0000}], nodes: [{node: A, status: 0}]} ) ).toBe( false );
		expect( editRefusedAll( {values: [{node: A, source: null, status: 0x809F0000}], nodes: [{node: A, status: 0}]} ) ).toBe( true );//the values, not the node's entry
		expect( editRefusedAll( {values: [], nodes: []} ) ).toBe( false );
	} );
} );

describe( 'editValueJson', ()=>{
	it( 'carries the node, the source time as plain seconds and nanos, the value, and the server time and status only when given', ()=>{
		expect( editValueJson( {node: A, source: new Date(1700000000500), value: 2.5} ) ).toEqual( {node: A.toJson(), source: {seconds: 1700000000, nanos: 500000000}, value: 2.5} );
		expect( editValueJson( {node: A, source: new Date(1000), server: new Date(2000), status: 0x40000000, value: true} ) ).toEqual( {node: A.toJson(), source: {seconds: 1, nanos: 0}, server: {seconds: 2, nanos: 0}, status: 0x40000000, value: true} );
		expect( editValueJson( {node: A, source: new Date(1000), status: 0, value: 'x'} ) ).toHaveProperty( 'status', 0 );//Good given is still given
		expect( editValueJson( {node: A, source: {seconds: 1, nanos: 123400}, value: 1} )['source'] ).toEqual( {seconds: 1, nanos: 123400} );
	} );
} );

describe( 'pushValue', ()=>{
	const now = new Date( 5000 );
	it( 'is placed by source time, then server time, then arrival', ()=>{
		expect( pushValue( A, {value: 1, source: new Date(1000), server: new Date(1001)}, now ).source?.getTime() ).toBe( 1000 );
		expect( pushValue( A, {value: 1, server: new Date(1001)}, now ).source?.getTime() ).toBe( 1001 );
		expect( pushValue( A, {value: 1}, now ).source?.getTime() ).toBe( 5000 );
	} );
	it( 'carries the quality and drops a refusal', ()=>{
		expect( pushValue( A, {value: 2, sc: 0x40000000, source: new Date(1)}, now ).status ).toBe( 0x40000000 );
		expect( pushValue( A, {value: new OpcError(0x80000000, 'OpcError', '', undefined), sc: 0x80000000, source: new Date(1)}, now ).value ).toBeUndefined();
	} );
} );

describe( 'latestHistTimes', ()=>{
	it( "is each node's last source time held, past values with none", ()=>{
		expect( latestHistTimes( [] ).size ).toBe( 0 );
		expect( latestHistTimes( [at(A, 1000), at(B, 2000), at(A, 3000), at(A, null)] ) ).toEqual( new Map([[A.key, new Date(3000)], [B.key, new Date(2000)]]) );
	} );
} );
//the oldest of the nodes' latest, so a node whose clock runs behind keeps what was stored meanwhile, within the reach of the
//newest (historian-live-trend #5)
describe( 'histResumeTime', ()=>{
	it( "is the oldest node's latest, no further back than the reach before the newest, and none for none", ()=>{
		expect( histResumeTime( [] ) ).toBeUndefined();
		expect( histResumeTime( [new Date(7000), new Date(5000)] ) ).toEqual( new Date(5000) );
		const newest = 5000+histResumeReach+1000;
		expect( histResumeTime( [new Date(newest), new Date(5000)] ) ).toEqual( new Date(newest-histResumeReach) );
	} );
} );

describe( 'applyHistModification', ()=>{
	const mod = ( kind:HistPushedModification["kind"], node:NodeId, ms:number, value?:number ):HistPushedModification=>({ node, target: new Date(ms), kind, value: value==undefined ? undefined : {...at(node, ms), value}, time: new Date(9000), user: 'me' });
	const held = ()=>[ at(A, 1000), at(A, 2000), at(B, 2000), at(A, 3000) ];

	it( 'puts a replacement in place of the node\'s record at the target, and leaves the others', ()=>{
		const y = applyHistModification( held(), mod( 'replace', A, 2000, 7 ) );
		expect( y.map( keyOf ) ).toEqual( held().map( keyOf ) );
		expect( y[1].value ).toBe( 7 );
		expect( y[2].value ).toBe( 2000 );
	} );
	it( 'takes every record of the node at the target for a delete, and only those', ()=>{
		const y = applyHistModification( [at(A, 1000), at(A, 2000), {...at(A, 2000, 0x809D0000), value: undefined}, at(B, 2000), at(A, 3000)], mod( 'delete', A, 2000 ) );//a marker and the value ending its gap
		expect( y.map( keyOf ) ).toEqual( [at(A, 1000), at(B, 2000), at(A, 3000)].map( keyOf ) );
	} );
	it( 'puts an insert where it falls, after the records at its time', ()=>{
		const y = applyHistModification( held(), mod( 'insert', B, 2500, 5 ) );
		expect( y.map( keyOf ) ).toEqual( [at(A, 1000), at(A, 2000), at(B, 2000), at(B, 2500), at(A, 3000)].map( keyOf ) );
		const same = applyHistModification( held(), mod( 'insert', B, 3000, 5 ) );
		expect( same.map( keyOf ) ).toEqual( [at(A, 1000), at(A, 2000), at(B, 2000), at(A, 3000), at(B, 3000)].map( keyOf ) );
	} );
	it( 'returns the same array when nothing changes:  a delete of nothing held, a value before the earliest, a replace with no value', ()=>{
		const values = held();
		expect( applyHistModification( values, mod( 'delete', B, 3000 ) ) ).toBe( values );
		expect( applyHistModification( values, mod( 'insert', A, 500, 1 ) ) ).toBe( values );
		expect( applyHistModification( values, mod( 'replace', A, 2000 ) ) ).toBe( values );
		expect( applyHistModification( [], mod( 'insert', A, 500, 1 ) ) ).toEqual( [] );
	} );
	//the values back to the start of the history:  nothing will read one before the earliest, so it goes first (historian-live-trend #4)
	it( 'puts a value before the earliest first when the values are whole, and one into none', ()=>{
		const first = applyHistModification( held(), mod( 'insert', A, 500, 1 ), true );
		expect( first.map( keyOf ) ).toEqual( [at(A, 500), ...held()].map( keyOf ) );
		expect( first[0].value ).toBe( 1 );
		expect( applyHistModification( [], mod( 'insert', A, 500, 1 ), true ).map( keyOf ) ).toEqual( [keyOf( at(A, 500) )] );
		const values = held();
		expect( applyHistModification( values, mod( 'delete', A, 500 ), true ) ).toBe( values );
		expect( applyHistModification( [], mod( 'delete', A, 500 ), true ) ).toEqual( [] );
	} );
	//a range purge pushes a delete per record it took, and each was a filter of all held (historian-live-trend #12)
	it( 'applies a run of deletes in one pass, in order with the puts around it', ()=>{
		const y = applyHistModifications( held(), [mod( 'delete', A, 1000 ), mod( 'delete', A, 2000 ), mod( 'insert', A, 2000, 7 ), mod( 'delete', B, 2000 ), mod( 'delete', A, 3000 )] );
		expect( y.map( keyOf ) ).toEqual( [keyOf( at(A, 2000) )] );
		expect( y[0].value ).toBe( 7 );
		expect( applyHistModifications( held(), [mod( 'insert', A, 500, 1 )], true ).map( keyOf ) ).toEqual( [at(A, 500), ...held()].map( keyOf ) );
		const values = held();
		expect( applyHistModifications( values, [mod( 'delete', B, 3000 ), mod( 'delete', A, 500 )] ) ).toBe( values );
		expect( applyHistModifications( values, [] ) ).toBe( values );
	} );
} );

describe( 'toHistThresholds', ()=>{
	//historyThresholds' answer (hist.jsonnet):  a member by its row, its node with the server's index and its URI, the template
	//member's row and path, the server's word on an enumeration, and the five thresholds with their sources
	it( 'reads each member with its thresholds and their sources', ()=>{
		const t = toHistThresholds( {update: true, nodes: [
			{ id: 7, node: {ns: 2, i: 6022, nsu: 'urn:jde:pumps'}, templateNode: 3, path: '[{"nsu":"urn:jde:pumps","name":"motor"},{"nsu":"urn:jde:pumps","name":"rpm"}]', enumeration: false, band: 'noRange', thresholds: [
				{ name: 'exceptionDeviation', value: 5, source: 'node' }, { name: 'exceptionDeviationFormat', value: 1, source: 'node' }, { name: 'minTimeInterval', value: 1000, source: 'template' },
				{ name: 'maxTimeInterval', value: 60000, source: 'group' }, { name: 'stepped', value: true, source: 'default' }
			] },
			{ id: 8, node: {i: 2259, nsu: 'http://opcfoundation.org/UA/'}, templateNode: null, path: null, enumeration: null, band: null, thresholds: [{ name: 'exceptionDeviation', value: null, source: 'default' }] }
		]} );
		expect( t.update ).toBe( true );
		expect( t.nodes ).toHaveLength( 2 );
		const [rpm, state] = t.nodes;
		expect( rpm.id ).toBe( 7 );
		expect( rpm.node.key ).toBe( new NodeId( {ns: 2, i: 6022} ).key );//keyed as a page's variables are, by the server's index
		expect( rpm.templateNode ).toBe( 3 );
		expect( rpm.path ).toEqual( [{nsu: 'urn:jde:pumps', name: 'motor'}, {nsu: 'urn:jde:pumps', name: 'rpm'}] );
		expect( histPathText( rpm.path! ) ).toBe( 'motor/rpm' );
		expect( rpm.enumeration ).toBe( false );
		expect( rpm.band ).toBe( 'noRange' );
		expect( rpm.thresholds.map( t=>[t.name, t.value, t.source] ) ).toEqual( [['exceptionDeviation', 5, 'node'], ['exceptionDeviationFormat', 1, 'node'], ['minTimeInterval', 1000, 'template'], ['maxTimeInterval', 60000, 'group'], ['stepped', true, 'default']] );
		expect( state.templateNode ).toBeUndefined();
		expect( state.path ).toBeUndefined();
		expect( state.enumeration ).toBeUndefined();//the server hasn't said
		expect( state.band ).toBeUndefined();//a member the group doesn't hold
		expect( state.thresholds[0].value ).toBeNull();
		expect( histMemberIndexed( rpm.node ) ).toBe( true );
		expect( histMemberIndexed( state.node ) ).toBe( true );//namespace 0's index is 0 on every server
	} );
	//historian-threshold-overrides #2:  before the collector has a member's index, the gateway sends the row's node, its URI alone
	it( "keeps the URI of a member the collector hasn't indexed", ()=>{
		const [member] = toHistThresholds( {update: false, nodes: [{ id: 9, node: {i: 6022, nsu: 'urn:jde:pumps'}, thresholds: [] }]} ).nodes;
		expect( member.node.nsu ).toBe( 'urn:jde:pumps' );
		expect( histMemberIndexed( member.node ) ).toBe( false );
	} );
	it( 'reads nothing from no answer', ()=>{
		expect( toHistThresholds( undefined ) ).toEqual( {update: false, nodes: []} );
		expect( parseHistPath( 'not json' ) ).toBeUndefined();
		expect( parseHistPath( null ) ).toBeUndefined();
	} );
	//a cell's text:  a deviation none sets is 'none', a format its name, stepped a word, the rest the number
	it( 'shows a threshold by its kind', ()=>{
		expect( histThresholdText( {name: 'exceptionDeviation', value: null, source: 'default'} ) ).toBe( 'none' );
		expect( histThresholdText( {name: 'exceptionDeviation', value: 2.5, source: 'group'} ) ).toBe( '2.5' );
		expect( histThresholdText( {name: 'exceptionDeviationFormat', value: 3, source: 'node'} ) ).toBe( 'Percent of EU range' );
		expect( histThresholdText( {name: 'exceptionDeviationFormat', value: 9, source: 'node'} ) ).toBe( '9' );
		expect( histThresholdText( {name: 'minTimeInterval', value: 0, source: 'default'} ) ).toBe( '0' );
		expect( histThresholdText( {name: 'stepped', value: false, source: 'node'} ) ).toBe( 'false' );
	} );
	//a row's own column:  blank stays blank, for the row inherits;  a format by its name, or as stored when it names none
	it( "shows a row's own column", ()=>{
		const fields = {...histThresholdBlank, exceptionDeviationFormat: '1', minTimeInterval: '1000'};
		expect( histThresholdFieldText( fields, 'exceptionDeviation' ) ).toBe( '' );
		expect( histThresholdFieldText( fields, 'exceptionDeviationFormat' ) ).toBe( 'Percent of value' );
		expect( histThresholdFieldText( {...fields, exceptionDeviationFormat: '9'}, 'exceptionDeviationFormat' ) ).toBe( '9' );
		expect( histThresholdFieldText( fields, 'minTimeInterval' ) ).toBe( '1000' );
	} );
} );