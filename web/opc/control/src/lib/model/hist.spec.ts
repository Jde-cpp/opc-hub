if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import Long from 'long';
import { NodeId } from './node-id';
import { OpcError } from './opc-error';
import { HistValue, mergeHistValues, pushValue, qlTime, timeKey, toHistPage } from './hist';

const A = new NodeId( {ns:2, i:1} ), B = new NodeId( {ns:2, i:2} );
const at = ( node:NodeId, ms:number|null, status=0 ):HistValue=>({ node, source: ms==null ? null : new Date(ms), server: null, status, value: ms ?? undefined, bound: false, heartbeat: false });

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
	it( 'is empty for a missing result', ()=>{
		expect( toHistPage( undefined ) ).toEqual( {values: [], continuation: null, nodes: []} );
	} );
} );

describe( 'qlTime', ()=>{
	it( 'is plain seconds and nanos, not a Long', ()=>{
		const t = qlTime( new Date(1700000000500) );
		expect( t ).toEqual( {seconds: 1700000000, nanos: 500000000} );
		expect( typeof t.seconds ).toBe( 'number' );
		expect( JSON.parse(JSON.stringify(t)) ).toEqual( t );
	} );
} );

describe( 'mergeHistValues', ()=>{
	it( 'keeps one record per node and source time, in time order', ()=>{
		const existing = [ at(A, 1000), at(A, 3000) ];
		const merged = mergeHistValues( existing, [at(A, 3000), at(A, 2000), at(B, 3000), at(A, 4000)] );
		expect( merged.map(v=>timeKey(v)) ).toEqual( [at(A,1000), at(A,2000), at(A,3000), at(B,3000), at(A,4000)].map(timeKey) );
	} );
	it( 'returns the same array when nothing is new, so a signal stays quiet', ()=>{
		const existing = [ at(A, 1000), at(A, 2000) ];
		expect( mergeHistValues( existing, [at(A, 1000)] ) ).toBe( existing );
		expect( mergeHistValues( existing, [] ) ).toBe( existing );
	} );
	it( 'puts a value with no source time last', ()=>{
		const merged = mergeHistValues( [at(A, null)], [at(A, 1000)] );
		expect( merged.map(v=>v.source?.getTime() ?? null) ).toEqual( [1000, null] );
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