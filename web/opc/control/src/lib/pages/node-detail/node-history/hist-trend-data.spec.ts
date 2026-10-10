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
import { HistValue } from '../../../model/hist';
import { NodeId } from '../../../model/node-id';
import { toTrendPoints, trendY, uncertainColor } from './hist-trend-data';

const A = new NodeId( {ns:2, i:1} ), B = new NodeId( {ns:2, i:2} );
const at = ( ms:number|null, value:any, status=0, node=A ):HistValue=>({ node, source: ms==null ? null : new Date(ms), server: null, status, value, bound: false, heartbeat: false });
const Bad = 0x80000000, Uncertain = 0x40000000;

describe( 'trendY', ()=>{
	it( 'draws numbers, booleans and Longs and nothing else', ()=>{
		expect( trendY( 1.5 ) ).toBe( 1.5 );
		expect( trendY( true ) ).toBe( 1 );
		expect( trendY( false ) ).toBe( 0 );
		expect( trendY( Long.fromNumber(42) ) ).toBe( 42 );
		expect( trendY( "text" ) ).toBeUndefined();
		expect( trendY( undefined ) ).toBeUndefined();
	} );
} );

describe( 'toTrendPoints', ()=>{
	it( 'is one point per good value of the node, in order', ()=>{
		const {points, flags} = toTrendPoints( [at(1000, 1), at(2000, 2, 0, B), at(3000, 3)], A, false );
		expect( points.map(p=>[p.x, p.y]) ).toEqual( [[1000, 1], [3000, 3]] );
		expect( flags ).toEqual( [] );
	} );
	it( 'skips a value with no time or no number', ()=>{
		const {points} = toTrendPoints( [at(null, 1), at(1000, "x"), at(2000, 2)], A, false );
		expect( points.map(p=>p.x) ).toEqual( [2000] );
	} );
	it( 'marks an Uncertain value with a triangle and keeps its value', ()=>{
		const {points} = toTrendPoints( [at(1000, 1, Uncertain)], A, false );
		expect( points[0].y ).toBe( 1 );
		expect( points[0].marker ).toMatchObject( {enabled: true, symbol: 'triangle', fillColor: uncertainColor} );
		expect( points[0].custom.status ).toBe( Uncertain );
	} );
	it( 'draws a Bad value as a gap and flags it', ()=>{
		const {points, flags} = toTrendPoints( [at(1000, 1), at(2000, undefined, Bad), at(3000, 3)], A, false );
		expect( points.map(p=>[p.x, p.y]) ).toEqual( [[1000, 1], [2000, null], [3000, 3]] );
		expect( flags ).toHaveLength( 1 );
		expect( flags[0] ).toMatchObject( {x: 2000, title: '!'} );
		expect( flags[0].custom.status ).toBe( Bad );
	} );
	it( 'stepped, holds the last value up to the Bad before the gap', ()=>{
		const {points} = toTrendPoints( [at(1000, 1), at(2000, undefined, Bad), at(3000, 3)], A, true );
		expect( points.map(p=>[p.x, p.y]) ).toEqual( [[1000, 1], [2000, 1], [2000, null], [3000, 3]] );
		expect( points[1].custom.hold ).toBe( true );
		expect( points[1].marker ).toEqual( {enabled: false} );
	} );
	//an aggregate read answers every interval of a failure with the same Bad, and every interval before the history began with
	//Bad_NoData:  a flag a run, where the gap opens, and none for no data, which the gap says alone
	it( 'flags a run of one Bad status once, and Bad_NoData never', ()=>{
		const NoData = 0x809B0000, Other = 0x808C0000;
		const {points, flags} = toTrendPoints( [at(1000, 1), at(2000, undefined, Bad), at(3000, undefined, Bad), at(4000, undefined, Other), at(5000, 5), at(6000, undefined, Bad), at(7000, undefined, NoData), at(8000, undefined, Bad)], A, false );
		expect( points.map(p=>p.y) ).toEqual( [1, null, null, null, 5, null, null, null] );
		expect( flags.map(f=>[f.x, f.custom.status]) ).toEqual( [[2000, Bad], [4000, Other], [6000, Bad], [8000, Bad]] );
	} );
	it( 'stepped, a Bad with nothing before it has nothing to hold', ()=>{
		const {points} = toTrendPoints( [at(1000, undefined, Bad), at(2000, undefined, Bad), at(3000, 3)], A, true );
		expect( points.map(p=>[p.x, p.y]) ).toEqual( [[1000, null], [2000, null], [3000, 3]] );
	} );
} );