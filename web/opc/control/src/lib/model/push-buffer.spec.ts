import { PushBuffer } from './push-buffer';
import { histValue, nodeA as A } from 'jde-opc/testing';

const minute = 60_000;
const times = ( b:PushBuffer, key='a' )=>b.values( [key] ).map( v=>v.source!.getTime() );

//Clients 7 (#420, spec *Live trend*):  the trend's buffer of pushes, a window behind each row's newest
describe( 'PushBuffer', ()=>{
	it( "keeps a row's pushes in time order, one per time, and none without one", ()=>{
		const b = new PushBuffer( 10*minute );
		b.push( 'a', histValue( A, 2000, 2 ) );
		b.push( 'a', histValue( A, 1000, 1 ) );//late
		b.push( 'a', histValue( A, 3000, 3 ) );
		b.push( 'a', histValue( A, 2000, 9 ) );//the time held keeps its value, as mergeHistValues does
		b.push( 'a', histValue( A, null, 4 ) );//no place on a time axis
		expect( b.values( ['a'] ).map( v=>[v.source!.getTime(), v.value] ) ).toEqual( [[1000, 1], [2000, 2], [3000, 3]] );
		expect( b.version() ).toBe( 3 );
	} );
	//the chart adds a point to a line that only grew and rebuilds one whose first point went, so the window prunes a batch at a time
	it( 'drops what is older than the window behind the newest, a batch at a time, keeping the newest before the edge', ()=>{
		const b = new PushBuffer( minute );
		for( let s=0; s<=66; ++s )
			b.push( 'a', histValue( A, s*1000, s ) );
		expect( times( b )[0] ).toBe( 0 );//6 s past the edge:  not yet
		b.push( 'a', histValue( A, 67_000, 67 ) );
		expect( times( b )[0] ).toBe( 6000 );//the edge is 7 s, and the push at 6 s is where a stepped line reaches it from
		expect( times( b ) ).toHaveLength( 62 );
	} );
	it( 'prunes to a window shrunk at once', ()=>{
		const b = new PushBuffer( 10*minute );
		for( let s=0; s<=120; s+=10 )
			b.push( 'a', histValue( A, s*1000, s ) );
		const version = b.version();
		b.window = minute;
		expect( times( b ) ).toEqual( [50_000, 60_000, 70_000, 80_000, 90_000, 100_000, 110_000, 120_000] );
		expect( b.version() ).toBe( version+1 );
	} );
	it( "caps a row, a tenth at a time", ()=>{
		const b = new PushBuffer( 60*minute );
		for( let i=0; i<=PushBuffer.capacity; ++i )
			b.push( 'a', histValue( A, i, i ) );
		expect( b.size( 'a' ) ).toBe( 9000 );
		expect( times( b )[0] ).toBe( 1001 );
	} );
	it( 'answers the rows asked in their order, and forgets a row removed or the lot cleared', ()=>{
		const b = new PushBuffer( minute );
		b.push( 'a', histValue( A, 1000, 1 ) );
		b.push( 'b', histValue( A, 500, 2 ) );
		b.push( 'a', histValue( A, 2000, 3 ) );
		expect( b.values( ['b', 'a'] ).map( v=>v.value ) ).toEqual( [2, 1, 3] );
		expect( b.values( ['c'] ) ).toEqual( [] );
		b.remove( 'a' );
		expect( b.values( ['a', 'b'] ).map( v=>v.value ) ).toEqual( [2] );
		expect( b.size( 'a' ) ).toBe( 0 );
		b.clear();
		expect( b.values( ['b'] ) ).toEqual( [] );
	} );
} );