import { PushBuffer } from './push-buffer';
import { histValue, nodeA as A } from 'jde-opc/testing';

const minute = 60_000;
const times = ( b:PushBuffer, key='a' )=>b.values( [key] ).map( v=>v.source!.getTime() );

//Clients 7 (#420, spec *Live trend*):  the trend's buffer of pushes, a window behind each row's newest
describe( 'PushBuffer', ()=>{
	it( "keeps a row's pushes in time order, one per time, and none without one", ()=>{
		const b = new PushBuffer( 10*minute );
		expect( b.version( 'a' ) ).toBe( 0 );
		b.push( 'a', histValue( A, 2000, 2 ) );
		b.push( 'a', histValue( A, 1000, 1 ) );//late
		b.push( 'a', histValue( A, 3000, 3 ) );
		b.push( 'a', histValue( A, 2000, 9 ) );//the time held keeps its value, as mergeHistValues does
		b.push( 'a', histValue( A, null, 4 ) );//no place on a time axis
		expect( b.values( ['a'] ).map( v=>[v.source!.getTime(), v.value] ) ).toEqual( [[1000, 1], [2000, 2], [3000, 3]] );
		expect( b.version( 'a' ) ).toBe( 3 );
	} );
	//a server that doesn't restamp a status change sent the Bad at the last value's time:  it was dropped, and the trend drew Good on (420 #8)
	it( 'takes a status change at the time it holds', ()=>{
		const b = new PushBuffer( 10*minute ), Bad = 0x80310000;
		b.push( 'a', histValue( A, 1000, 1 ) );
		b.push( 'a', histValue( A, 2000, 2 ) );
		b.push( 'a', histValue( A, 2000, undefined, {status: Bad} ) );
		expect( b.values( ['a'] ).map( v=>[v.source!.getTime(), v.value, v.status] ) ).toEqual( [[1000, 1, 0], [2000, undefined, Bad]] );
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
		const version = b.version( 'a' );
		b.window = minute;
		expect( times( b ) ).toEqual( [50_000, 60_000, 70_000, 80_000, 90_000, 100_000, 110_000, 120_000] );
		expect( b.version( 'a' ) ).toBe( version+1 );
	} );
	//every row is buffered, so one version for all moved the trend's values with every row's push (420 #5)
	it( "moves a row's version with its own changes alone", ()=>{
		const b = new PushBuffer( minute );
		expect( [b.version( 'a' ), b.version( 'b' )] ).toEqual( [0, 0] );
		b.push( 'b', histValue( A, 1000, 1 ) );
		b.push( 'c', histValue( A, 1000, 1 ) );//read by nothing:  no version to move
		expect( [b.version( 'a' ), b.version( 'b' )] ).toEqual( [0, 1] );
		b.push( 'a', histValue( A, 1000, 1 ) );
		b.remove( 'b' );
		expect( [b.version( 'a' ), b.version( 'b' )] ).toEqual( [1, 0] );//b's made again
		b.clear();
		expect( b.version( 'a' ) ).toBe( 0 );
	} );
	it( "caps a row, a tenth at a time", ()=>{
		const b = new PushBuffer( 60*minute );
		for( let i=0; i<=PushBuffer.capacity; ++i )
			b.push( 'a', histValue( A, i, i ) );
		expect( b.size( 'a' ) ).toBe( 9000 );
		expect( times( b )[0] ).toBe( 1001 );
	} );
	//every row is buffered, so a long list held some 720 000 values on the 1 h window, with no bound over all of them (420 #6)
	it( 'holds a total over every row, the largest row not drawn giving way first', ()=>{
		const keys = 'abcdefghijkl'.split( '' ), drawn = new Set( ['a', 'b'] );
		const b = new PushBuffer( 60*minute, key=>drawn.has( key ) ), total = ()=>keys.reduce( (n, k)=>n+b.size( k ), 0 );
		for( const key of keys ){
			for( let i=0; i<9000; ++i )
				b.push( key, histValue( A, i, i ) );
		}
		expect( total() ).toBeLessThanOrEqual( PushBuffer.total );
		expect( [b.size( 'a' ), b.size( 'b' )] ).toEqual( [9000, 9000] );//the drawn rows keep their window
		const sizes = keys.slice( 2 ).map( k=>b.size( k ) );
		expect( Math.min( ...sizes ) ).toBe( 8100 );//largest first:  a tenth off the largest, none twice before the rest once
		expect( sizes.filter( n=>n<9000 ).length ).toBeGreaterThan( 1 );
		expect( keys.every( k=>times( b, k ).at( -1 )==8999 ) ).toBe( true );//each keeps its newest
		b.remove( 'a' );
		b.push( 'a', histValue( A, 0, 0 ) );
		expect( total() ).toBeLessThan( PushBuffer.total );//the count follows a remove
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